//========= Copyright Valve Corporation, All rights reserved. ============//
// Development-only `shader_precache` command. The console callback only parses and queues a
// copy of its arguments; the recording owner drains the queue in BeginFrame, opens the VCS
// files read-only, decodes every present static/dynamic combo, reflects each payload and
// reports native/legacy counts and failures. It never writes, replaces or compiles shaders,
// and PSO warming is not implied: pipeline state depends on per-draw snapshot/raster/format state.
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "shader_vcs_dx12.h"
#include "vertex_layout_dx12.h"
#include "native_engine_cbuffers_dx12.h"
#include "filesystem.h"
#include "tier1/convar.h"
#include "tier1/utlbuffer.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cstring>
#include <string>

namespace shaderapidx12
{
namespace
{
struct PrecacheCounts
{
    unsigned files = 0, statics = 0, combos = 0, skipped = 0, native = 0, legacy = 0, failures = 0;
    void Add(const PrecacheCounts &o) { files += o.files; statics += o.statics; combos += o.combos; skipped += o.skipped; native += o.native; legacy += o.legacy; failures += o.failures; }
};

bool IsDxbc(const VcsPayload &payload)
{
    return payload.tokens.size() >= 4 && !std::memcmp(payload.tokens.data(), "DXBC", 4);
}

// Reflects a native payload's space-1 cbuffers against the backend engine layouts; material blocks are
// reported by name/register/size (their layout hash is checked at draw time against the bridge write).
bool ReflectNative(const VcsPayload &payload, VcsStage stage, std::string &error)
{
    Microsoft::WRL::ComPtr<ID3D12ShaderReflection> reflection;
    if (FAILED(D3DReflect(payload.tokens.data(), payload.tokens.size(), IID_PPV_ARGS(&reflection))))
        return error = "D3DReflect failed", false;
    D3D12_SHADER_DESC desc{};
    if (FAILED(reflection->GetDesc(&desc))) return error = "reflection description failed", false;
    const UINT type = D3D12_SHVER_GET_TYPE(desc.Version);
    if (type != static_cast<UINT>(stage == VcsStage::Vertex ? D3D12_SHVER_VERTEX_SHADER : D3D12_SHVER_PIXEL_SHADER))
        return error = "DXBC stage does not match the VCS stage", false;
    std::vector<ShaderInputElementDX12> inputs;
    if (!ReadShaderInputSignatureDX12(payload.tokens.data(), payload.tokens.size(), inputs))
        return error = "input signature reflection failed", false;
    for (UINT i = 0; i < desc.BoundResources; ++i)
    {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        if (FAILED(reflection->GetResourceBindingDesc(i, &binding))) return error = "resource binding reflection failed", false;
        if (binding.Type != D3D_SIT_CBUFFER || binding.Space != 1) continue;
        ID3D12ShaderReflectionConstantBuffer *buffer = reflection->GetConstantBufferByName(binding.Name);
        D3D12_SHADER_BUFFER_DESC bufferDesc{};
        if (!buffer || FAILED(buffer->GetDesc(&bufferDesc))) return error = std::string("cbuffer reflection failed: ") + binding.Name, false;
        for (const auto &engine : dx12native::kEngineCBufferLayouts)
            if (!std::strcmp(engine.name, binding.Name) && (engine.shaderRegister != binding.BindPoint || engine.byteSize != bufferDesc.Size))
                return error = std::string("engine cbuffer layout mismatch: ") + binding.Name, false;
        const unsigned first = stage == VcsStage::Vertex ? 2u : 1u;
        bool engine = false;
        for (const auto &layout : dx12native::kEngineCBufferLayouts) engine |= !std::strcmp(layout.name, binding.Name);
        if (!engine && (binding.BindPoint < first || binding.BindPoint > 7 || (bufferDesc.Size & 15) || bufferDesc.Size > 65536))
            return error = std::string("material cbuffer outside the space-1 slot contract: ") + binding.Name, false;
    }
    return true;
}

PrecacheCounts ValidateFile(IFileSystem &filesystem, const char *name, VcsStage stage, int onlyStatic, int onlyDynamic)
{
    PrecacheCounts counts;
    ShaderVcsFile file;
    std::string error;
    if (!file.Open(filesystem, name, stage, error)) { ++counts.failures; Warning("shader_precache: %s\n", error.c_str()); return counts; }
    ++counts.files;
    const char *stageName = stage == VcsStage::Vertex ? "vs" : "ps";
    uint32_t staticIndex = 0;
    bool foundStatic = onlyStatic < 0;
    for (size_t ordinal = 0; file.StaticComboIndex(ordinal, staticIndex); ++ordinal)
    {
        if (onlyStatic >= 0 && staticIndex != static_cast<uint32_t>(onlyStatic)) continue;
        foundStatic = true;
        if (!file.LoadStaticCombo(staticIndex, error)) { ++counts.failures; Warning("shader_precache: %s\n", error.c_str()); continue; }
        ++counts.statics;
        for (uint32_t dynamic = 0; dynamic < file.DynamicComboCount(); ++dynamic)
        {
            if (onlyDynamic >= 0 && dynamic != static_cast<uint32_t>(onlyDynamic)) continue;
            const VcsPayload *payload = file.DynamicPayload(staticIndex, dynamic);
            if (!payload) { ++counts.skipped; if (onlyDynamic >= 0) Msg("shader_precache: %s %s static %u dynamic %u is a skipped combo (not combo 0)\n", stageName, name, staticIndex, dynamic); continue; }
            ++counts.combos;
            if (!IsDxbc(*payload)) { ++counts.legacy; continue; }
            ++counts.native;
            if (!ReflectNative(*payload, stage, error))
            {
                ++counts.failures;
                Warning("shader_precache: %s %s static %u dynamic %u: %s\n", stageName, name, staticIndex, dynamic, error.c_str());
            }
        }
    }
    if (!foundStatic) { ++counts.failures; Warning("shader_precache: %s %s has no static combo %d\n", stageName, name, onlyStatic); }
    Msg("shader_precache: %s %s (%s) statics=%u combos=%u skipped=%u native=%u legacy=%u failures=%u\n", stageName, name,
        file.Path().c_str(), counts.statics, counts.combos, counts.skipped, counts.native, counts.legacy, counts.failures);
    return counts;
}

// Logical names from the published native manifest mapping (generated/meta equivalent at runtime):
// every file present under shaders/vsh and shaders/psh.
void ForEachPublished(IFileSystem &filesystem, const char *pattern, VcsStage stage, PrecacheCounts &total)
{
    FileFindHandle_t find = FILESYSTEM_INVALID_FIND_HANDLE;
    for (const char *file = filesystem.FindFirstEx(pattern, "GAME", &find); file; file = filesystem.FindNext(find))
    {
        if (file[0] == '.' || filesystem.FindIsDirectory(find)) continue;
        std::string name(file);
        const size_t dot = name.rfind(".vcs");
        if (dot == std::string::npos) continue;
        name.resize(dot);
        total.Add(ValidateFile(filesystem, name.c_str(), stage, -1, -1));
    }
    if (find != FILESYSTEM_INVALID_FIND_HANDLE) filesystem.FindClose(find);
}
} // namespace

void CShaderAPIDX12::QueueShaderPrecacheRequest(const char *name, int staticIndex, int dynamicIndex)
{
    AUTO_LOCK(precacheMutex_);
    if (!precacheAccepting_) { Warning("shader_precache: rejected, the renderer is shut down\n"); return; }
    precacheRequests_.AddToTail({CUtlString(name), staticIndex, dynamicIndex});
}

void CShaderAPIDX12::ProcessShaderPrecacheRequests()
{
    CUtlVector<PrecacheRequestDX12> requests;
    {
        AUTO_LOCK(precacheMutex_);
        if (!precacheRequests_.Count()) return;
        requests.Swap(precacheRequests_);
    }
    IFileSystem *filesystem = g_pShaderDeviceMgrDX12 ? g_pShaderDeviceMgrDX12->HostFileSystem() : nullptr;
    if (!filesystem || !device_ || !device_->IsRecordingOwner()) { Warning("shader_precache: dropped %d request(s): no filesystem or not on the recording owner\n", requests.Count()); return; }
    for (const auto &request : requests)
    {
        PrecacheCounts total;
        if (!V_stricmp(request.name.String(), "all"))
        {
            ForEachPublished(*filesystem, "shaders/vsh/*.vcs", VcsStage::Vertex, total);
            ForEachPublished(*filesystem, "shaders/psh/*.vcs", VcsStage::Pixel, total);
        }
        else
        {
            // A logical name identifies a stage by its _vs/_ps token; resolve both stages when ambiguous.
            const char *name = request.name.String();
            const bool vertex = V_strstr(name, "_vs") != nullptr, pixel = V_strstr(name, "_ps") != nullptr;
            if (vertex || !pixel) total.Add(ValidateFile(*filesystem, name, VcsStage::Vertex, request.staticIndex, request.dynamicIndex));
            if (pixel || !vertex) total.Add(ValidateFile(*filesystem, name, VcsStage::Pixel, request.staticIndex, request.dynamicIndex));
        }
        Msg("shader_precache %s: files=%u statics=%u combos=%u skipped=%u native=%u legacy=%u failures=%u (validation only; PSOs are created per draw state)\n",
            request.name.String(), total.files, total.statics, total.combos, total.skipped, total.native, total.legacy, total.failures);
    }
}

void CShaderAPIDX12::SetShaderPrecacheAccepting(bool accepting)
{
    AUTO_LOCK(precacheMutex_);
    precacheAccepting_ = accepting;
    if (!accepting) precacheRequests_.Purge();
}

} // namespace shaderapidx12

static void ShaderPrecacheCommand(const CCommand &args)
{
    if (args.ArgC() < 2)
    {
        Msg("usage: shader_precache <logicalName> [staticIndex] [dynamicIndex] | shader_precache all\n");
        return;
    }
    if (!shaderapidx12::g_pShaderAPIDX12) { Warning("shader_precache: rejected, the DX12 renderer is not active\n"); return; }
    const int staticIndex = args.ArgC() > 2 ? V_atoi(args[2]) : -1;
    const int dynamicIndex = args.ArgC() > 3 ? V_atoi(args[3]) : -1;
    if ((args.ArgC() > 2 && staticIndex < 0) || (args.ArgC() > 3 && dynamicIndex < 0))
    {
        Warning("shader_precache: indices must be non-negative (static is the already-multiplied Source index)\n");
        return;
    }
    shaderapidx12::g_pShaderAPIDX12->QueueShaderPrecacheRequest(args[1], staticIndex, dynamicIndex);
    Msg("shader_precache: queued %s; results are reported by the render thread at the next frame\n", args[1]);
}
// Development command: FCVAR_CHEAT (requires sv_cheats 1). Not FCVAR_DEVELOPMENTONLY, which retail engines refuse
// to dispatch at all ("Unknown command").
static ConCommand shader_precache("shader_precache", ShaderPrecacheCommand,
    "Development only: validate/reflect published VCS payloads (logical name [static] [dynamic], or 'all') on the render thread", FCVAR_CHEAT);
