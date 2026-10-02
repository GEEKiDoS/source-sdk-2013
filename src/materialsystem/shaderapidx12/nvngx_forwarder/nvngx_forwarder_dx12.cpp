//========= Copyright Valve Corporation, All rights reserved. ============//
// DLSS-NR snippet forwarder for shaderapidx12.
//
// nvngx_dlssnr.dll only accepts calls whose return address lies in a module whose path contains "nvngx.dll"
// (the driver core is _nvngx.dll); any other caller gets FAIL_PlatformError before arguments are read. This DLL is
// therefore built as "nvngx.dll_dlssnr_dx12.dll" and every call into the snippet originates here. Each forwarder
// stores the snippet result in a volatile local before returning: a tail call would jump into the snippet with the
// renderer's return address and fail the same check.
//===========================================================================//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>

namespace
{
typedef int(__cdecl *PFN_InitExt)(unsigned long long, const wchar_t *, ID3D12Device *, int, const void *);
typedef int(__cdecl *PFN_Shutdown1)(ID3D12Device *);
typedef int(__cdecl *PFN_CreateFeature)(ID3D12GraphicsCommandList *, int, void *, void **);
typedef int(__cdecl *PFN_EvaluateFeature)(ID3D12GraphicsCommandList *, const void *, const void *, void *);
typedef int(__cdecl *PFN_ReleaseFeature)(void *);

HMODULE g_snippet = nullptr;
PFN_InitExt g_init = nullptr;
PFN_Shutdown1 g_shutdown1 = nullptr;
PFN_CreateFeature g_create = nullptr;
PFN_EvaluateFeature g_evaluate = nullptr;
PFN_ReleaseFeature g_release = nullptr;

constexpr int kNotLoaded = static_cast<int>(0xBAD00000u | 2u); // NVSDK_NGX_Result_FAIL_PlatformError

template<class Fn> bool Resolve(const char *name, Fn &fn)
{
    fn = reinterpret_cast<Fn>(GetProcAddress(g_snippet, name));
    return fn != nullptr;
}
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_Load(const wchar_t *snippetPath)
{
    if (g_snippet) return 1;
    if (!snippetPath) return 0;
    g_snippet = LoadLibraryExW(snippetPath, nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!g_snippet) return 0;
    if (!Resolve("NVSDK_NGX_D3D12_Init_Ext", g_init) || !Resolve("NVSDK_NGX_D3D12_Shutdown1", g_shutdown1) ||
        !Resolve("NVSDK_NGX_D3D12_CreateFeature", g_create) || !Resolve("NVSDK_NGX_D3D12_EvaluateFeature", g_evaluate) ||
        !Resolve("NVSDK_NGX_D3D12_ReleaseFeature", g_release))
    {
        FreeLibrary(g_snippet); g_snippet = nullptr;
        return 0;
    }
    return 1;
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_Init(unsigned long long applicationId, const wchar_t *dataPath, ID3D12Device *device, int sdkVersion, const void *featureInfo)
{
    if (!g_init) return kNotLoaded;
    volatile int result = g_init(applicationId, dataPath, device, sdkVersion, featureInfo);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_CreateFeature(ID3D12GraphicsCommandList *list, int feature, void *parameters, void **handle)
{
    if (!g_create) return kNotLoaded;
    volatile int result = g_create(list, feature, parameters, handle);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_EvaluateFeature(ID3D12GraphicsCommandList *list, const void *handle, const void *parameters)
{
    if (!g_evaluate) return kNotLoaded;
    volatile int result = g_evaluate(list, handle, parameters, nullptr);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_ReleaseFeature(void *handle)
{
    if (!g_release) return kNotLoaded;
    volatile int result = g_release(handle);
    return result;
}

extern "C" __declspec(dllexport) int __cdecl NrFwd_Shutdown(ID3D12Device *device)
{
    if (!g_shutdown1) return kNotLoaded;
    volatile int result = g_shutdown1(device);
    return result;
}

extern "C" __declspec(dllexport) void __cdecl NrFwd_Unload()
{
    if (g_snippet) FreeLibrary(g_snippet);
    g_snippet = nullptr; g_init = nullptr; g_shutdown1 = nullptr; g_create = nullptr; g_evaluate = nullptr; g_release = nullptr;
}
