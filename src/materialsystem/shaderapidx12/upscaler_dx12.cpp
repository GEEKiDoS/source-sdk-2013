#include "upscaler_dx12.h"
#include "command_recorder_dx12.h"
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "upscaler_nr_shaders_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "renderparm.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier0/platform.h"
#include "tier1/convar.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <xess/xess_d3d12.h>
// FidelityFX SDK v2 (split runtime): amd_fidelityfx_loader_dx12.dll dispatches to amd_fidelityfx_upscaler_dx12.dll.
#include <upscalers/include/ffx_upscale.h>
#include <api/include/dx12/ffx_api_dx12.h>
#include <nvsdk_ngx.h>

namespace shaderapidx12
{
static_assert(kNrMaxLayersDX12 == DX12_NR_MAX_LAYERS, "renderparm.h and the provider agree on the DLSS-NR layer limit");

const char *UpscalerKindNameDX12(UpscalerKindDX12 kind)
{
    switch (kind)
    {
    case UpscalerKindDX12::DLSS: return "DLSS/DLAA";
    case UpscalerKindDX12::FSR: return "FSR native AA";
    case UpscalerKindDX12::XeSS: return "XeSS AA";
    default: return "none";
    }
}

namespace
{
// Failure codes in UpscalerReplayResultDX12::code/nrCode; provider results are folded into the low 16 bits.
constexpr uint32_t kReplayForced = 0x10001u, kReplayNoContext = 0x10002u;
constexpr D3D12_RESOURCE_STATES kRead = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kNpsr = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
constexpr D3D12_RESOURCE_STATES kUav = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
// Descriptor block per compute pass: t0..t3 then u0..u1.
constexpr uint32_t kPassDescriptors = 6;

template<class Fn> bool ResolveExport(HMODULE module, const char *name, Fn &fn)
{
    fn = reinterpret_cast<Fn>(GetProcAddress(module, name));
    return fn != nullptr;
}

bool FileExists(const std::wstring &path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

HMODULE LoadProviderModule(const std::wstring &dir, const wchar_t *name)
{
    const std::wstring path = dir + name;
    if (!FileExists(path)) return nullptr;
    return LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}

std::string FileVersion(const std::wstring &path)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (!size) return "unknown version";
    std::vector<unsigned char> data(size);
    VS_FIXEDFILEINFO *info = nullptr; UINT length = 0;
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data()) || !VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &length) || !info)
        return "unknown version";
    char text[64];
    snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS));
    return text;
}

void AddTransition(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, D3D12_RESOURCE_BARRIER *barriers, UINT &count)
{
    if (!resource || before == after) return;
    D3D12_RESOURCE_BARRIER &b = barriers[count++];
    b = {}; b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION; b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES; b.Transition.StateBefore = before; b.Transition.StateAfter = after;
}

void Transition(ID3D12GraphicsCommandList *list, ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER barrier; UINT count = 0;
    AddTransition(resource, before, after, &barrier, count);
    if (count) list->ResourceBarrier(1, &barrier);
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTexture(ID3D12Device *device, uint32_t width, uint32_t height, DXGI_FORMAT format, D3D12_RESOURCE_STATES state)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, state, nullptr, IID_PPV_ARGS(&resource)))) resource.Reset();
    return resource;
}

}

// Recorded payload: the stable provider object plus the dispatch copied at record time.
struct UpscalerReplayPayloadDX12
{
    const CUpscalerDX12 *owner;
    UpscalerDispatchDX12 dispatch;
};
static_assert(std::is_trivially_copyable<UpscalerReplayPayloadDX12>::value, "external command payloads are copied bytewise");

struct CUpscalerDX12::XeSSApi
{
    decltype(&xessGetVersion) getVersion = nullptr;
    decltype(&xessD3D12CreateContext) createContext = nullptr;
    decltype(&xessD3D12Init) init = nullptr;
    decltype(&xessD3D12Execute) execute = nullptr;
    decltype(&xessDestroyContext) destroyContext = nullptr;
    decltype(&xessGetInputResolution) getInputResolution = nullptr;
    decltype(&xessSetVelocityScale) setVelocityScale = nullptr;
    decltype(&xessSetJitterScale) setJitterScale = nullptr;      // optional
    decltype(&xessGetJitterScale) getJitterScale = nullptr;      // optional
    decltype(&xessGetIntelXeFXVersion) getXeFXVersion = nullptr; // optional
    decltype(&xessSetLoggingCallback) setLogging = nullptr;      // optional
};
struct CUpscalerDX12::FfxApi
{
    HMODULE upscaler = nullptr; // effect DLL, loaded by full path so the loader's bare-name LoadLibraryA finds it
    PfnFfxCreateContext createContext = nullptr;
    PfnFfxDestroyContext destroyContext = nullptr;
    PfnFfxQuery query = nullptr;
    PfnFfxDispatch dispatch = nullptr;
    bool fsr4 = false; // the selected provider is FSR 4 (UNTESTED: no RDNA3/RDNA4 GPU was available)
};
// NGX driver core (_nvngx.dll). Exports differ from the static-library wrappers: Init_ProjectID takes the SDK version
// before the feature info.
struct CUpscalerDX12::NgxApi
{
    using InitProjectId = NVSDK_NGX_Result(NVSDK_CONV *)(const char *, NVSDK_NGX_EngineType, const char *, const wchar_t *, ID3D12Device *, NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo *);
    using Shutdown1 = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12Device *);
    using Parameters = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Parameter **);
    using DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Parameter *);
    using CreateFeature = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12GraphicsCommandList *, NVSDK_NGX_Feature, NVSDK_NGX_Parameter *, NVSDK_NGX_Handle **);
    using ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV *)(NVSDK_NGX_Handle *);
    using EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV *)(ID3D12GraphicsCommandList *, const NVSDK_NGX_Handle *, NVSDK_NGX_Parameter *, PFN_NVSDK_NGX_ProgressCallback);
    InitProjectId init = nullptr;
    Shutdown1 shutdown = nullptr;
    Parameters capabilities = nullptr, allocate = nullptr;
    DestroyParameters destroy = nullptr;
    CreateFeature create = nullptr;
    ReleaseFeature release = nullptr;
    EvaluateFeature evaluate = nullptr;
    bool initialized = false;
    NVSDK_NGX_Parameter *caps = nullptr;       // capability block: DLSS availability, and the DLSS-NR parameter block
    NVSDK_NGX_Parameter *dlssParams = nullptr; // DLSS create/evaluate block, owned here
    std::wstring dataPath;
    const wchar_t *pathList[1] = {};
};
// DLSS-NR snippet calls go through nvngx.dll_dlssnr_dx12.dll: nvngx_dlssnr.dll rejects any caller whose return address
// is outside a module path containing "nvngx.dll".
struct CUpscalerDX12::NrApi
{
    using Load = int(__cdecl *)(const wchar_t *);
    using Init = int(__cdecl *)(unsigned long long, const wchar_t *, ID3D12Device *, int, const void *);
    using Create = int(__cdecl *)(ID3D12GraphicsCommandList *, int, void *, void **);
    using Evaluate = int(__cdecl *)(ID3D12GraphicsCommandList *, const void *, const void *);
    using Release = int(__cdecl *)(void *);
    using Shutdown = int(__cdecl *)(ID3D12Device *);
    using Unload = void(__cdecl *)();
    Load load = nullptr; Init init = nullptr; Create create = nullptr; Evaluate evaluate = nullptr; Release release = nullptr; Shutdown shutdown = nullptr; Unload unload = nullptr;
    bool initialized = false;
};
struct CUpscalerDX12::NrChain
{
    DlssNrTuningDX12 tuning;
    uint32_t layers = 0, width = 0, height = 0;
    ID3D12Resource *source = nullptr;
    // proxy: NON_PIXEL_SHADER_RESOURCE between replays; answer and out[]: UNORDERED_ACCESS.
    Microsoft::WRL::ComPtr<ID3D12Resource> proxy, answer, out[2];
    void *handles[kNrMaxLayersDX12] = {};
};

CUpscalerDX12::CUpscalerDX12() = default;
CUpscalerDX12::~CUpscalerDX12() { Shutdown(); }

bool CUpscalerDX12::Initialize(ID3D12Device *device, const MaterialAdapterInfo_t &adapter, const wchar_t *moduleDir, bool verbose)
{
    Shutdown();
    if (!device || !moduleDir) return false;
    device_ = device; verbose_ = verbose; vendor_ = adapter.m_VendorID; deviceId_ = adapter.m_DeviceID;
    dir_ = moduleDir;
    if (!dir_.empty() && dir_.back() != L'\\' && dir_.back() != L'/') dir_ += L'\\';
    LoadNgx(dir_);
    LoadFfx(dir_);
    LoadXeSS(dir_);
    ProbeNr(dir_);
    Msg("ShaderAPIDX12 upscaler: adapter vendor 0x%04x device 0x%04x\n", vendor_, deviceId_);
    const auto report = [](const char *name, const Provider &p) {
        if (p.available) Msg("ShaderAPIDX12 upscaler: %s available (%s)\n", name, p.version.c_str());
        else Msg("ShaderAPIDX12 upscaler: %s unavailable: %s\n", name, p.reason.c_str());
    };
    report("dlss", dlss_); report("fsr", fsr_); report("xess", xess_); report("dlssnr", nr_);
    return true;
}

UpscalerKindDX12 CUpscalerDX12::Resolve(int mode) const
{
    switch (mode)
    {
    case 1:
        if (dlss_.available) return UpscalerKindDX12::DLSS;
        if (vendor_ == 0x8086 && xess_.available) return UpscalerKindDX12::XeSS;
        if (fsr_.available) return UpscalerKindDX12::FSR;
        if (xess_.available) return UpscalerKindDX12::XeSS;
        return UpscalerKindDX12::None;
    case 2: return dlss_.available ? UpscalerKindDX12::DLSS : UpscalerKindDX12::None;
    case 3: return fsr_.available ? UpscalerKindDX12::FSR : UpscalerKindDX12::None;
    case 4: return xess_.available ? UpscalerKindDX12::XeSS : UpscalerKindDX12::None;
    default: return UpscalerKindDX12::None;
    }
}

bool CUpscalerDX12::LoadXeSS(const std::wstring &dir)
{
    xess_.module = LoadProviderModule(dir, L"libxess.dll");
    if (!xess_.module) { xess_.reason = "libxess.dll not installed beside the renderer"; return false; }
    auto *api = new XeSSApi;
    const auto fail = [&](const char *reason) { delete api; FreeLibrary(xess_.module); xess_.module = nullptr; xess_.reason = reason; return false; };
    if (!ResolveExport(xess_.module, "xessGetVersion", api->getVersion) || !ResolveExport(xess_.module, "xessD3D12CreateContext", api->createContext) ||
        !ResolveExport(xess_.module, "xessD3D12Init", api->init) || !ResolveExport(xess_.module, "xessD3D12Execute", api->execute) ||
        !ResolveExport(xess_.module, "xessDestroyContext", api->destroyContext) || !ResolveExport(xess_.module, "xessGetInputResolution", api->getInputResolution) ||
        !ResolveExport(xess_.module, "xessSetVelocityScale", api->setVelocityScale))
        return fail("libxess.dll lacks a required D3D12 export");
    ResolveExport(xess_.module, "xessSetJitterScale", api->setJitterScale);
    ResolveExport(xess_.module, "xessGetJitterScale", api->getJitterScale);
    ResolveExport(xess_.module, "xessGetIntelXeFXVersion", api->getXeFXVersion);
    ResolveExport(xess_.module, "xessSetLoggingCallback", api->setLogging);
    xess_version_t version{};
    if (api->getVersion(&version) != XESS_RESULT_SUCCESS) return fail("xessGetVersion failed");
    char text[96]; snprintf(text, sizeof(text), "XeSS %u.%u.%u", version.major, version.minor, version.patch); xess_.version = text;
    // Context creation is the device capability decision (the DP4a path needs SM 6.4; the runtime decides).
    xess_context_handle_t probe = nullptr;
    const xess_result_t created = api->createContext(device_, &probe);
    if (created != XESS_RESULT_SUCCESS || !probe)
    {
        snprintf(text, sizeof(text), "xessD3D12CreateContext rejected this device (%d)", static_cast<int>(created));
        return fail(text);
    }
    if (api->getXeFXVersion)
    {
        xess_version_t xefx{};
        if (api->getXeFXVersion(probe, &xefx) == XESS_RESULT_SUCCESS && (xefx.major || xefx.minor || xefx.patch))
        { snprintf(text, sizeof(text), ", XeFX %u.%u.%u", xefx.major, xefx.minor, xefx.patch); xess_.version += text; }
    }
    api->destroyContext(probe);
    xessApi_ = api; xess_.available = true;
    return true;
}

bool CUpscalerDX12::LoadFfx(const std::wstring &dir)
{
    // The loader resolves effect DLLs by bare name; the effect DLL is loaded first by full path so that name matches
    // the already-loaded module instead of whatever the process search order would find.
    HMODULE upscaler = LoadProviderModule(dir, L"amd_fidelityfx_upscaler_dx12.dll");
    if (!upscaler) { fsr_.reason = "amd_fidelityfx_upscaler_dx12.dll not installed beside the renderer"; return false; }
    fsr_.module = LoadProviderModule(dir, L"amd_fidelityfx_loader_dx12.dll");
    if (!fsr_.module) { FreeLibrary(upscaler); fsr_.reason = "amd_fidelityfx_loader_dx12.dll not installed beside the renderer"; return false; }
    auto *api = new FfxApi; api->upscaler = upscaler;
    const auto fail = [&](const std::string &reason) { FreeLibrary(api->upscaler); delete api; FreeLibrary(fsr_.module); fsr_.module = nullptr; fsr_.reason = reason; return false; };
    if (!ResolveExport(fsr_.module, "ffxCreateContext", api->createContext) || !ResolveExport(fsr_.module, "ffxDestroyContext", api->destroyContext) ||
        !ResolveExport(fsr_.module, "ffxQuery", api->query) || !ResolveExport(fsr_.module, "ffxDispatch", api->dispatch))
        return fail("amd_fidelityfx_loader_dx12.dll lacks a required FFX API export");
    uint64_t count = 0;
    ffxQueryDescGetVersions versions{}; versions.header.type = FFX_API_QUERY_DESC_TYPE_GET_VERSIONS;
    versions.createDescType = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; versions.device = device_; versions.outputCount = &count;
    if (api->query(nullptr, &versions.header) != FFX_API_RETURN_OK || !count) return fail("no FFX upscale provider enumerated");
    std::vector<uint64_t> ids(static_cast<size_t>(count)); std::vector<const char *> names(static_cast<size_t>(count));
    versions.versionIds = ids.data(); versions.versionNames = names.data();
    if (api->query(nullptr, &versions.header) != FFX_API_RETURN_OK) return fail("FFX version query failed");
    // The list is newest first. FSR 4 is selected only on AMD adapters (it needs RDNA3/RDNA4); elsewhere the newest
    // FSR 3.x provider runs. The opaque id is never hard-coded.
    std::string all;
    int selected = -1;
    for (uint64_t i = 0; i < count; ++i)
    {
        const char *name = names[static_cast<size_t>(i)] ? names[static_cast<size_t>(i)] : "?";
        all += i ? ", " : ""; all += name;
        const char *space = strrchr(name, ' ');
        const int major = atoi(space ? space + 1 : name);
        if (selected < 0 && (major == 3 || (major >= 4 && vendor_ == 0x1002))) selected = static_cast<int>(i);
    }
    if (selected < 0) return fail("no usable FFX upscale provider among: " + all);
    const char *chosen = names[static_cast<size_t>(selected)] ? names[static_cast<size_t>(selected)] : "?";
    const char *space = strrchr(chosen, ' ');
    api->fsr4 = atoi(space ? space + 1 : chosen) >= 4;
    fsr_.versionId = ids[static_cast<size_t>(selected)];
    fsr_.version = std::string(chosen) + (api->fsr4 ? " [FSR 4 path UNTESTED]" : "") + "; loader " + FileVersion(dir + L"amd_fidelityfx_loader_dx12.dll") + "; enumerated " + all;
    ffxApi_ = api; fsr_.available = true;
    return true;
}

static void NVSDK_CONV NgxLog(const char *message, NVSDK_NGX_Logging_Level, NVSDK_NGX_Feature feature)
{
    Msg("ShaderAPIDX12 upscaler: NGX[%d]: %s", static_cast<int>(feature), message ? message : "\n");
}

bool CUpscalerDX12::LoadNgx(const std::wstring &dir)
{
    if (vendor_ != 0x10de) { dlss_.reason = ngxCore_.reason = "not an NVIDIA adapter"; return false; }
    if (!FileExists(dir + L"nvngx_dlss.dll")) { dlss_.reason = "nvngx_dlss.dll not installed beside the renderer"; }
    // The driver publishes the NGX core location; _nvngx.dll is the core, nvngx.dll the legacy name.
    wchar_t registered[MAX_PATH]{}; DWORD bytes = sizeof(registered);
    std::wstring coreDir;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"System\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore", L"NGXPath", RRF_RT_REG_SZ, nullptr, registered, &bytes) == ERROR_SUCCESS)
    {
        coreDir = registered;
        const DWORD attributes = GetFileAttributesW(coreDir.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY)) coreDir.resize(coreDir.find_last_of(L"\\/") + 1);
        if (!coreDir.empty() && coreDir.back() != L'\\' && coreDir.back() != L'/') coreDir += L'\\';
    }
    for (const wchar_t *name : {L"_nvngx.dll", L"nvngx.dll"})
        if (!ngxCore_.module && !coreDir.empty()) ngxCore_.module = LoadProviderModule(coreDir, name);
    if (!ngxCore_.module) { dlss_.reason = ngxCore_.reason = "NGX core (_nvngx.dll) not found through HKLM\\...\\nvlddmkm\\NGXCore\\NGXPath"; return false; }
    auto *api = new NgxApi;
    const auto fail = [&](const std::string &reason) {
        if (api->caps && api->destroy) api->destroy(api->caps);
        if (api->initialized && api->shutdown) api->shutdown(device_);
        delete api; FreeLibrary(ngxCore_.module); ngxCore_.module = nullptr;
        ngxCore_.reason = reason; if (dlss_.reason.empty()) dlss_.reason = reason;
        return false;
    };
    if (!ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_Init_ProjectID", api->init) || !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_Shutdown1", api->shutdown) ||
        !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_GetCapabilityParameters", api->capabilities) || !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_AllocateParameters", api->allocate) ||
        !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_DestroyParameters", api->destroy) || !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_CreateFeature", api->create) ||
        !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_ReleaseFeature", api->release) || !ResolveExport(ngxCore_.module, "NVSDK_NGX_D3D12_EvaluateFeature", api->evaluate))
        return fail("NGX core lacks a required D3D12 export");
    // Feature DLLs (nvngx_dlss.dll) are searched in PathListInfo; the renderer directory is the only entry.
    api->dataPath = dir.substr(0, dir.size() - 1);
    api->pathList[0] = api->dataPath.c_str();
    NVSDK_NGX_FeatureCommonInfo info{};
    info.PathListInfo.Path = api->pathList; info.PathListInfo.Length = 1;
    if (verbose_) { info.LoggingInfo.LoggingCallback = NgxLog; info.LoggingInfo.MinimumLoggingLevel = NVSDK_NGX_LOGGING_LEVEL_ON; }
    // No NVIDIA-issued application id: a custom-engine project id (any GUID) is the documented alternative.
    const NVSDK_NGX_Result init = api->init("6c3d9c52-8c1e-4b2a-9f4e-1b5a0d7e2c41", NVSDK_NGX_ENGINE_TYPE_CUSTOM, "Source SDK 2013 DX12",
                                            api->dataPath.c_str(), device_, NVSDK_NGX_Version_API, &info);
    char text[160];
    if (NVSDK_NGX_FAILED(init)) { snprintf(text, sizeof(text), "NVSDK_NGX_D3D12_Init_ProjectID failed (0x%08x)", static_cast<unsigned>(init)); return fail(text); }
    api->initialized = true;
    if (NVSDK_NGX_FAILED(api->capabilities(&api->caps)) || !api->caps) return fail("NVSDK_NGX_D3D12_GetCapabilityParameters failed");
    ngxCore_.available = true; ngxCore_.version = FileVersion(coreDir + L"_nvngx.dll");
    ngxApi_ = api;
    if (!dlss_.reason.empty()) return false;
    unsigned int available = 0, needsDriver = 0, minMajor = 0, minMinor = 0;
    api->caps->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &available);
    api->caps->Get(NVSDK_NGX_Parameter_SuperSampling_NeedsUpdatedDriver, &needsDriver);
    api->caps->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMajor, &minMajor);
    api->caps->Get(NVSDK_NGX_Parameter_SuperSampling_MinDriverVersionMinor, &minMinor);
    if (needsDriver) { snprintf(text, sizeof(text), "DLSS needs driver %u.%u or newer", minMajor, minMinor); dlss_.reason = text; return false; }
    if (!available) { dlss_.reason = "NGX reports SuperSampling unavailable on this adapter"; return false; }
    if (NVSDK_NGX_FAILED(api->allocate(&api->dlssParams)) || !api->dlssParams) { dlss_.reason = "NVSDK_NGX_D3D12_AllocateParameters failed"; return false; }
    dlss_.version = "nvngx_dlss.dll " + FileVersion(dir + L"nvngx_dlss.dll") + ", NGX core " + ngxCore_.version;
    dlss_.available = true;
    return true;
}

void CUpscalerDX12::ProbeNr(const std::wstring &dir)
{
    if (!ngxCore_.available) { nr_.reason = "NGX core unavailable: " + ngxCore_.reason; return; }
    if (!FileExists(dir + L"nvngx_dlssnr.dll")) { nr_.reason = "nvngx_dlssnr.dll not installed beside the renderer"; return; }
    if (!FileExists(dir + L"nvngx.dll_dlssnr_dx12.dll")) { nr_.reason = "nvngx.dll_dlssnr_dx12.dll (snippet forwarder) not built beside the renderer"; return; }
    nr_.version = "nvngx_dlssnr.dll " + FileVersion(dir + L"nvngx_dlssnr.dll");
    nr_.available = true;
}

bool CUpscalerDX12::InitNrSnippet()
{
    if (nrApi_ && nrApi_->initialized) return true;
    if (!nr_.available || !ngxApi_ || !ngxApi_->caps) return false;
    const auto disable = [&](const std::string &reason) {
        Warning("ShaderAPIDX12 upscaler: DLSS-NR unavailable: %s\n", reason.c_str());
        if (nrApi_ && nrApi_->unload) nrApi_->unload();
        delete nrApi_; nrApi_ = nullptr;
        if (nr_.module) FreeLibrary(nr_.module);
        nr_.module = nullptr; nr_.available = false; nr_.reason = reason;
        return false;
    };
    nr_.module = LoadProviderModule(dir_, L"nvngx.dll_dlssnr_dx12.dll");
    if (!nr_.module) return disable("nvngx.dll_dlssnr_dx12.dll failed to load");
    nrApi_ = new NrApi;
    if (!ResolveExport(nr_.module, "NrFwd_Load", nrApi_->load) || !ResolveExport(nr_.module, "NrFwd_Init", nrApi_->init) || !ResolveExport(nr_.module, "NrFwd_CreateFeature", nrApi_->create) ||
        !ResolveExport(nr_.module, "NrFwd_EvaluateFeature", nrApi_->evaluate) || !ResolveExport(nr_.module, "NrFwd_ReleaseFeature", nrApi_->release) ||
        !ResolveExport(nr_.module, "NrFwd_Shutdown", nrApi_->shutdown) || !ResolveExport(nr_.module, "NrFwd_Unload", nrApi_->unload))
        return disable("snippet forwarder lacks a required export");
    if (!nrApi_->load((dir_ + L"nvngx_dlssnr.dll").c_str())) return disable("nvngx_dlssnr.dll failed to load or lacks a D3D12 export");
    // The snippet is initialised the way the NGX core initialises it: SDK 0x15 and the core's capability block.
    const int init = nrApi_->init(0x24480451ull, ngxApi_->dataPath.c_str(), device_, 0x0000015, ngxApi_->caps);
    if (NVSDK_NGX_FAILED(static_cast<NVSDK_NGX_Result>(init)))
    {
        char text[96]; snprintf(text, sizeof(text), "nvngx_dlssnr.dll Init_Ext failed (0x%08x)", static_cast<unsigned>(init));
        return disable(text);
    }
    nrApi_->initialized = true;
    Msg("ShaderAPIDX12 upscaler: DLSS-NR snippet initialised (%s)\n", nr_.version.c_str());
    return true;
}

bool CUpscalerDX12::EnsureCompute()
{
    if (computeRoot_ && nrPso_ && depthPso_) return true;
    D3D12_DESCRIPTOR_RANGE ranges[2]{};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; ranges[0].NumDescriptors = 4; ranges[0].OffsetInDescriptorsFromTableStart = 0;
    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV; ranges[1].NumDescriptors = 2; ranges[1].OffsetInDescriptorsFromTableStart = 0;
    D3D12_ROOT_PARAMETER parameters[3]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; parameters[0].Constants.Num32BitValues = sizeof(NrConstantsDX12) / 4;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; parameters[1].DescriptorTable.NumDescriptorRanges = 1; parameters[1].DescriptorTable.pDescriptorRanges = &ranges[0];
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; parameters[2].DescriptorTable.NumDescriptorRanges = 1; parameters[2].DescriptorTable.pDescriptorRanges = &ranges[1];
    D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP; sampler.MaxLOD = D3D12_FLOAT32_MAX;
    D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 3; desc.pParameters = parameters; desc.NumStaticSamplers = 1; desc.pStaticSamplers = &sampler;
    Microsoft::WRL::ComPtr<ID3DBlob> blob, error, mainCode, depthCode;
    HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    if (SUCCEEDED(hr)) hr = device_->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&computeRoot_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12 upscaler: compute root signature failed (0x%08x)\n", static_cast<unsigned>(hr)); return false; }
    for (int pass = 0; pass < 2; ++pass)
    {
        Microsoft::WRL::ComPtr<ID3DBlob> &code = pass ? depthCode : mainCode;
        error.Reset();
        hr = D3DCompile(kNrShaderSourceDX12, sizeof(kNrShaderSourceDX12) - 1, "upscaler_nr", nullptr, nullptr, pass ? "CSDepth" : "CSMain", "cs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &error);
        if (FAILED(hr)) { Warning("ShaderAPIDX12 upscaler: compute shader compile failed (0x%08x): %s\n", static_cast<unsigned>(hr), error ? static_cast<const char *>(error->GetBufferPointer()) : ""); computeRoot_.Reset(); return false; }
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso{}; pso.pRootSignature = computeRoot_.Get(); pso.CS = {code->GetBufferPointer(), code->GetBufferSize()};
        hr = device_->CreateComputePipelineState(&pso, IID_PPV_ARGS(pass ? &depthPso_ : &nrPso_));
        if (FAILED(hr)) { Warning("ShaderAPIDX12 upscaler: compute PSO failed (0x%08x)\n", static_cast<unsigned>(hr)); computeRoot_.Reset(); nrPso_.Reset(); return false; }
    }
    descriptorStride_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    return true;
}

bool CUpscalerDX12::EnsureDepthClone(uint32_t width, uint32_t height)
{
    if (depthClone_)
    {
        const D3D12_RESOURCE_DESC desc = depthClone_->GetDesc();
        if (desc.Width == width && desc.Height == height) return true;
        depthClone_.Reset(); // only reached behind the GPU-idle boundary that precedes feature/chain creation
    }
    depthClone_ = CreateTexture(device_, width, height, DXGI_FORMAT_R32_FLOAT, kNpsr);
    if (!depthClone_) Warning("ShaderAPIDX12 upscaler: depth clone %ux%u creation failed\n", width, height);
    return depthClone_ != nullptr;
}

ID3D12GraphicsCommandList *CUpscalerDX12::BeginImmediate()
{
    if (!immediateAllocator_ && FAILED(device_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&immediateAllocator_)))) return nullptr;
    if (!immediateFence_ && FAILED(device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&immediateFence_)))) return nullptr;
    if (FAILED(immediateAllocator_->Reset())) return nullptr;
    if (!immediateList_)
    {
        if (FAILED(device_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, immediateAllocator_.Get(), nullptr, IID_PPV_ARGS(&immediateList_)))) return nullptr;
    }
    else if (FAILED(immediateList_->Reset(immediateAllocator_.Get(), nullptr))) return nullptr;
    return immediateList_.Get();
}

bool CUpscalerDX12::EndImmediate(ID3D12CommandQueue *queue)
{
    if (!queue || FAILED(immediateList_->Close())) return false;
    ID3D12CommandList *lists[] = {immediateList_.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(immediateFence_.Get(), ++immediateValue_))) return false;
    if (immediateFence_->GetCompletedValue() < immediateValue_)
    {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event) return false;
        const bool waited = SUCCEEDED(immediateFence_->SetEventOnCompletion(immediateValue_, event)) && WaitForSingleObject(event, INFINITE) == WAIT_OBJECT_0;
        CloseHandle(event);
        if (!waited) return false;
    }
    return true;
}

bool CUpscalerDX12::PrepareFeature(const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *queue)
{
    if (state_ == State::Ready) return feature_ == desc; // a different feature needs ReleaseFeature behind a GPU-idle boundary first
    if (state_ == State::Failed && feature_ == desc) return false;
    if (!device_ || desc.kind == UpscalerKindDX12::None || !desc.width || !desc.height) return false;
    feature_ = desc;
    bool created = false;
    if (desc.kind == UpscalerKindDX12::XeSS) created = CreateXeSS(desc);
    else if (desc.kind == UpscalerKindDX12::FSR) created = CreateFfx(desc);
    else if (desc.kind == UpscalerKindDX12::DLSS) created = CreateDlss(desc, queue);
    state_ = created ? State::Ready : State::Failed;
    const Provider &provider = desc.kind == UpscalerKindDX12::XeSS ? xess_ : desc.kind == UpscalerKindDX12::FSR ? fsr_ : dlss_;
    if (created) Msg("ShaderAPIDX12 upscaler: %s ready at %ux%u (%s)\n", UpscalerKindNameDX12(desc.kind), desc.width, desc.height, provider.version.c_str());
    else Warning("ShaderAPIDX12 upscaler: %s feature creation failed at %ux%u\n", UpscalerKindNameDX12(desc.kind), desc.width, desc.height);
    return created;
}

static void XeSSLog(const char *message, xess_logging_level_t level)
{
    if (level >= XESS_LOGGING_LEVEL_WARNING) Warning("ShaderAPIDX12 upscaler: XeSS: %s\n", message ? message : "");
}
static void FfxLog(uint32_t type, const wchar_t *message)
{
    Warning("ShaderAPIDX12 upscaler: FSR %s: %ls\n", type == FFX_API_MESSAGE_TYPE_ERROR ? "error" : "warning", message ? message : L"");
}

bool CUpscalerDX12::CreateXeSS(const UpscalerFeatureDescDX12 &desc)
{
    if (!xessApi_) return false;
    xess_context_handle_t context = nullptr;
    xess_result_t result = xessApi_->createContext(device_, &context);
    if (result != XESS_RESULT_SUCCESS || !context) { Warning("ShaderAPIDX12 upscaler: xessD3D12CreateContext failed (%d)\n", static_cast<int>(result)); return false; }
    const auto fail = [&](const char *what, int code) { Warning("ShaderAPIDX12 upscaler: %s failed (%d)\n", what, code); xessApi_->destroyContext(context); return false; };
    if (xessApi_->setLogging) xessApi_->setLogging(context, XESS_LOGGING_LEVEL_WARNING, XeSSLog);
    xess_d3d12_init_params_t init{};
    init.outputResolution = {desc.width, desc.height};
    init.qualitySetting = XESS_QUALITY_SETTING_AA;
    // Linear scRGB input (no LDR flag), output-resolution jitter-free motion vectors (no JITTERED_MV flag).
    init.initFlags = XESS_INIT_FLAG_HIGH_RES_MV | XESS_INIT_FLAG_ENABLE_AUTOEXPOSURE | (desc.depthInverted ? XESS_INIT_FLAG_INVERTED_DEPTH : 0);
    result = xessApi_->init(context, &init);
    if (result != XESS_RESULT_SUCCESS) return fail("xessD3D12Init", result);
    xess_2d_t output{desc.width, desc.height}, input{};
    result = xessApi_->getInputResolution(context, &output, XESS_QUALITY_SETTING_AA, &input);
    if (result != XESS_RESULT_SUCCESS) return fail("xessGetInputResolution", result);
    if (input.x != desc.width || input.y != desc.height) return fail("XeSS AA native input resolution check", static_cast<int>(input.x));
    // _rt_MotionVectors holds current-minus-previous UV; XeSS wants previous-minus-current pixels.
    result = xessApi_->setVelocityScale(context, -static_cast<float>(desc.width), -static_cast<float>(desc.height));
    if (result != XESS_RESULT_SUCCESS) return fail("xessSetVelocityScale", result);
    if (xessApi_->setJitterScale)
    {
        result = xessApi_->setJitterScale(context, 1.f, 1.f);
        if (result != XESS_RESULT_SUCCESS) return fail("xessSetJitterScale", result);
        float jx = 0, jy = 0;
        if (xessApi_->getJitterScale && xessApi_->getJitterScale(context, &jx, &jy) == XESS_RESULT_SUCCESS)
            Msg("ShaderAPIDX12 upscaler: XeSS effective jitter scale %.2f %.2f\n", jx, jy);
    }
    xessContext_ = context;
    return true;
}

bool CUpscalerDX12::CreateFfx(const UpscalerFeatureDescDX12 &desc)
{
    if (!ffxApi_) return false;
    // Chain: upscale -> DX12 backend -> API version (required by v2) -> provider override (the enumerated id).
    ffxOverrideVersion override{}; override.header.type = FFX_API_DESC_TYPE_OVERRIDE_VERSION; override.versionId = fsr_.versionId;
    ffxCreateContextDescUpscaleVersion version{}; version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE_VERSION; version.version = FFX_UPSCALER_VERSION;
    version.header.pNext = &override.header;
    ffxCreateBackendDX12Desc backend{}; backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12; backend.device = device_; backend.header.pNext = &version.header;
    ffxCreateContextDescUpscale create{}; create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE; create.header.pNext = &backend.header;
    create.flags = FFX_UPSCALE_ENABLE_HIGH_DYNAMIC_RANGE | FFX_UPSCALE_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS | FFX_UPSCALE_ENABLE_AUTO_EXPOSURE |
                   (desc.depthInverted ? FFX_UPSCALE_ENABLE_DEPTH_INVERTED : 0) | (verbose_ ? FFX_UPSCALE_ENABLE_DEBUG_CHECKING : 0);
    create.maxRenderSize = {desc.width, desc.height}; create.maxUpscaleSize = {desc.width, desc.height};
    create.fpMessage = FfxLog;
    if (ffxApi_->fsr4) Warning("ShaderAPIDX12 upscaler: creating an FSR 4 context; this path is UNTESTED (no RDNA3/RDNA4 GPU was available)\n");
    ffxContext context = nullptr;
    const ffxReturnCode_t created = ffxApi_->createContext(&context, &create.header, nullptr);
    if (created != FFX_API_RETURN_OK || !context) { Warning("ShaderAPIDX12 upscaler: ffxCreateContext(upscale) failed (%u)\n", created); return false; }
    ffxQueryGetProviderVersion provider{}; provider.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
    if (ffxApi_->query(&context, &provider.header) == FFX_API_RETURN_OK && provider.versionName)
        Msg("ShaderAPIDX12 upscaler: FSR context provider %s\n", provider.versionName);
    // Native AA: render size == display size; the provider supplies its phase count and offsets.
    int32_t phases = 0;
    ffxQueryDescUpscaleGetJitterPhaseCount phaseQuery{}; phaseQuery.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTERPHASECOUNT;
    phaseQuery.renderWidth = desc.width; phaseQuery.displayWidth = desc.width; phaseQuery.pOutPhaseCount = &phases;
    if (ffxApi_->query(&context, &phaseQuery.header) != FFX_API_RETURN_OK || phases <= 0 || phases > 1024)
    { Warning("ShaderAPIDX12 upscaler: FSR jitter phase query failed\n"); ffxApi_->destroyContext(&context, nullptr); return false; }
    ffxJitter_.assign(static_cast<size_t>(phases) * 2, 0.f);
    for (int32_t i = 0; i < phases; ++i)
    {
        ffxQueryDescUpscaleGetJitterOffset offset{}; offset.header.type = FFX_API_QUERY_DESC_TYPE_UPSCALE_GETJITTEROFFSET;
        offset.index = i; offset.phaseCount = phases; offset.pOutX = &ffxJitter_[i * 2]; offset.pOutY = &ffxJitter_[i * 2 + 1];
        if (ffxApi_->query(&context, &offset.header) != FFX_API_RETURN_OK)
        { Warning("ShaderAPIDX12 upscaler: FSR jitter offset query failed\n"); ffxJitter_.clear(); ffxApi_->destroyContext(&context, nullptr); return false; }
    }
    if (verbose_) Msg("ShaderAPIDX12 upscaler: FSR jitter phases %d\n", phases);
    ffxContext_ = context;
    return true;
}

bool CUpscalerDX12::CreateDlss(const UpscalerFeatureDescDX12 &desc, ID3D12CommandQueue *queue)
{
    if (!ngxApi_ || !ngxApi_->dlssParams || !EnsureCompute() || !EnsureDepthClone(desc.width, desc.height)) return false;
    NVSDK_NGX_Parameter *p = ngxApi_->dlssParams;
    p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u); p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
    p->Set(NVSDK_NGX_Parameter_Width, desc.width); p->Set(NVSDK_NGX_Parameter_Height, desc.height);
    p->Set(NVSDK_NGX_Parameter_OutWidth, desc.width); p->Set(NVSDK_NGX_Parameter_OutHeight, desc.height);
    p->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<int>(NVSDK_NGX_PerfQuality_Value_DLAA));
    p->Set(NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA, static_cast<unsigned int>(NVSDK_NGX_DLSS_Hint_Render_Preset_Default));
    p->Set(NVSDK_NGX_Parameter_DLSS_Enable_Output_Subrects, 0);
    // Linear scRGB, full-resolution motion vectors without jitter, automatic exposure.
    const int flags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure | (desc.depthInverted ? NVSDK_NGX_DLSS_Feature_Flags_DepthInverted : 0);
    p->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags, flags);
    ID3D12GraphicsCommandList *list = BeginImmediate();
    if (!list) return false;
    NVSDK_NGX_Handle *handle = nullptr;
    const NVSDK_NGX_Result result = ngxApi_->create(list, NVSDK_NGX_Feature_SuperSampling, p, &handle);
    // The creation list completes before any recorded frame can evaluate the feature.
    const bool executed = EndImmediate(queue);
    if (NVSDK_NGX_FAILED(result) || !handle || !executed)
    {
        Warning("ShaderAPIDX12 upscaler: NVSDK_NGX_D3D12_CreateFeature(SuperSampling) failed (0x%08x)\n", static_cast<unsigned>(result));
        if (handle) ngxApi_->release(handle);
        return false;
    }
    dlssHandle_ = handle;
    return true;
}

bool CUpscalerDX12::ProviderJitter(uint64_t index, float &x, float &y) const
{
    if (state_ != State::Ready) return false;
    if (feature_.kind == UpscalerKindDX12::DLSS)
    {
        // DLAA converges better on a longer sequence than the backend's default eight phases.
        const auto halton = [](uint32_t i, uint32_t base) { float f = 1.f, r = 0.f; for (; i; i /= base) { f /= static_cast<float>(base); r += f * static_cast<float>(i % base); } return r; };
        const uint32_t phase = static_cast<uint32_t>(index % 32) + 1;
        x = halton(phase, 2) - .5f; y = halton(phase, 3) - .5f;
        return true;
    }
    if (feature_.kind != UpscalerKindDX12::FSR || ffxJitter_.size() < 2) return false;
    const size_t phase = static_cast<size_t>(index % (ffxJitter_.size() / 2));
    x = ffxJitter_[phase * 2]; y = ffxJitter_[phase * 2 + 1];
    return true;
}

bool CUpscalerDX12::NrReady(const DlssNrTuningDX12 &tuning, uint32_t layers, ID3D12Resource *source) const
{
    return nrChain_ && nrChain_->layers == layers && nrChain_->tuning == tuning && nrChain_->source == source;
}

uint32_t CUpscalerDX12::NrLayers() const { return nrChain_ ? nrChain_->layers : 0; }

ID3D12Resource *CUpscalerDX12::NrResult() const { return nrChain_ ? nrChain_->out[(nrChain_->layers - 1) & 1].Get() : nullptr; }

bool CUpscalerDX12::PrepareNr(const DlssNrTuningDX12 &tuning, uint32_t layers, ID3D12Resource *source, ID3D12CommandQueue *queue)
{
    if (NrReady(tuning, layers, source)) return true;
    if (nrChain_ || !source || !layers || layers > kNrMaxLayersDX12 || !InitNrSnippet() || !EnsureCompute()) return false;
    const D3D12_RESOURCE_DESC sourceDesc = source->GetDesc();
    const uint32_t width = static_cast<uint32_t>(sourceDesc.Width), height = sourceDesc.Height;
    // At 64x64 the model creates, then faults the GPU on its first evaluate (device removed). 320x180 is the smallest
    // size verified to run; every game mode is larger.
    if (width < kNrMinWidth || height < kNrMinHeight)
    {
        Warning("ShaderAPIDX12 upscaler: DLSS-NR needs at least %ux%u (scene is %ux%u)\n", kNrMinWidth, kNrMinHeight, width, height);
        return false;
    }
    if (!EnsureDepthClone(width, height)) return false;
    auto *chain = new NrChain;
    chain->tuning = tuning; chain->layers = layers; chain->width = width; chain->height = height; chain->source = source;
    // The model reads an sRGB-encoded display-referred proxy; it is stored in the scene's FP16 format as the fork does.
    chain->proxy = CreateTexture(device_, width, height, sourceDesc.Format, kNpsr);
    chain->answer = CreateTexture(device_, width, height, sourceDesc.Format, kUav);
    chain->out[0] = CreateTexture(device_, width, height, sourceDesc.Format, kUav);
    if (layers > 1) chain->out[1] = CreateTexture(device_, width, height, sourceDesc.Format, kUav);
    if (!chain->proxy || !chain->answer || !chain->out[0] || (layers > 1 && !chain->out[1]))
    { Warning("ShaderAPIDX12 upscaler: DLSS-NR chain textures %ux%u failed\n", width, height); delete chain; return false; }
    NVSDK_NGX_Parameter *p = ngxApi_->caps;
    ID3D12GraphicsCommandList *list = BeginImmediate();
    if (!list) { delete chain; return false; }
    // Tuning is read only at creation; the block outlives the features, so every value is written each time.
    int result = NVSDK_NGX_Result_Success;
    for (uint32_t layer = 0; layer < layers && NVSDK_NGX_SUCCEED(static_cast<NVSDK_NGX_Result>(result)); ++layer)
    {
        p->Set("DLSSNR.Enabled", 1u); p->Set("DLSSNR.Width", width); p->Set("DLSSNR.Height", height);
        p->Set(NVSDK_NGX_Parameter_CreationNodeMask, 1u); p->Set(NVSDK_NGX_Parameter_VisibilityNodeMask, 1u);
        p->Set("DLSSNR.Hint.Render.Preset", tuning.preset);
        p->Set("DLSSNR.Intensity", tuning.intensity); p->Set("DLSSNR.Style", tuning.style);
        p->Set("DLSSNR.LocalStructureStrength", tuning.localStructure); p->Set("DLSSNR.LocalToneStrength", tuning.localTone);
        p->Set("DLSSNR.SkinStructureStrength", tuning.skinStructure);
        p->Set("DLSSNR.UseAutoMask", tuning.autoMask ? 1u : 0u); p->Set("DLSSNR.UICorrection", tuning.uiCorrection ? 1u : 0u);
        void *handle = nullptr;
        result = nrApi_->create(list, 18, p, &handle);
        if (NVSDK_NGX_SUCCEED(static_cast<NVSDK_NGX_Result>(result)) && !handle) result = NVSDK_NGX_Result_Fail;
        chain->handles[layer] = handle;
    }
    // Creation completes on its own list before any recorded frame evaluates a layer.
    const bool executed = EndImmediate(queue);
    if (NVSDK_NGX_FAILED(static_cast<NVSDK_NGX_Result>(result)) || !executed)
    {
        Warning("ShaderAPIDX12 upscaler: DLSS-NR feature creation failed (0x%08x)\n", static_cast<unsigned>(result));
        for (void *handle : chain->handles) if (handle) nrApi_->release(handle);
        delete chain;
        return false;
    }
    nrChain_ = chain;
    Msg("ShaderAPIDX12 upscaler: DLSS-NR %u layer%s ready at %ux%u (preset %u, style %u, intensity %.2f)\n", layers, layers == 1 ? "" : "s", width, height, tuning.preset, tuning.style, tuning.intensity);
    return true;
}

void CUpscalerDX12::ReleaseNr()
{
    if (!nrChain_) return;
    if (nrApi_) for (void *handle : nrChain_->handles) if (handle) nrApi_->release(handle);
    delete nrChain_; nrChain_ = nullptr;
}

void CUpscalerDX12::WriteDescriptors(const UpscalerDispatchDX12 &d) const
{
    ID3D12Device *device = device_;
    const auto slot = [&](uint32_t pass, uint32_t index) { D3D12_CPU_DESCRIPTOR_HANDLE h = d.cpu; h.ptr += static_cast<SIZE_T>(pass * kPassDescriptors + index) * descriptorStride_; return h; };
    const auto srv = [&](uint32_t pass, uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format) {
        D3D12_SHADER_RESOURCE_VIEW_DESC v{}; v.Format = format; v.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        v.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; v.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(resource, &v, slot(pass, index));
    };
    const auto uav = [&](uint32_t pass, uint32_t index, ID3D12Resource *resource, DXGI_FORMAT format) {
        D3D12_UNORDERED_ACCESS_VIEW_DESC v{}; v.Format = format; v.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        device->CreateUnorderedAccessView(resource, nullptr, &v, slot(pass, index));
    };
    const DXGI_FORMAT color = d.output->GetDesc().Format;
    const auto pass = [&](uint32_t index, ID3D12Resource *t0, DXGI_FORMAT f0, ID3D12Resource *t1, ID3D12Resource *t2, ID3D12Resource *u0, ID3D12Resource *u1) {
        srv(index, 0, t0, f0); srv(index, 1, t1, color); srv(index, 2, t2, color); srv(index, 3, nullptr, color);
        uav(index, 4, u0, color); uav(index, 5, u1, DXGI_FORMAT_R32_FLOAT);
    };
    // Pass 0: scene depth -> typed R32F clone.
    pass(0, NeedsDepthClone(d) ? d.depth : nullptr, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, nullptr, nullptr, nullptr, NeedsDepthClone(d) ? depthClone_.Get() : nullptr);
    for (uint32_t layer = 0; layer < d.nrLayers; ++layer)
    {
        ID3D12Resource *input = layer ? nrChain_->out[(layer - 1) & 1].Get() : d.output;
        ID3D12Resource *output = nrChain_->out[layer & 1].Get();
        pass(1 + layer * 2, input, color, nullptr, nullptr, nrChain_->proxy.Get(), nullptr);                                   // encode
        pass(2 + layer * 2, nrChain_->proxy.Get(), color, nrChain_->answer.Get(), input, output, nullptr);                     // resolve
    }
}

bool CUpscalerDX12::RecordDispatch(CCommandRecorderDX12 &recorder, const UpscalerDispatchDX12 &dispatch)
{
    if (state_ != State::Ready || !dispatch.result || !dispatch.color || !dispatch.motion || !dispatch.output) return false;
    if (dispatch.nrLayers && (!nrChain_ || nrChain_->layers != dispatch.nrLayers || nrChain_->source != dispatch.output)) return false;
    if (NeedsDescriptors(dispatch))
    {
        if (!dispatch.heap || !dispatch.cpu.ptr || !computeRoot_ || (NeedsDepthClone(dispatch) && (!dispatch.depth || !depthClone_))) return false;
        WriteDescriptors(dispatch);
    }
    const UpscalerReplayPayloadDX12 payload{this, dispatch};
    recorder.ExternalCommand(&CUpscalerDX12::ReplayThunk, payload);
    return true;
}

void CUpscalerDX12::BindCompute(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &d, ID3D12PipelineState *pso, uint32_t pass, const NrConstantsDX12 &constants) const
{
    list->SetComputeRootSignature(computeRoot_.Get());
    ID3D12DescriptorHeap *heap = d.heap;
    list->SetDescriptorHeaps(1, &heap);
    list->SetPipelineState(pso);
    D3D12_GPU_DESCRIPTOR_HANDLE srvs = d.gpu; srvs.ptr += static_cast<UINT64>(pass * kPassDescriptors) * descriptorStride_;
    D3D12_GPU_DESCRIPTOR_HANDLE uavs = srvs; uavs.ptr += static_cast<UINT64>(4) * descriptorStride_;
    list->SetComputeRoot32BitConstants(0, sizeof(constants) / 4, &constants, 0);
    list->SetComputeRootDescriptorTable(1, srvs);
    list->SetComputeRootDescriptorTable(2, uavs);
    list->Dispatch((d.width + 7) / 8, (d.height + 7) / 8, 1);
}

void CUpscalerDX12::ReplayThunk(ID3D12GraphicsCommandList *list, ID3D12Device *, const void *bytes) noexcept
{
    UpscalerReplayPayloadDX12 payload;
    std::memcpy(&payload, bytes, sizeof(payload));
    const UpscalerDispatchDX12 &d = payload.dispatch;
    const CUpscalerDX12 &owner = *payload.owner;
    // XeSS with high-resolution motion vectors takes no depth; FSR reads it through its R24 SRV mapping; DLSS and
    // DLSS-NR read the typed clone written from it here.
    const bool clone = owner.NeedsDepthClone(d);
    const bool depthInput = owner.feature_.kind == UpscalerKindDX12::FSR || clone;
    const D3D12_RESOURCE_STATES depthDuring = depthInput ? kRead : D3D12_RESOURCE_STATE_DEPTH_WRITE;
    D3D12_RESOURCE_BARRIER barriers[8]; UINT count = 0;
    AddTransition(d.color, d.colorBefore, kRead, barriers, count);
    AddTransition(d.depth, d.depthBefore, depthDuring, barriers, count);
    AddTransition(d.motion, d.motionBefore, kRead, barriers, count);
    AddTransition(d.output, d.outputBefore, kUav, barriers, count);
    AddTransition(clone ? owner.depthClone_.Get() : nullptr, kNpsr, kUav, barriers, count);
    if (count) list->ResourceBarrier(count, barriers);
    if (clone)
    {
        NrConstantsDX12 constants{}; constants.width = d.width; constants.height = d.height;
        owner.BindCompute(list, d, owner.depthPso_.Get(), 0, constants);
        Transition(list, owner.depthClone_.Get(), kUav, kNpsr);
    }
    const uint32_t code = d.forceFailure ? kReplayForced : owner.Execute(list, d);
    uint32_t nrCode = 0;
    ID3D12Resource *result = d.output;
    if (!code && d.nrLayers)
    {
        nrCode = owner.ExecuteNr(list, d);
        if (!nrCode) result = owner.NrResult();
    }
    count = 0;
    if (!code)
    {
        // The temporal-AA output, or the last DLSS-NR layer when the chain ran; both rest in UNORDERED_ACCESS.
        AddTransition(result, kUav, D3D12_RESOURCE_STATE_COPY_SOURCE, barriers, count);
        AddTransition(d.color, kRead, D3D12_RESOURCE_STATE_COPY_DEST, barriers, count);
        list->ResourceBarrier(count, barriers); count = 0;
        list->CopyResource(d.color, result);
        AddTransition(result, D3D12_RESOURCE_STATE_COPY_SOURCE, kUav, barriers, count);
        AddTransition(d.color, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, count);
    }
    else AddTransition(d.color, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, count);
    AddTransition(d.depth, depthDuring, D3D12_RESOURCE_STATE_DEPTH_WRITE, barriers, count);
    AddTransition(d.motion, kRead, D3D12_RESOURCE_STATE_RENDER_TARGET, barriers, count);
    if (count) list->ResourceBarrier(count, barriers);
    // The owner reads `serial` with acquire before `code`/`nrCode`/`frame`.
    d.result->code.store(code, std::memory_order_relaxed);
    d.result->nrCode.store(nrCode, std::memory_order_relaxed);
    d.result->frame.store(d.frame, std::memory_order_relaxed);
    d.result->serial.store(d.serial, std::memory_order_release);
}

uint32_t CUpscalerDX12::ExecuteNr(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &d) const noexcept
{
    if (!nrChain_ || !nrApi_ || !ngxApi_ || !ngxApi_->caps) return kReplayNoContext;
    NVSDK_NGX_Parameter *p = ngxApi_->caps;
    const NrChain &chain = *nrChain_;
    // Layer N reads layer N-1's output (layer 0 reads the temporal-AA output). Every resource returns to its resting
    // state -- proxy NON_PIXEL_SHADER_RESOURCE, the rest UNORDERED_ACCESS -- before an evaluate failure returns.
    for (uint32_t layer = 0; layer < chain.layers; ++layer)
    {
        ID3D12Resource *input = layer ? chain.out[(layer - 1) & 1].Get() : d.output;
        NrConstantsDX12 constants{};
        constants.width = d.width; constants.height = d.height; constants.whitePoint = d.nr.whitePoint;
        constants.transferStrength = d.nr.transferStrength; constants.colourStrength = d.nr.colourStrength; constants.maxRatio = d.nr.maxRatio;
        constants.reversibleMode = d.nr.reversibleMode; constants.debugView = d.nr.debugView; constants.applyModel = d.nr.applyModel ? 1u : 0u;
        constants.compareMode = d.nr.compareMode; constants.compareSplit = d.nr.compareSplit; constants.compareZoom = d.nr.compareZoom; constants.compareSwap = d.nr.compareSwap ? 1u : 0u;
        // Encode: input -> proxy.
        D3D12_RESOURCE_BARRIER barriers[3]; UINT count = 0;
        AddTransition(input, kUav, kNpsr, barriers, count);
        AddTransition(chain.proxy.Get(), kNpsr, kUav, barriers, count);
        list->ResourceBarrier(count, barriers);
        constants.mode = 0;
        BindCompute(list, d, nrPso_.Get(), 1 + layer * 2, constants);
        Transition(list, chain.proxy.Get(), kUav, kNpsr);
        // The model: proxy + depth clone + motion -> answer.
        p->Set("DLSSNR.Color", chain.proxy.Get()); p->Set("DLSSNR.Depth", depthClone_.Get());
        p->Set("DLSSNR.MVec", d.motion); p->Set("DLSSNR.Output", chain.answer.Get());
        p->Set("DLSSNR.Enabled", 1u); p->Set("DLSSNR.Width", d.width); p->Set("DLSSNR.Height", d.height);
        p->Set("DLSSNR.DepthInverted", feature_.depthInverted ? 1u : 0u); p->Set("DLSSNR.Reset", d.nrReset ? 1u : 0u);
        for (const char *prefix : {"DLSSNR.Color", "DLSSNR.Output", "DLSSNR.Depth", "DLSSNR.MVec"})
        {
            char name[64];
            snprintf(name, sizeof(name), "%sSubrectBaseX", prefix); p->Set(name, 0u);
            snprintf(name, sizeof(name), "%sSubrectBaseY", prefix); p->Set(name, 0u);
            snprintf(name, sizeof(name), "%sSubrectWidth", prefix); p->Set(name, d.width);
            snprintf(name, sizeof(name), "%sSubrectHeight", prefix); p->Set(name, d.height);
        }
        p->Set("DLSSNR.MVecScaleX", d.motionScale[0]); p->Set("DLSSNR.MVecScaleY", d.motionScale[1]);
        const DlssNrTuningDX12 &t = chain.tuning;
        p->Set("DLSSNR.Intensity", t.intensity); p->Set("DLSSNR.Style", t.style);
        p->Set("DLSSNR.LocalStructureStrength", t.localStructure); p->Set("DLSSNR.LocalToneStrength", t.localTone);
        p->Set("DLSSNR.SkinStructureStrength", t.skinStructure); p->Set("DLSSNR.UseAutoMask", t.autoMask ? 1u : 0u);
        const int result = nrApi_->evaluate(list, chain.handles[layer], p);
        if (NVSDK_NGX_FAILED(static_cast<NVSDK_NGX_Result>(result)))
        {
            Transition(list, input, kNpsr, kUav);
            return 0x40000u | (static_cast<uint32_t>(result) & 0xffffu);
        }
        // Resolve: proxy + answer + input -> output.
        Transition(list, chain.answer.Get(), kUav, kNpsr);
        constants.mode = 1;
        BindCompute(list, d, nrPso_.Get(), 2 + layer * 2, constants);
        count = 0;
        AddTransition(chain.answer.Get(), kNpsr, kUav, barriers, count);
        AddTransition(input, kNpsr, kUav, barriers, count);
        list->ResourceBarrier(count, barriers);
    }
    return 0;
}

uint32_t CUpscalerDX12::Execute(ID3D12GraphicsCommandList *list, const UpscalerDispatchDX12 &d) const noexcept
{
    switch (feature_.kind)
    {
    case UpscalerKindDX12::XeSS:
    {
        if (!xessContext_) return kReplayNoContext;
        xess_d3d12_execute_params_t params{};
        params.pColorTexture = d.color; params.pVelocityTexture = d.motion; params.pDepthTexture = nullptr; params.pOutputTexture = d.output;
        params.jitterOffsetX = d.jitter[0]; params.jitterOffsetY = d.jitter[1]; params.exposureScale = 1.f;
        params.resetHistory = d.reset ? 1u : 0u; params.inputWidth = d.width; params.inputHeight = d.height;
        const xess_result_t result = xessApi_->execute(static_cast<xess_context_handle_t>(xessContext_), list, &params);
        return result == XESS_RESULT_SUCCESS ? 0u : 0x20000u | (static_cast<uint32_t>(-static_cast<int>(result)) & 0xffffu);
    }
    case UpscalerKindDX12::FSR:
    {
        if (!ffxContext_) return kReplayNoContext;
        ffxDispatchDescUpscale dispatch{}; dispatch.header.type = FFX_API_DISPATCH_DESC_TYPE_UPSCALE;
        dispatch.commandList = list;
        dispatch.color = ffxApiGetResourceDX12(d.color, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatch.depth = ffxApiGetResourceDX12(d.depth, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatch.motionVectors = ffxApiGetResourceDX12(d.motion, FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ);
        dispatch.output = ffxApiGetResourceDX12(d.output, FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);
        dispatch.jitterOffset = {d.jitter[0], d.jitter[1]};
        dispatch.motionVectorScale = {d.motionScale[0], d.motionScale[1]};
        dispatch.renderSize = {d.width, d.height}; dispatch.upscaleSize = {d.width, d.height};
        dispatch.enableSharpening = false; dispatch.sharpness = 0.f;
        dispatch.frameTimeDelta = d.frameTimeMs; dispatch.preExposure = 1.f; dispatch.reset = d.reset;
        dispatch.cameraNear = d.cameraNear; dispatch.cameraFar = d.cameraFar; dispatch.cameraFovAngleVertical = d.fovY;
        dispatch.viewSpaceToMetersFactor = 0.0254f; // Source units are inches
        ffxContext context = ffxContext_;
        const ffxReturnCode_t result = ffxApi_->dispatch(&context, &dispatch.header);
        return result == FFX_API_RETURN_OK ? 0u : 0x30000u | (result & 0xffffu);
    }
    case UpscalerKindDX12::DLSS:
    {
        if (!dlssHandle_ || !ngxApi_ || !ngxApi_->dlssParams) return kReplayNoContext;
        NVSDK_NGX_Parameter *p = ngxApi_->dlssParams;
        p->Set(NVSDK_NGX_Parameter_Color, d.color); p->Set(NVSDK_NGX_Parameter_Output, d.output);
        p->Set(NVSDK_NGX_Parameter_Depth, depthClone_.Get()); p->Set(NVSDK_NGX_Parameter_MotionVectors, d.motion);
        p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, d.jitter[0]); p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, d.jitter[1]);
        p->Set(NVSDK_NGX_Parameter_MV_Scale_X, d.motionScale[0]); p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, d.motionScale[1]);
        p->Set(NVSDK_NGX_Parameter_Reset, d.reset ? 1 : 0);
        p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Width, d.width); p->Set(NVSDK_NGX_Parameter_DLSS_Render_Subrect_Dimensions_Height, d.height);
        p->Set(NVSDK_NGX_Parameter_Sharpness, 0.f);
        p->Set(NVSDK_NGX_Parameter_DLSS_Pre_Exposure, 1.f); p->Set(NVSDK_NGX_Parameter_DLSS_Exposure_Scale, 1.f);
        p->Set(NVSDK_NGX_Parameter_FrameTimeDeltaInMsec, d.frameTimeMs);
        const NVSDK_NGX_Result result = ngxApi_->evaluate(list, static_cast<const NVSDK_NGX_Handle *>(dlssHandle_), p, nullptr);
        return NVSDK_NGX_SUCCEED(result) ? 0u : 0x50000u | (static_cast<uint32_t>(result) & 0xffffu);
    }
    default:
        return kReplayNoContext;
    }
}

void CUpscalerDX12::ReleaseFeature()
{
    ReleaseNr();
    if (xessContext_ && xessApi_) xessApi_->destroyContext(static_cast<xess_context_handle_t>(xessContext_));
    if (ffxContext_ && ffxApi_) { ffxContext context = ffxContext_; ffxApi_->destroyContext(&context, nullptr); }
    if (dlssHandle_ && ngxApi_) ngxApi_->release(static_cast<NVSDK_NGX_Handle *>(dlssHandle_));
    xessContext_ = nullptr; ffxContext_ = nullptr; dlssHandle_ = nullptr; ffxJitter_.clear();
    state_ = State::Disabled; feature_ = {};
}

void CUpscalerDX12::Shutdown()
{
    ReleaseFeature();
    // Order: features -> DLSS-NR snippet -> NGX parameter blocks -> NGX core -> modules.
    if (nrApi_)
    {
        if (nrApi_->initialized && device_) nrApi_->shutdown(device_);
        nrApi_->unload();
        delete nrApi_; nrApi_ = nullptr;
    }
    if (ngxApi_)
    {
        if (ngxApi_->dlssParams) ngxApi_->destroy(ngxApi_->dlssParams);
        if (ngxApi_->caps) ngxApi_->destroy(ngxApi_->caps);
        if (ngxApi_->initialized && device_) ngxApi_->shutdown(device_);
    }
    delete ngxApi_; ngxApi_ = nullptr;
    delete xessApi_; xessApi_ = nullptr;
    if (ffxApi_ && ffxApi_->upscaler) FreeLibrary(ffxApi_->upscaler);
    delete ffxApi_; ffxApi_ = nullptr;
    for (Provider *p : {&nr_, &dlss_, &fsr_, &xess_, &ngxCore_})
    {
        if (p->module) FreeLibrary(p->module);
        *p = Provider{};
    }
    depthClone_.Reset(); immediateList_.Reset(); immediateAllocator_.Reset(); immediateFence_.Reset(); immediateValue_ = 0;
    nrPso_.Reset(); depthPso_.Reset(); computeRoot_.Reset();
    device_ = nullptr; vendor_ = deviceId_ = 0; dir_.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// CShaderAPIDX12 integration: mode selection, jitter, dispatch and lifetime. All of it runs on the recording owner.
// ---------------------------------------------------------------------------------------------------------------
static ConVar upscaler_fault_replays("upscaler_fault_replays", "0", FCVAR_CHEAT,
    "Test hook: the next N recorded upscaler dispatches skip the provider and report a replay failure");

namespace
{
float Halton(uint32_t index, uint32_t base)
{
    float fraction = 1.f, result = 0.f;
    for (; index; index /= base) { fraction /= static_cast<float>(base); result += fraction * static_cast<float>(index % base); }
    return result;
}

// View-space distances where the projection maps depth to 0 and 1 (column-vector VMatrix, w row = row 3).
bool ProjectionDepthRange(const VMatrix &p, float &nearPlane, float &farPlane, bool &inverted, float &fovY)
{
    if (p[3][3] != 0.f || p[3][2] == 0.f || p[2][2] == 0.f || p[2][2] == p[3][2] || p[1][1] == 0.f) return false;
    const float atZero = std::fabs(-p[2][3] / p[2][2]), atOne = std::fabs((p[3][3] - p[2][3]) / (p[2][2] - p[3][2]));
    inverted = atZero > atOne;
    nearPlane = inverted ? atOne : atZero; farPlane = inverted ? atZero : atOne;
    fovY = 2.f * std::atan(1.f / std::fabs(p[1][1]));
    return std::isfinite(nearPlane) && std::isfinite(farPlane) && nearPlane > 0.f && farPlane > nearPlane;
}

uint32_t Field(int config, int shift, int bits) { return (static_cast<uint32_t>(config) >> shift) & ((1u << bits) - 1u); }
}

void CShaderAPIDX12::ConsumeUpscalerReplays(bool wait)
{
    while (upscalerConsumedSerial_ < upscalerPendingSerial_)
    {
        const uint64_t serial = upscalerConsumedSerial_ + 1;
        UpscalerReplayResultDX12 &slot = upscalerReplay_[serial % kUpscalerReplaySlots];
        if (slot.serial.load(std::memory_order_acquire) != serial)
        {
            if (!wait) return;
            // The dispatch chunk was flushed when recorded, so its replay is queued ahead of anything still pending.
            if (device_ && device_->WaitForSubmissionProgress()) continue;
            // Queue empty: the callback has either run (serial now visible through the tail's release) or never will.
            if (slot.serial.load(std::memory_order_acquire) != serial)
            {
                upscalerConsumedSerial_ = serial; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
                renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -6;
                Warning("ShaderAPIDX12 upscaler: dispatch %llu never replayed; history reset\n", static_cast<unsigned long long>(serial));
                continue;
            }
        }
        upscalerConsumedSerial_ = serial;
        const uint32_t code = slot.code.load(std::memory_order_relaxed), nrCode = slot.nrCode.load(std::memory_order_relaxed);
        const uint64_t frame = slot.frame.load(std::memory_order_relaxed);
        if (nrCode)
        {
            upscalerNrHistoryGap_ = true;
            renderingInts_[INT_RENDERPARM_DX12_NR_STATUS] = -6;
            Warning("ShaderAPIDX12 upscaler: DLSS-NR replay failed for frame %llu (code 0x%x); the temporal-AA result was shown\n", static_cast<unsigned long long>(frame), nrCode);
        }
        if (!code) { upscalerLastSuccessFrame_ = frame; continue; }
        upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
        renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -6;
        Warning("ShaderAPIDX12 upscaler: %s replay failed for frame %llu (code 0x%x); history reset\n", UpscalerKindNameDX12(upscalerKind_), static_cast<unsigned long long>(frame), code);
    }
}

void CShaderAPIDX12::SampleUpscalerJitter()
{
    upscalerJitter_[0] = upscalerJitter_[1] = 0.f;
    // The first frame after enable/reset renders unjittered; jitter starts only once the previous frame's
    // dispatch replayed successfully, so no unfiltered jittered frame reaches the screen.
    if (!upscalerViewEligible_ || upscalerHistoryGap_ || upscalerKind_ == UpscalerKindDX12::None || !device_ || device_->SceneSampleCount() != 1 ||
        upscalerQueuedFrame_ == frameCounter_ || upscalerLastDispatchFrame_ == ~0ull || upscalerLastSuccessFrame_ != upscalerLastDispatchFrame_)
        return;
    const uint64_t index = upscalerJitterIndex_++;
    float x = 0.f, y = 0.f;
    if (!upscaler_.ProviderJitter(index, x, y))
    {
        const uint32_t phase = static_cast<uint32_t>(index % 8) + 1; // Halton(2,3), eight phases
        x = Halton(phase, 2) - .5f; y = Halton(phase, 3) - .5f;
    }
    upscalerJitter_[0] = std::clamp(x, -.5f, .5f); upscalerJitter_[1] = std::clamp(y, -.5f, .5f);
}

bool CShaderAPIDX12::WaitUpscalerGpuIdle()
{
    if (!device_) return true;
    if (device_->IsRecordingOwner()) return device_->SubmitAndWaitForGpu();
    // Teardown off the owner: every dispatch chunk was flushed when recorded; wait for its replay and the last submission.
    device_->DrainSubmissions();
    return device_->WaitForFence(device_->NextFenceValue() - 1);
}

void CShaderAPIDX12::ReleaseUpscalerFeature()
{
    // GPU-idle boundary on the recording owner: every recorded callback naming the feature has replayed and its GPU
    // work has completed before the provider context is destroyed.
    if (!device_ || !device_->IsRecordingOwner() || !WaitUpscalerGpuIdle()) return;
    pipeline_.InvalidateGraphicsBindings();
    ConsumeUpscalerReplays(false);
    upscaler_.ReleaseFeature();
    upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
}

void CShaderAPIDX12::ReleaseUpscalerNr()
{
    if (!upscaler_.NrLayers() || !device_ || !device_->IsRecordingOwner() || !WaitUpscalerGpuIdle()) return;
    pipeline_.InvalidateGraphicsBindings();
    ConsumeUpscalerReplays(false);
    upscaler_.ReleaseNr();
    upscalerNrHistoryGap_ = true;
}

uint32_t CShaderAPIDX12::PrepareUpscalerNr(DlssNrComposeDX12 &compose)
{
    const int config = renderingInts_[INT_RENDERPARM_DX12_NR_CONFIG];
    int &status = renderingInts_[INT_RENDERPARM_DX12_NR_STATUS];
    const uint32_t layers = std::min<uint32_t>(Field(config, DX12_NR_CONFIG_LAYERS_SHIFT, 4), DX12_NR_MAX_LAYERS);
    if (!layers)
    {
        if (upscaler_.NrLayers()) ReleaseUpscalerNr();
        upscalerNrFailed_ = false; status = 0;
        return 0;
    }
    if (!upscaler_.NrAvailable())
    {
        if (!upscalerNrUnavailableLogged_) Warning("ShaderAPIDX12 upscaler: DLSS-NR requested but unavailable: %s\n", upscaler_.NrReason().c_str());
        upscalerNrUnavailableLogged_ = true; status = -1;
        return 0;
    }
    const float *f = renderingFloats_.data();
    const auto clampf = [](float v, float lo, float hi) { return std::isfinite(v) ? std::clamp(v, lo, hi) : lo; };
    DlssNrTuningDX12 tuning;
    tuning.preset = std::min(Field(config, DX12_NR_CONFIG_PRESET_SHIFT, 4), 3u);
    tuning.style = std::min(Field(config, DX12_NR_CONFIG_STYLE_SHIFT, 2), 2u);
    tuning.intensity = clampf(f[FLOAT_RENDERPARM_DX12_NR_INTENSITY], 0.f, 2.f);
    tuning.localStructure = clampf(f[FLOAT_RENDERPARM_DX12_NR_LOCAL_STRUCTURE], 0.f, 2.f);
    tuning.localTone = clampf(f[FLOAT_RENDERPARM_DX12_NR_LOCAL_TONE], 0.f, 2.f);
    tuning.skinStructure = clampf(f[FLOAT_RENDERPARM_DX12_NR_SKIN_STRUCTURE], -1.f, 2.f);
    tuning.autoMask = (config & DX12_NR_CONFIG_AUTO_MASK) != 0;
    tuning.uiCorrection = (config & DX12_NR_CONFIG_UI_CORRECTION) != 0;
    ID3D12Resource *source = upscalerOutput_.Get();
    if (!upscaler_.NrReady(tuning, layers, source))
    {
        // A failed configuration is not retried every frame; any change to it is.
        if (upscalerNrFailed_ && upscalerNrFailedTuning_ == tuning && upscalerNrFailedLayers_ == layers && upscalerNrFailedSource_ == source) { status = -2; return 0; }
        ReleaseUpscalerNr();
        if (!device_->CommandList() || !upscaler_.PrepareNr(tuning, layers, source, device_->Queue()))
        {
            upscalerNrFailed_ = true; upscalerNrFailedTuning_ = tuning; upscalerNrFailedLayers_ = layers; upscalerNrFailedSource_ = source;
            status = -2;
            return 0;
        }
        upscalerNrFailed_ = false; upscalerNrHistoryGap_ = true;
    }
    compose.whitePoint = clampf(f[FLOAT_RENDERPARM_DX12_NR_WHITE_POINT], .01f, 64.f);
    compose.transferStrength = clampf(f[FLOAT_RENDERPARM_DX12_NR_TRANSFER_STRENGTH], 0.f, 2.f);
    compose.colourStrength = clampf(f[FLOAT_RENDERPARM_DX12_NR_COLOUR_STRENGTH], 0.f, 4.f);
    compose.maxRatio = clampf(f[FLOAT_RENDERPARM_DX12_NR_MAX_RATIO], 1.f, 8.f);
    compose.compareSplit = clampf(f[FLOAT_RENDERPARM_DX12_NR_COMPARE_SPLIT], 0.f, 1.f);
    compose.compareZoom = clampf(f[FLOAT_RENDERPARM_DX12_NR_COMPARE_ZOOM], 1.f, 2.f);
    compose.reversibleMode = std::min(Field(config, DX12_NR_CONFIG_REVERSIBLE_SHIFT, 3), 4u);
    compose.debugView = Field(config, DX12_NR_CONFIG_DEBUG_VIEW_SHIFT, 2);
    compose.compareMode = std::min(Field(config, DX12_NR_CONFIG_COMPARE_SHIFT, 2), 2u);
    compose.applyModel = (config & DX12_NR_CONFIG_APPLY_MODEL) != 0;
    compose.compareSwap = (config & DX12_NR_CONFIG_COMPARE_SWAP) != 0;
    return layers;
}

bool CShaderAPIDX12::EnsureUpscalerOutput(UINT width, UINT height)
{
    if (upscalerOutput_)
    {
        const D3D12_RESOURCE_DESC desc = upscalerOutput_->GetDesc();
        if (desc.Width == width && desc.Height == height) return true;
        // Only reached on resize: the feature naming the old output is released behind a GPU-idle boundary first.
        ReleaseUpscalerFeature();
        upscalerOutput_.Reset();
    }
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.Format = device_->SceneColorFormat(); desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    const HRESULT hr = device_->NativeDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(&upscalerOutput_));
    if (FAILED(hr)) { Warning("ShaderAPIDX12 upscaler: output %ux%u creation failed (0x%08x)\n", width, height, static_cast<unsigned>(hr)); upscalerOutput_.Reset(); return false; }
    upscalerOutputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
    return true;
}

void CShaderAPIDX12::SetUpscalerMode(int mode)
{
    upscalerViewEligible_ = false; upscalerJitter_[0] = upscalerJitter_[1] = 0.f;
    if (!device_ || !device_->IsRecordingOwner() || !device_->CommandList()) return;
    if (mode < 0 || mode > 4) mode = 0;
    // Jitter sampling needs the previous dispatch's result; mode 0 only disarms and never waits on the worker.
    ConsumeUpscalerReplays(mode != 0);
    upscalerMode_ = mode;
    upscalerFrameToken_ = frameCounter_;
    // Mode 0 disarms: ineligible views (monitors, intro, loading, model panels) submit it every frame, before or after
    // the main view. The feature is released by BeginFrame once a whole frame passes without a nonzero mode, and a
    // later ineligible view keeps the status the main view published this frame.
    if (!mode)
    {
        if (upscalerEnabledFrame_ != frameCounter_) renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS] = renderingInts_[INT_RENDERPARM_DX12_NR_STATUS] = 0;
        return;
    }
    upscalerEnabledFrame_ = frameCounter_;
    if (mode != upscalerSelectedMode_) { upscalerSelectedMode_ = mode; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true; }
    if (!upscalerInitialized_)
    {
        upscalerInitialized_ = true;
        MaterialAdapterInfo_t adapter{};
        if (g_pShaderDeviceMgrDX12) g_pShaderDeviceMgrDX12->GetAdapterInfo(device_->GetCurrentAdapter(), adapter);
        wchar_t path[MAX_PATH]{}; HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&UpscalerKindNameDX12), &self);
        if (self && GetModuleFileNameW(self, path, MAX_PATH)) { if (wchar_t *slash = wcsrchr(path, L'\\')) slash[1] = 0; }
        upscaler_.Initialize(device_->NativeDevice(), adapter, path, CommandLine() && CommandLine()->CheckParm("-dx12upscalerlog"));
    }
    const UpscalerKindDX12 kind = upscaler_.Resolve(mode);
    if (kind != upscalerKind_)
    {
        if (upscaler_.Feature().kind != UpscalerKindDX12::None) ReleaseUpscalerFeature();
        upscalerKind_ = kind; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
        if (kind != UpscalerKindDX12::None) Msg("ShaderAPIDX12 upscaler: mode %d selects %s\n", mode, UpscalerKindNameDX12(kind));
        else Warning("ShaderAPIDX12 upscaler: mode %d has no available provider on this adapter\n", mode);
    }
    // The eligible main view was begun this frame; SampleUpscalerJitter and DispatchUpscaler handle a missing provider.
    upscalerViewEligible_ = true;
    if (kind == UpscalerKindDX12::None) { renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS] = -1; return; }
    // A frame of the eligible view without a dispatch breaks the history.
    if (upscalerLastDispatchFrame_ == ~0ull || upscalerLastDispatchFrame_ + 1 < frameCounter_) upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
    SampleUpscalerJitter();
}

void CShaderAPIDX12::DispatchUpscaler(int flags)
{
    if (!(flags & DX12_UPSCALE_DISPATCH_RUN) || !upscalerMode_) return;
    int &status = renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS];
    int &nrStatus = renderingInts_[INT_RENDERPARM_DX12_NR_STATUS];
    // Every rejection records no command bytes and breaks the history; requested DLSS-NR layers report -3.
    const auto reject = [&](int code, const char *why) {
        if (status != code) Warning("ShaderAPIDX12 upscaler: dispatch rejected (%d): %s\n", code, why);
        status = code; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true; upscalerJitter_[0] = upscalerJitter_[1] = 0.f;
        nrStatus = Field(renderingInts_[INT_RENDERPARM_DX12_NR_CONFIG], DX12_NR_CONFIG_LAYERS_SHIFT, 4) ? -3 : 0;
    };
    if (!upscalerViewEligible_ || upscalerFrameToken_ != frameCounter_) return reject(-4, "no eligible main view was begun this frame");
    if (!device_ || device_->SceneSampleCount() > 1) return reject(-3, "MSAA is active");
    if (!device_->IsRecordingOwner() || !device_->CommandList() || renderTargets_[0] != SHADER_RENDERTARGET_BACKBUFFER || !device_->SceneColor() || !device_->SceneDepth())
        return reject(-4, "render target 0 is not the scene or the caller does not own recording");
    const UINT width = static_cast<UINT>(device_->SceneWidth()), height = static_cast<UINT>(device_->SceneHeight());
    TextureRecord *motion = motionResolvedFrame_ == frameCounter_ ? FindTexture(motionResolvedHandle_) : nullptr;
    if (!motion || !(motion->flags & TEXTURE_CREATE_RENDERTARGET) || motion->format != IMAGE_FORMAT_RGBA16161616F || motion->width != static_cast<int>(width) ||
        motion->height != static_cast<int>(height) || !EnsureTextureResident(*motion) || !motion->resource)
        return reject(-5, "no scene-sized motion-vector resolve this frame");
    if (upscalerKind_ == UpscalerKindDX12::None) return reject(-1, "no provider for the selected mode");
    if (upscalerQueuedFrame_ == frameCounter_) return; // one dispatch per frame
    float derivedNear = 0.f, derivedFar = 0.f, derivedFov = 0.f; bool inverted = false;
    ProjectionDepthRange(matrices_[MATERIAL_PROJECTION], derivedNear, derivedFar, inverted, derivedFov);
    const UpscalerFeatureDescDX12 desc{upscalerKind_, width, height, inverted};
    if (upscaler_.Feature().kind != UpscalerKindDX12::None && upscaler_.Feature() != desc) ReleaseUpscalerFeature();
    if (!EnsureUpscalerOutput(width, height)) return reject(-2, "output allocation failed");
    if (!upscaler_.PrepareFeature(desc, device_->Queue())) return reject(-2, "provider feature creation failed");
    DlssNrComposeDX12 compose;
    const uint32_t nrLayers = PrepareUpscalerNr(compose);
    FlushBufferedPrimitives(); CommitTransforms();
    auto *list = device_->CommandList();
    if (!list) return reject(-4, "recording ended during feature preparation");
    while (upscalerPendingSerial_ + 1 - upscalerConsumedSerial_ > kUpscalerReplaySlots) ConsumeUpscalerReplays(true);
    const int faces = (motion->flags & TEXTURE_CREATE_CUBEMAP) ? 6 : 1, sub = motion->currentCopy * faces * motion->mipLevels;
    const float *camera = renderingFloats_.data();
    UpscalerDispatchDX12 d;
    d.color = device_->SceneColor(); d.colorBefore = device_->SceneColorState();
    d.depth = device_->SceneDepth(); d.depthBefore = device_->SceneDepthState();
    d.motion = motion->resource.Get(); d.motionBefore = motion->subresourceStates[sub];
    d.output = upscalerOutput_.Get(); d.outputBefore = upscalerOutputState_;
    d.jitter[0] = upscalerJitter_[0]; d.jitter[1] = upscalerJitter_[1];
    // _rt_MotionVectors.xy = current - previous in UV; providers take previous - current in pixels.
    d.motionScale[0] = -static_cast<float>(width); d.motionScale[1] = -static_cast<float>(height);
    d.fovY = camera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] > 0.f ? camera[FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y] : derivedFov;
    d.cameraNear = camera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] > 0.f ? camera[FLOAT_RENDERPARM_DX12_UPSCALE_NEAR] : derivedNear;
    d.cameraFar = camera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] > 0.f ? camera[FLOAT_RENDERPARM_DX12_UPSCALE_FAR] : derivedFar;
    const double now = Plat_FloatTime();
    // FFX validates frameTimeDelta >= 1 ms; above 1000 fps the provider sees 1 ms.
    d.frameTimeMs = upscalerLastDispatchTime_ > 0.0 ? static_cast<float>(std::clamp((now - upscalerLastDispatchTime_) * 1000.0, 1.0, 100.0)) : 16.6667f;
    d.width = width; d.height = height;
    d.reset = (flags & DX12_UPSCALE_DISPATCH_RESET) != 0 || upscalerHistoryGap_;
    d.nrLayers = nrLayers; d.nr = compose;
    // The model's history breaks with the temporal AA's, on chain rebuild and on a change of proxy curve.
    d.nrReset = d.reset || upscalerNrHistoryGap_ || compose.reversibleMode != upscalerNrLastReversible_;
    if (upscaler_.NeedsDescriptors(d))
    {
        const DescriptorRangeDX12 range = pipeline_.AllocateTransientResources(CUpscalerDX12::DescriptorCount(nrLayers), device_->NextFenceValue());
        if (range.count != CUpscalerDX12::DescriptorCount(nrLayers)) return reject(-2, "descriptor allocation failed");
        d.heap = pipeline_.ResourceDescriptorHeap(); d.cpu = range.cpu; d.gpu = range.gpu;
    }
    if (upscaler_fault_replays.GetInt() > 0) { d.forceFailure = true; upscaler_fault_replays.SetValue(upscaler_fault_replays.GetInt() - 1); }
    const uint64_t serial = upscalerPendingSerial_ + 1;
    d.frame = frameCounter_; d.serial = serial; d.result = &upscalerReplay_[serial % kUpscalerReplaySlots];
    if (!upscaler_.RecordDispatch(*list, d)) return reject(-2, "provider feature not ready");
    upscalerPendingSerial_ = serial;
    // Flushed now so a later wait for this result only waits for this chunk's replay.
    list->Flush();
    pipeline_.InvalidateGraphicsBindings();
    for (ID3D12Resource *resource : {d.color, d.depth, d.motion, d.output}) device_->RetainResource(resource);
    // The callback leaves every resource in its normal state on success and failure alike.
    device_->SetSceneStatesAfterExternal(D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    if (motion->subresourceStates[sub] != D3D12_RESOURCE_STATE_RENDER_TARGET) { motion->subresourceStates[sub] = D3D12_RESOURCE_STATE_RENDER_TARGET; motion->sampledStateValid = false; ++textureStateEpoch_; }
    upscalerOutputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    upscalerQueuedFrame_ = upscalerLastDispatchFrame_ = frameCounter_;
    upscalerLastDispatchTime_ = now;
    upscalerHistoryGap_ = false;
    if (nrLayers) { upscalerNrHistoryGap_ = false; upscalerNrLastReversible_ = compose.reversibleMode; nrStatus = static_cast<int>(nrLayers); }
    upscalerJitter_[0] = upscalerJitter_[1] = 0.f;
    status = 1 | (static_cast<int>(upscalerKind_) << 8);
}

void CShaderAPIDX12::ReleaseIdleUpscaler()
{
    // A full frame without any nonzero mode submission (r_upscaler 0, DX9-equivalent paths, menus): drop the
    // provider feature and output behind a GPU-idle boundary. Modules stay loaded until device-resource release.
    if (upscalerEnabledFrame_ == ~0ull || upscalerEnabledFrame_ + 1 >= frameCounter_) return;
    upscalerEnabledFrame_ = ~0ull;
    if (upscaler_.Feature().kind != UpscalerKindDX12::None || upscalerOutput_)
    {
        ReleaseUpscalerFeature();
        upscalerOutput_.Reset(); upscalerOutputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }
    upscalerKind_ = UpscalerKindDX12::None; upscalerSelectedMode_ = 0; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
}

void CShaderAPIDX12::ReleaseUpscalerResources()
{
    if (upscaler_.Initialized() || upscalerOutput_ || upscalerPendingSerial_ != upscalerConsumedSerial_)
    {
        // No callback may run after the provider modules unload: replay everything and wait for the GPU.
        WaitUpscalerGpuIdle();
        ConsumeUpscalerReplays(false);
    }
    upscaler_.Shutdown();
    upscalerOutput_.Reset(); upscalerOutputState_ = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    upscalerInitialized_ = false; upscalerMode_ = upscalerSelectedMode_ = 0; upscalerKind_ = UpscalerKindDX12::None; upscalerEnabledFrame_ = ~0ull;
    upscalerViewEligible_ = false; upscalerHistoryGap_ = upscalerNrHistoryGap_ = true;
    upscalerNrFailed_ = upscalerNrUnavailableLogged_ = false; upscalerNrFailedSource_ = nullptr; upscalerNrLastReversible_ = 0;
    upscalerFrameToken_ = upscalerQueuedFrame_ = upscalerLastDispatchFrame_ = upscalerLastSuccessFrame_ = ~0ull;
    upscalerPendingSerial_ = upscalerConsumedSerial_ = 0; upscalerJitterIndex_ = 0; upscalerLastDispatchTime_ = 0.0;
    upscalerJitter_[0] = upscalerJitter_[1] = 0.f;
    for (auto &slot : upscalerReplay_) { slot.serial.store(0); slot.code.store(0); slot.nrCode.store(0); slot.frame.store(~0ull); }
    motionResolvedHandle_ = 0; motionResolvedFrame_ = ~0ull;
    renderingInts_[INT_RENDERPARM_DX12_UPSCALE_STATUS] = renderingInts_[INT_RENDERPARM_DX12_NR_STATUS] = 0;
}

} // namespace shaderapidx12
