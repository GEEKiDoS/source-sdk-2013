#include "shaderapi_dx12.h"
#include "materialsystem/ishadersystem_declarations.h"
#include "renderparm.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include <algorithm>
#include <cstring>
#include <string>
#include <initializer_list>

namespace shaderapidx12
{
namespace
{
const char kMotionEngineVS[] = R"HLSL(
struct DX12LightInfo
{
    float4 color;
    float4 dir;
    float4 pos;
    float4 spotParams;
    float4 atten;
};
cbuffer DX12VSEngine : register(b0, space1)
{
    float4 cConstants0; // @legacy none
    float4 cConstants1; // @legacy none
    float4 cEyePosWaterZ; // @legacy none
    float4 cFlexScale; // @legacy none
    column_major float4x4 cModelViewProj; // @legacy none
    column_major float4x4 cViewProj; // @legacy none
    float4 cModelViewProjZ; // @legacy none
    float4 cViewProjZ; // @legacy none
    float4 cFogParams; // @legacy none
    column_major float4x4 cViewModel; // @legacy none
    float4 cAmbientCube[6]; // @legacy none
    DX12LightInfo cLightInfo[4]; // @legacy none
    int4 cLightCount; // @legacy none
    uint4 cLightEnabled; // @legacy none
    float4 cViewportScale; // @legacy none
    float4 cClipPlanes[6]; // @legacy none
    uint4 cClipMask; // @legacy none
};
cbuffer DX12VSBones : register(b1, space1)
{
    column_major float4x3 cModel[53]; // @legacy none
};
)HLSL";

const char kMotionEnginePS[] = R"HLSL(
struct PixelShaderLightInfo
{
    float4 color;
    float4 pos;
};
cbuffer DX12PSEngine : register(b0, space1)
{
    float4 cPixelFogParams; // @legacy none
    float4 cLinearFogColor; // @legacy none
    float4 cLightScale; // @legacy none
    float3 cAmbientCube[6]; // @legacy none
    PixelShaderLightInfo cLightInfo[3]; // @legacy none
    float4 cAlphaTest; // @legacy none
    float4 cRasterFogColor; // @legacy none
    float4 cRasterFogParams; // @legacy none
};
)HLSL";

const char kMotionVS[] = R"HLSL(
cbuffer DX12MotionVS : register(b7, space1) { column_major float4x4 cPrevViewProj; float4 cMotionParams; float4 cBaseTexTransform[2]; column_major float4x3 cPrevModel[53]; };
struct VSIn { float4 pos:POSITION; float2 uv:TEXCOORD0;
#if COMPRESSED_VERTS
 int2 weights:BLENDWEIGHT;
#else
 float2 weights:BLENDWEIGHT;
#endif
 float4 indices:BLENDINDICES; float3 posFlex:POSITION1; };
struct VSOut { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 cur:TEXCOORD1; float4 prev:TEXCOORD2; float4 clip0:SV_ClipDistance0; float2 clip1:SV_ClipDistance1; };
float3 Blend(float4 p, float3 w, int3 idx, bool prev) {
 if (cMotionParams.x == 0) return prev ? mul(p, cPrevModel[0]) : mul(p, cModel[0]);
 float4x3 m = prev ? cPrevModel[idx.x]*w.x + cPrevModel[idx.y]*w.y + cPrevModel[idx.z]*w.z
                   : cModel[idx.x]*w.x + cModel[idx.y]*w.y + cModel[idx.z]*w.z;
 return mul(p, m);
}
VSOut main(VSIn v) {
 VSOut o; float4 p = float4(v.pos.xyz + v.posFlex * cFlexScale.x, 1);
#if COMPRESSED_VERTS
 float3 w; w.xy = (float2(v.weights) + 1) / 32768;
#else
 float3 w; w.xy = v.weights.xy;
#endif
 w.z = 1 - w.x - w.y;
 int3 idx = D3DCOLORtoUBYTE4(v.indices).xyz;
 float4 cur = mul(float4(Blend(p, w, idx, false), 1), cViewProj);
 float4 prv = mul(float4(Blend(p, w, idx, true), 1), cPrevViewProj);
 o.cur = cur; o.prev = prv;
 o.uv = cMotionParams.y != 0 ? float2(dot(float4(v.uv, 0, 1), cBaseTexTransform[0]), dot(float4(v.uv, 0, 1), cBaseTexTransform[1])) : v.uv;
 [unroll] for (uint i = 0; i < 6; ++i) { float d = ((cClipMask.x & (1u << i)) != 0u) ? dot(cur, cClipPlanes[i]) : 1.0f; if (i < 4) o.clip0[i] = d; else o.clip1[i - 4] = d; }
 o.pos = cur; o.pos.xy = mad(cViewportScale.xy, cur.ww, cur.xy);
 return o;
}
)HLSL";

const char kMotionPS[] = R"HLSL(
Texture2D t0:register(t0); SamplerState s0:register(s0);
struct PSIn { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 cur:TEXCOORD1; float4 prev:TEXCOORD2; };
float4 main(PSIn i):SV_Target0 {
 if (cAlphaTest.x != 0) {
  float a = t0.Sample(s0, i.uv).a; int c = (int)cAlphaTest.y; float r = cAlphaTest.z;
  bool passes = (c==2&&a<r)||(c==3&&a==r)||(c==4&&a<=r)||(c==5&&a>r)||(c==6&&a!=r)||(c==7&&a>=r)||c==8;
  if (!passes) discard;
 }
 float2 c = i.cur.xy / i.cur.w, p = i.prev.xy / i.prev.w;
 return float4((c - p) * float2(0.5, -0.5), 0, 1);
}
)HLSL";

const char kMotionReprojectVS[] = R"HLSL(float4 main(uint id:SV_VertexID):SV_Position { float2 p=float2((id<<1)&2,id&2); return float4(p*float2(2,-2)+float2(-1,1),0,1); })HLSL";

const char kMotionReprojectPS[] = R"HLSL(
cbuffer C:register(b0){ column_major float4x4 clipToPrevClip; float4 vp; }
#if MOTION_MSAA
Texture2DMS<float> depthTex:register(t0);
float4 main(float4 pos:SV_Position, uint s:SV_SampleIndex):SV_Target { float d = depthTex.Load(int2(pos.xy), s);
#else
Texture2D<float> depthTex:register(t0);
float4 main(float4 pos:SV_Position):SV_Target { float d = depthTex.Load(int3(pos.xy, 0));
#endif
 float2 ndc = float2((pos.x - vp.z) * vp.x * 2 - 1, 1 - (pos.y - vp.w) * vp.y * 2);
 float4 prev = mul(float4(ndc, d, 1), clipToPrevClip); prev.xy /= prev.w;
 return float4((ndc - prev.xy) * float2(0.5, -0.5), 0, 1); }
)HLSL";

bool ValidateMotionNative(const ShaderRecordDX12 *record, bool pixel)
{
    if (!record || record->legacyBytecode.size()) return false;
    for (const auto &binding : record->nativeCBuffers)
    {
        const dx12native::EngineCBufferLayoutDX12 *layout = nullptr;
        for (const auto &candidate : dx12native::kEngineCBufferLayouts)
            if (binding.name == candidate.name) { layout = &candidate; break; }
        if (!layout || layout->stage != (pixel ? dx12native::kStagePixel : dx12native::kStageVertex) || layout->shaderRegister != binding.shaderRegister || layout->byteSize != binding.byteSize || layout->memberCount != binding.members.size()) return false;
        for (uint32_t m = 0; m < layout->memberCount; ++m)
            if (binding.members[m].name != layout->members[m].name || binding.members[m].offset != layout->members[m].offset || binding.members[m].byteSize != layout->members[m].size) return false;
    }
    return true;
}

uint64_t MotionHash(std::initializer_list<uint64_t> values)
{
    uint64_t hash = 1469598103934665603ull;
    for (uint64_t value : values)
    {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

bool MotionLoggingEnabled()
{
    static const bool enabled = CommandLine() && CommandLine()->FindParm("-dx12motionlog") != 0;
    return enabled;
}

}

ShaderRecordDX12 *CShaderAPIDX12::MotionVertexShader(VertexFormat_t format)
{
    return motionVS_[(format & VERTEX_FORMAT_COMPRESSED) ? 1 : 0];
}

bool CShaderAPIDX12::EnsureMotionResources()
{
    if (motionUnavailable_) return false;
    if (!device_ || !device_->NativeDevice() || !device_->SceneColor() || !device_->SceneDepth()) return false;
    if (!motionVS_[0])
    {
        for (int i = 0; i < 2; ++i)
        {
            const std::string source = std::string("#define COMPRESSED_VERTS ") + (i ? "1\n" : "0\n") + kMotionEngineVS + kMotionVS;
            motionVS_[i] = CompileNativeShaderRecordDX12(device_, source, false, "vs_5_1");
            if (!motionVS_[i] || !ReflectNativeCBuffersDX12(motionVS_[i]) || !ValidateMotionNative(motionVS_[i], false))
            {
                Warning("ShaderAPIDX12: motion shaders unavailable\n");
                for (auto *&record : motionVS_) { delete record; record = nullptr; }
                motionUnavailable_ = true; renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = -1; return false;
            }
        }
        motionPS_ = CompileNativeShaderRecordDX12(device_, std::string(kMotionEnginePS) + kMotionPS, true, "ps_5_1");
        if (!motionPS_ || !ReflectNativeCBuffersDX12(motionPS_) || !ValidateMotionNative(motionPS_, true))
        {
            Warning("ShaderAPIDX12: motion shaders unavailable\n");
            for (auto *&record : motionVS_) { delete record; record = nullptr; }
            delete motionPS_; motionPS_ = nullptr;
            motionUnavailable_ = true; renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = -1; return false;
        }
    }
    const D3D12_RESOURCE_DESC scene = device_->SceneColor()->GetDesc();
    if (motionTarget_ && scene.Width == motionTargetWidth_ && scene.Height == motionTargetHeight_ && scene.SampleDesc.Count == motionTargetSamples_ && scene.SampleDesc.Quality == motionTargetQuality_)
    {
        renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = 1; return true;
    }
    if (!device_->SupportsMSAAFormat(DXGI_FORMAT_R16G16B16A16_FLOAT, scene.SampleDesc.Count, scene.SampleDesc.Quality))
    {
        if (!(motionWarned_ & 32)) { motionWarned_ |= 32; Warning("ShaderAPIDX12: motion pass unavailable at %ux MSAA quality %u\n", scene.SampleDesc.Count, scene.SampleDesc.Quality); }
        motionTarget_.Reset(); motionTargetSamples_ = 0; renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = -2; return false;
    }
    device_->DrainRecording();
    if (motionTarget_) device_->RetainResource(motionTarget_.Get());
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = scene.Width; desc.Height = scene.Height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc = scene.SampleDesc; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[0] = 0; clear.Color[1] = 0; clear.Color[2] = 0; clear.Color[3] = 1;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    if (FAILED(device_->NativeDevice()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS(&resource)))) return false;
    if (!motionRtvHeap_)
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{}; heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heapDesc.NumDescriptors = 1;
        if (FAILED(device_->NativeDevice()->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&motionRtvHeap_)))) return false;
        motionRtv_ = motionRtvHeap_->GetCPUDescriptorHandleForHeapStart();
    }
    D3D12_RENDER_TARGET_VIEW_DESC rtv{}; rtv.Format = desc.Format; rtv.ViewDimension = desc.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
    device_->NativeDevice()->CreateRenderTargetView(resource.Get(), &rtv, motionRtv_);
    motionTarget_ = std::move(resource); motionTargetWidth_ = static_cast<UINT>(scene.Width); motionTargetHeight_ = scene.Height; motionTargetSamples_ = scene.SampleDesc.Count; motionTargetQuality_ = scene.SampleDesc.Quality; motionTargetState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (motionReprojectSamples_ != scene.SampleDesc.Count) { motionReprojectPso_.Reset(); motionReprojectSamples_ = 0; }
    renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = 1; return true;
}
void CShaderAPIDX12::TransitionMotionTarget(D3D12_RESOURCE_STATES desired)
{
    if (!motionTarget_ || motionTargetState_ == desired) return;
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = motionTarget_.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = motionTargetState_;
    barrier.Transition.StateAfter = desired;
    device_->CommandList()->ResourceBarrier(1, &barrier);
    motionTargetState_ = desired;
}

bool CShaderAPIDX12::PrepareMotionBinding(RenderTargetBindingDX12 &binding)
{
    binding = {};
    if (!motionTarget_ || !device_ || !device_->SceneDepth() || !device_->CommandList()) return false;
    binding.colors[0] = motionTarget_.Get(); binding.rtvs[0] = motionRtv_; binding.colorFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; binding.colorCount = 1; binding.color = binding.colors[0]; binding.rtv = binding.rtvs[0]; binding.colorFormat = binding.colorFormats[0]; binding.depth = device_->SceneDepth(); binding.dsv = device_->SceneReadOnlyDSV(); binding.depthFormat = device_->SceneDepthFormat(); binding.width = motionTargetWidth_; binding.height = motionTargetHeight_; binding.sampleCount = motionTargetSamples_; binding.sampleQuality = motionTargetQuality_;
    TransitionMotionTarget(D3D12_RESOURCE_STATE_RENDER_TARGET); device_->TransitionSceneDepth(D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); return true;
}

void CShaderAPIDX12::DrawMotionReprojection()
{
    if (!motionTarget_ || !device_ || !device_->CommandList() || !device_->SceneDepth()) return;
    auto *native = device_->NativeDevice(); auto *list = device_->CommandList();
    if (!motionReprojectRoot_)
    {
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors = 1; range.BaseShaderRegister = 0;
        D3D12_ROOT_PARAMETER params[2]{}; params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; params[0].DescriptorTable.NumDescriptorRanges = 1; params[0].DescriptorTable.pDescriptorRanges = &range; params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS; params[1].Constants.Num32BitValues = 20; params[1].Constants.ShaderRegister = 0; params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC desc{}; desc.NumParameters = 2; desc.pParameters = params;
        Microsoft::WRL::ComPtr<ID3DBlob> blob, error; if (FAILED(D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error)) || FAILED(native->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&motionReprojectRoot_)))) return;
    }
    const UINT samples = motionTargetSamples_;
    if (!motionReprojectPso_)
    {
        ShaderRecordDX12 *vs = CompileNativeShaderRecordDX12(device_, kMotionReprojectVS, false, "vs_5_1"); ShaderRecordDX12 *ps = CompileNativeShaderRecordDX12(device_, std::string("#define MOTION_MSAA ") + (samples > 1 ? "1\n" : "0\n") + kMotionReprojectPS, true, "ps_5_1");
        if (!vs || !ps) { delete vs; delete ps; return; }
        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{}; desc.pRootSignature = motionReprojectRoot_.Get(); desc.VS = vs->Bytecode(); desc.PS = ps->Bytecode(); desc.SampleMask = UINT_MAX; desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; desc.NumRenderTargets = 1; desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; desc.SampleDesc.Count = samples; desc.SampleDesc.Quality = motionTargetQuality_; desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID; desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; desc.RasterizerState.DepthClipEnable = TRUE; desc.RasterizerState.MultisampleEnable = samples > 1; desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL; desc.DepthStencilState.DepthEnable = FALSE; desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        if (FAILED(native->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&motionReprojectPso_)))) { delete vs; delete ps; return; }
        delete vs; delete ps; motionReprojectSamples_ = samples;
    }
    DescriptorRangeDX12 srv = pipeline_.AllocateTransientResources(1, device_->NextFenceValue()); if (!srv.cpu.ptr) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{}; view.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; view.ViewDimension = samples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D; if (samples == 1) view.Texture2D.MipLevels = 1; native->CreateShaderResourceView(device_->SceneDepth(), &view, srv.cpu);
    device_->TransitionSceneDepth(D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE); TransitionMotionTarget(D3D12_RESOURCE_STATE_RENDER_TARGET);
    VMatrix curVP = matrices_[MATERIAL_PROJECTION] * matrices_[MATERIAL_VIEW], inv, prevVP; if (!MatrixInverseGeneral(curVP, inv)) return; if (motionPrevViewProjValid_[motionPassSlot_]) std::memcpy(prevVP.Base(), motionPrevViewProj_[motionPassSlot_].data(), sizeof(float) * 16); else prevVP = curVP; VMatrix clipToPrev = prevVP * inv;
    float constants[20]{}; std::memcpy(constants, clipToPrev.Base(), sizeof(float) * 16); float width = static_cast<float>(motionTargetWidth_), height = static_cast<float>(motionTargetHeight_), left = 0, top = 0; if (viewportCount_ > 0) { width = static_cast<float>(viewports_[0].m_nWidth); height = static_cast<float>(viewports_[0].m_nHeight); left = static_cast<float>(viewports_[0].m_nTopLeftX); top = static_cast<float>(viewports_[0].m_nTopLeftY); } constants[16] = width > 0 ? 1.f / width : 0; constants[17] = height > 0 ? 1.f / height : 0; constants[18] = left; constants[19] = top;
    D3D12_VIEWPORT viewport{left, top, width, height, 0, 1}; D3D12_RECT scissor{static_cast<LONG>(left), static_cast<LONG>(top), static_cast<LONG>(left + width), static_cast<LONG>(top + height)}; ID3D12DescriptorHeap *heap = pipeline_.ResourceDescriptorHeap(); list->SetDescriptorHeaps(1, &heap); list->RSSetViewports(1, &viewport); list->RSSetScissorRects(1, &scissor); list->OMSetRenderTargets(1, &motionRtv_, FALSE, nullptr); list->SetGraphicsRootSignature(motionReprojectRoot_.Get()); list->SetGraphicsRootDescriptorTable(0, srv.gpu); list->SetGraphicsRoot32BitConstants(1, 20, constants, 0); list->SetPipelineState(motionReprojectPso_.Get()); list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST); list->DrawInstanced(3, 1, 0, 0); pipeline_.InvalidateGraphicsBindings();
}

void CShaderAPIDX12::SetMotionPass(int mode)
{
    const bool canRecord = device_ && device_->IsRecordingOwner() && device_->CommandList();
    if ( mode == DX12_MOTION_PASS_END )
    {
        if ( motionPassState_ == MotionPassStateDX12::None )
            return;
        if ( canRecord )
        {
            if ( motionPassState_ == MotionPassStateDX12::Active )
            {
                FlushBufferedPrimitives();
                ResolveMotionTarget();
            }
            else
            {
                MarkMotionTargetStale();
                ++motionSuppressedPasses_;
            }
        }
        if (MotionLoggingEnabled() && frameCounter_ - motionLogFrame_ >= 120)
        {
            motionLogFrame_ = frameCounter_;
            Msg("ShaderAPIDX12 motion: draws %u objects %u suppressed %u\n", motionPassDraws_, motionPassObjects_, motionSuppressedPasses_);
        }
        motionPassState_ = MotionPassStateDX12::None;
        motionResolveTarget_ = 0;
        return;
    }
    if (motionPassState_ != MotionPassStateDX12::None) SetMotionPass(DX12_MOTION_PASS_END);
    auto fail = [&](unsigned bit, const char *why) { if (!(motionWarned_ & bit)) { motionWarned_ |= static_cast<uint8_t>(bit); Warning("ShaderAPIDX12: motion pass suppressed: %s\n", why); } motionPassState_ = MotionPassStateDX12::Suppressed; };
    if (!canRecord) { fail(1, "off the recording thread"); return; }
    if (mode == DX12_MOTION_PASS_APPEND_MAIN && motionMainFrame_ != frameCounter_) { if (!(motionWarned_ & 2)) { motionWarned_ |= 2; Warning("ShaderAPIDX12: motion append without a main pass; promoting to begin\n"); } mode = DX12_MOTION_PASS_BEGIN_MAIN; }
    if (mode == DX12_MOTION_PASS_BEGIN_VIEWMODEL && motionMainFrame_ != frameCounter_) { fail(4, "viewmodel pass without a main pass this frame"); return; }
    if (!EnsureMotionResources()) { fail(8, "private resources unavailable"); return; }
    TextureRecord *record = FindTexture(renderTargets_[0]); if (!record || !(record->flags & TEXTURE_CREATE_RENDERTARGET) || record->format != IMAGE_FORMAT_RGBA16161616F || record->width != static_cast<int>(motionTargetWidth_) || record->height != static_cast<int>(motionTargetHeight_)) { fail(16, "render target 0 is not a scene-sized RGBA16F render target"); return; }
    FlushBufferedPrimitives(); CommitTransforms(); motionPassSlot_ = mode == DX12_MOTION_PASS_BEGIN_VIEWMODEL ? 1 : 0;
    if (mode == DX12_MOTION_PASS_BEGIN_MAIN || mode == DX12_MOTION_PASS_BEGIN_VIEWMODEL) { motionPrevViewProj_[motionPassSlot_] = motionCurViewProj_[motionPassSlot_]; motionPrevViewProjValid_[motionPassSlot_] = motionCurViewProjValid_[motionPassSlot_]; std::memcpy(motionCurViewProj_[motionPassSlot_].data(), vsFloat_[VERTEX_SHADER_VIEWPROJ].data(), sizeof(float) * 16); motionCurViewProjValid_[motionPassSlot_] = true; }
    if (mode == DX12_MOTION_PASS_BEGIN_MAIN) { motionMainFrame_ = frameCounter_; motionHistoryCurrent_ ^= 1; motionHistory_[motionHistoryCurrent_].Clear(); motionPassDraws_ = motionPassObjects_ = 0; TransitionMotionTarget(D3D12_RESOURCE_STATE_RENDER_TARGET); const float clear[4] = {0, 0, 0, 1}; device_->CommandList()->ClearRenderTargetView(motionRtv_, clear, 0, nullptr); DrawMotionReprojection(); }
    motionResolveTarget_ = renderTargets_[0]; motionPassState_ = MotionPassStateDX12::Active; motionLastObjectKey_ = INT_MIN; motionObjectOrdinal_ = 0;
}

void CShaderAPIDX12::FillMotionBlock(const VertexBindingDX12 &vb, CIndexBufferDX12 *ib, size_t indexOffset, int firstIndex, int indexCount)
{
    const int n = std::clamp(motionBoneRows_, 1, NUM_MODEL_TRANSFORMS);
    const char *shaderName = activeSnapshot_.vertexShaderName.c_str();
    const bool texTransform = activeSnapshot_.alphaTest &&
        (std::strncmp(shaderName, "vertexlit_and_unlit_generic", 27) == 0 ||
         std::strncmp(shaderName, "lightmappedgeneric", 18) == 0);
    motionBlock_.cMotionParams[0] = boneCount_ > 0 ? 1.f : 0.f;
    motionBlock_.cMotionParams[1] = texTransform ? 1.f : 0.f;
    motionBlock_.cMotionParams[2] = motionBlock_.cMotionParams[3] = 0;
    if (texTransform)
    {
        std::memcpy(motionBlock_.cBaseTexTransform[0], vsFloat_[VERTEX_SHADER_SHADER_SPECIFIC_CONST_0].data(), sizeof(float) * 4);
        std::memcpy(motionBlock_.cBaseTexTransform[1], vsFloat_[VERTEX_SHADER_SHADER_SPECIFIC_CONST_1].data(), sizeof(float) * 4);
    }
    else
    {
        std::memset(motionBlock_.cBaseTexTransform, 0, sizeof(motionBlock_.cBaseTexTransform));
    }

    const auto &previous = motionPrevViewProjValid_[motionPassSlot_] ? motionPrevViewProj_[motionPassSlot_] : motionCurViewProj_[motionPassSlot_];
    std::memcpy(motionBlock_.cPrevViewProj, previous.data(), sizeof(float) * 16);
    const float *current = vsFloat_[VERTEX_SHADER_MODEL].data();
    std::memset(motionBlock_.cPrevModel, 0, sizeof(motionBlock_.cPrevModel));

    if (motionObjectKey_ == 0)
    {
        std::memcpy(motionBlock_.cPrevModel, current, sizeof(float) * n * 12);
    }
    else
    {
        if (motionObjectKey_ != motionLastObjectKey_)
        {
            motionLastObjectKey_ = motionObjectKey_;
            motionObjectOrdinal_ = 0;
            ++motionPassObjects_;
        }
        const uint64_t materialVS = boundVS_ == VERTEX_SHADER_HANDLE_INVALID ? 0 : reinterpret_cast<ShaderRecordDX12 *>(boundVS_)->identity;
        const uint64_t key = ib && !ib->IsDynamic()
            ? MotionHash({static_cast<uint32_t>(motionObjectKey_), reinterpret_cast<uintptr_t>(ib), indexOffset, static_cast<uint32_t>(firstIndex), static_cast<uint32_t>(indexCount)})
            : MotionHash({static_cast<uint32_t>(motionObjectKey_), materialVS, vb.vertexCount, static_cast<uint32_t>(indexCount), motionObjectOrdinal_++});

        auto &write = motionHistory_[motionHistoryCurrent_];
        const auto &read = motionHistory_[motionHistoryCurrent_ ^ 1];
        const auto found = read.entries.Find(key);
        const bool hit = found != read.entries.InvalidHandle() &&
            read.entries[found].materialVS == materialVS &&
            read.entries[found].count == static_cast<uint32_t>(n) &&
            read.entries[found].offset + read.entries[found].count * 12 <= static_cast<uint32_t>(read.rows.Count());
        std::memcpy(motionBlock_.cPrevModel, hit ? read.rows.Base() + read.entries[found].offset : current, sizeof(float) * n * 12);
        if (write.entries.Find(key) == write.entries.InvalidHandle())
        {
            const MotionHistoryEntryDX12 entry{static_cast<uint32_t>(write.rows.Count()), static_cast<uint32_t>(n), materialVS};
            write.rows.AddMultipleToTail(n * 12, current);
            write.entries.Insert(key, entry);
        }
    }
    ++motionBlockVersion_;
    ++motionPassDraws_;
}


void CShaderAPIDX12::ReleaseMotionResources()
{
    for (auto *&record : motionVS_) { if (record) RetireShaderPipelines(record); delete record; record = nullptr; }
    if (motionPS_) { RetireShaderPipelines(motionPS_); delete motionPS_; motionPS_ = nullptr; }
    motionReprojectRoot_.Reset(); motionReprojectPso_.Reset(); motionReprojectSamples_ = 0; motionRtvHeap_.Reset(); motionTarget_.Reset(); motionPassState_ = MotionPassStateDX12::None; motionUnavailable_ = false; motionMainFrame_ = ~0ull; motionWarned_ = 0; motionResolveTarget_ = 0; motionTargetSamples_ = 0; renderingInts_[INT_RENDERPARM_DX12_MOTION_STATUS] = 0; motionHistory_[0].Clear(); motionHistory_[1].Clear(); motionPrevViewProjValid_ = {}; motionCurViewProjValid_ = {};
}

} // namespace shaderapidx12
