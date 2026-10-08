//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 motion vector pass: private motion shaders, the scene-sized
//			motion target, camera reprojection and per-object history
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "materialsystem/ishadersystem_declarations.h"
#include "renderparm.h"
#include "tier0/dbg.h"
#include "tier0/icommandline.h"
#include "tier1/strtools.h"
#include "tier1/utlstring.h"

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

//-----------------------------------------------------------------------------
// Purpose: Verifies that a motion shader's reflected cbuffers match the engine layouts
//-----------------------------------------------------------------------------
bool ValidateMotionNative( const ShaderRecordDX12 *pRecord, bool bPixel )
{
	if ( pRecord->legacyBytecode.Count() )
		return false;
	for ( int i = 0; i < pRecord->nativeCBuffers.Count(); ++i )
	{
		const ShaderRecordDX12::NativeCBufferBindingDX12 &binding = pRecord->nativeCBuffers[i];
		const dx12native::EngineCBufferLayoutDX12 *pLayout = nullptr;
		for ( int j = 0; j < ARRAYSIZE( dx12native::kEngineCBufferLayouts ); ++j )
			if ( binding.name == dx12native::kEngineCBufferLayouts[j].name )
			{
				pLayout = &dx12native::kEngineCBufferLayouts[j];
				break;
			}
		if ( !pLayout || pLayout->stage != ( bPixel ? dx12native::kStagePixel : dx12native::kStageVertex ) || pLayout->shaderRegister != binding.shaderRegister || pLayout->byteSize != binding.byteSize || pLayout->memberCount != static_cast<uint32_t>( binding.members.Count() ) )
			return false;
		for ( uint32_t m = 0; m < pLayout->memberCount; ++m )
			if ( binding.members[m].name != pLayout->members[m].name || binding.members[m].offset != pLayout->members[m].offset || binding.members[m].byteSize != pLayout->members[m].size )
				return false;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: FNV-style hash of a list of 64-bit values; keys the per-object history
//-----------------------------------------------------------------------------
uint64 MotionHash( const uint64 *pValues, int nCount )
{
	uint64 nHash = 1469598103934665603ull;
	for ( int i = 0; i < nCount; ++i )
	{
		nHash ^= pValues[i];
		nHash *= 1099511628211ull;
	}
	return nHash;
}

bool MotionLoggingEnabled()
{
	static const bool s_bEnabled = CommandLine()->FindParm( "-dx12motionlog" ) != 0;
	return s_bEnabled;
}

} // namespace

//-----------------------------------------------------------------------------
// Purpose: Selects the motion vertex shader matching the vertex compression
//-----------------------------------------------------------------------------
ShaderRecordDX12 *CShaderAPIDX12::MotionVertexShader( VertexFormat_t vertexFormat )
{
	return m_MotionVS[( vertexFormat & VERTEX_FORMAT_COMPRESSED ) ? 1 : 0];
}

//-----------------------------------------------------------------------------
// Purpose: Lazily compiles the motion shaders and (re)creates the motion target
//			to match the scene color buffer; updates INT_RENDERPARM_DX12_MOTION_STATUS
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::EnsureMotionResources()
{
	if ( m_bMotionUnavailable )
		return false;
	if ( !m_pDevice->NativeDevice() || !m_pDevice->SceneColor() || !m_pDevice->SceneDepth() )
		return false;
	if ( !m_MotionVS[0] )
	{
		for ( int i = 0; i < 2; ++i )
		{
			CUtlString source( i ? "#define COMPRESSED_VERTS 1\n" : "#define COMPRESSED_VERTS 0\n" );
			source += kMotionEngineVS;
			source += kMotionVS;
			m_MotionVS[i] = CompileNativeShaderRecordDX12( m_pDevice, source.Get(), false, "vs_5_1" );
			if ( !m_MotionVS[i] || !ReflectNativeCBuffersDX12( m_MotionVS[i] ) || !ValidateMotionNative( m_MotionVS[i], false ) )
			{
				Warning( "ShaderAPIDX12: motion shaders unavailable\n" );
				for ( int j = 0; j < ARRAYSIZE( m_MotionVS ); ++j )
				{
					delete m_MotionVS[j];
					m_MotionVS[j] = nullptr;
				}
				m_bMotionUnavailable = true;
				m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = -1;
				return false;
			}
		}
		CUtlString source( kMotionEnginePS );
		source += kMotionPS;
		m_pMotionPS = CompileNativeShaderRecordDX12( m_pDevice, source.Get(), true, "ps_5_1" );
		if ( !m_pMotionPS || !ReflectNativeCBuffersDX12( m_pMotionPS ) || !ValidateMotionNative( m_pMotionPS, true ) )
		{
			Warning( "ShaderAPIDX12: motion shaders unavailable\n" );
			for ( int j = 0; j < ARRAYSIZE( m_MotionVS ); ++j )
			{
				delete m_MotionVS[j];
				m_MotionVS[j] = nullptr;
			}
			delete m_pMotionPS;
			m_pMotionPS = nullptr;
			m_bMotionUnavailable = true;
			m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = -1;
			return false;
		}
	}
	const D3D12_RESOURCE_DESC scene = m_pDevice->SceneColor()->GetDesc();
	if ( m_pMotionTarget && scene.Width == m_nMotionTargetWidth && scene.Height == m_nMotionTargetHeight && scene.SampleDesc.Count == m_nMotionTargetSamples && scene.SampleDesc.Quality == m_nMotionTargetQuality )
	{
		m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = 1;
		return true;
	}
	if ( !m_pDevice->SupportsMSAAFormat( DXGI_FORMAT_R16G16B16A16_FLOAT, scene.SampleDesc.Count, scene.SampleDesc.Quality ) )
	{
		if ( !( m_nMotionWarned & 32 ) )
		{
			m_nMotionWarned |= 32;
			Warning( "ShaderAPIDX12: motion pass unavailable at %ux MSAA quality %u\n", scene.SampleDesc.Count, scene.SampleDesc.Quality );
		}
		m_pMotionTarget.Reset();
		m_nMotionTargetSamples = 0;
		m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = -2;
		return false;
	}
	m_pDevice->DrainRecording();
	if ( m_pMotionTarget )
		m_pDevice->RetainResource( m_pMotionTarget.Get() );
	D3D12_HEAP_PROPERTIES heap{};
	heap.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc{};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = scene.Width;
	desc.Height = scene.Height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	desc.SampleDesc = scene.SampleDesc;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear{};
	clear.Format = desc.Format;
	clear.Color[0] = 0;
	clear.Color[1] = 0;
	clear.Color[2] = 0;
	clear.Color[3] = 1;
	Microsoft::WRL::ComPtr<ID3D12Resource> pResource;
	if ( FAILED( m_pDevice->NativeDevice()->CreateCommittedResource( &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &clear, IID_PPV_ARGS( &pResource ) ) ) )
		return false;
	if ( !m_pMotionRtvHeap )
	{
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		heapDesc.NumDescriptors = 1;
		if ( FAILED( m_pDevice->NativeDevice()->CreateDescriptorHeap( &heapDesc, IID_PPV_ARGS( &m_pMotionRtvHeap ) ) ) )
			return false;
		m_MotionRtv = m_pMotionRtvHeap->GetCPUDescriptorHandleForHeapStart();
	}
	D3D12_RENDER_TARGET_VIEW_DESC rtv{};
	rtv.Format = desc.Format;
	rtv.ViewDimension = desc.SampleDesc.Count > 1 ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
	m_pDevice->NativeDevice()->CreateRenderTargetView( pResource.Get(), &rtv, m_MotionRtv );
	m_pMotionTarget = std::move( pResource );
	m_nMotionTargetWidth = static_cast<UINT>( scene.Width );
	m_nMotionTargetHeight = scene.Height;
	m_nMotionTargetSamples = scene.SampleDesc.Count;
	m_nMotionTargetQuality = scene.SampleDesc.Quality;
	// A recreated target cannot carry camera/object/provider history from its previous shape.
	m_MotionHistory[0].Clear();
	m_MotionHistory[1].Clear();
	memset( m_MotionPrevViewProjValid, 0, sizeof( m_MotionPrevViewProjValid ) );
	memset( m_MotionCurViewProjValid, 0, sizeof( m_MotionCurViewProjValid ) );
	m_hMotionResolvedHandle = 0;
	m_nMotionResolvedFrame = ~0ull;
	m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = m_bFrameGenHistoryGap = true;
	m_MotionTargetState = D3D12_RESOURCE_STATE_RENDER_TARGET;
	if ( m_nMotionReprojectSamples != scene.SampleDesc.Count )
	{
		m_pMotionReprojectPso.Reset();
		m_nMotionReprojectSamples = 0;
	}
	m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = 1;
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Records a barrier moving the motion target to the desired state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::TransitionMotionTarget( D3D12_RESOURCE_STATES desiredState )
{
	if ( !m_pMotionTarget || m_MotionTargetState == desiredState )
		return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = m_pMotionTarget.Get();
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = m_MotionTargetState;
	barrier.Transition.StateAfter = desiredState;
	m_pDevice->CommandList()->ResourceBarrier( 1, &barrier );
	m_MotionTargetState = desiredState;
}

//-----------------------------------------------------------------------------
// Purpose: Builds the render target binding used while the motion pass is active
//-----------------------------------------------------------------------------
bool CShaderAPIDX12::PrepareMotionBinding( RenderTargetBindingDX12 &binding )
{
	binding = {};
	if ( !m_pMotionTarget || !m_pDevice || !m_pDevice->SceneDepth() || !m_pDevice->CommandList() )
		return false;
	binding.colors[0] = m_pMotionTarget.Get();
	binding.rtvs[0] = m_MotionRtv;
	binding.colorFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
	binding.colorCount = 1;
	binding.color = binding.colors[0];
	binding.rtv = binding.rtvs[0];
	binding.colorFormat = binding.colorFormats[0];
	binding.depth = m_pDevice->SceneDepth();
	binding.dsv = m_pDevice->SceneReadOnlyDSV();
	binding.depthFormat = m_pDevice->SceneDepthFormat();
	binding.width = m_nMotionTargetWidth;
	binding.height = m_nMotionTargetHeight;
	binding.sampleCount = m_nMotionTargetSamples;
	binding.sampleQuality = m_nMotionTargetQuality;
	TransitionMotionTarget( D3D12_RESOURCE_STATE_RENDER_TARGET );
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Seeds the motion target with camera-only motion reconstructed from
//			scene depth and the previous view-projection matrix
//-----------------------------------------------------------------------------
void CShaderAPIDX12::DrawMotionReprojection()
{
	if ( !m_pDevice->SceneDepth() )
		return;
	ID3D12Device *pNative = m_pDevice->NativeDevice();
	CCommandRecorderDX12 *pList = m_pDevice->CommandList();
	if ( !m_pMotionReprojectRoot )
	{
		D3D12_DESCRIPTOR_RANGE range{};
		range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		range.NumDescriptors = 1;
		range.BaseShaderRegister = 0;
		D3D12_ROOT_PARAMETER params[2]{};
		params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[0].DescriptorTable.NumDescriptorRanges = 1;
		params[0].DescriptorTable.pDescriptorRanges = &range;
		params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
		params[1].Constants.Num32BitValues = 20;
		params[1].Constants.ShaderRegister = 0;
		params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
		D3D12_ROOT_SIGNATURE_DESC desc{};
		desc.NumParameters = 2;
		desc.pParameters = params;
		Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
		if ( FAILED( D3D12SerializeRootSignature( &desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error ) ) || FAILED( pNative->CreateRootSignature( 0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS( &m_pMotionReprojectRoot ) ) ) )
			return;
	}
	const UINT nSamples = m_nMotionTargetSamples;
	if ( !m_pMotionReprojectPso )
	{
		ShaderRecordDX12 *pVS = CompileNativeShaderRecordDX12( m_pDevice, kMotionReprojectVS, false, "vs_5_1" );
		CUtlString psSource( nSamples > 1 ? "#define MOTION_MSAA 1\n" : "#define MOTION_MSAA 0\n" );
		psSource += kMotionReprojectPS;
		ShaderRecordDX12 *pPS = CompileNativeShaderRecordDX12( m_pDevice, psSource.Get(), true, "ps_5_1" );
		if ( !pVS || !pPS )
		{
			delete pVS;
			delete pPS;
			return;
		}
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
		desc.pRootSignature = m_pMotionReprojectRoot.Get();
		desc.VS = pVS->Bytecode();
		desc.PS = pPS->Bytecode();
		desc.SampleMask = UINT_MAX;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
		desc.SampleDesc.Count = nSamples;
		desc.SampleDesc.Quality = m_nMotionTargetQuality;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.RasterizerState.MultisampleEnable = nSamples > 1;
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.DepthStencilState.DepthEnable = FALSE;
		desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
		desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_ALWAYS;
		if ( FAILED( pNative->CreateGraphicsPipelineState( &desc, IID_PPV_ARGS( &m_pMotionReprojectPso ) ) ) )
		{
			delete pVS;
			delete pPS;
			return;
		}
		delete pVS;
		delete pPS;
		m_nMotionReprojectSamples = nSamples;
	}
	DescriptorRangeDX12 srv = m_Pipeline.AllocateTransientResources( 1, m_pDevice->NextFenceValue() );
	if ( !srv.cpu.ptr )
		return;
	D3D12_SHADER_RESOURCE_VIEW_DESC view{};
	view.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
	view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	view.ViewDimension = nSamples > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
	if ( nSamples == 1 )
		view.Texture2D.MipLevels = 1;
	pNative->CreateShaderResourceView( m_pDevice->SceneDepth(), &view, srv.cpu );
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );
	TransitionMotionTarget( D3D12_RESOURCE_STATE_RENDER_TARGET );
	VMatrix curVP = m_Matrices[MATERIAL_PROJECTION] * m_Matrices[MATERIAL_VIEW], inv, prevVP;
	if ( !MatrixInverseGeneral( curVP, inv ) )
		return;
	if ( m_MotionPrevViewProjValid[m_nMotionPassSlot] )
		memcpy( prevVP.Base(), m_MotionPrevViewProj[m_nMotionPassSlot], sizeof( float ) * 16 );
	else
		prevVP = curVP;
	VMatrix clipToPrev = prevVP * inv;
	float constants[20]{};
	memcpy( constants, clipToPrev.Base(), sizeof( float ) * 16 );
	float flWidth = static_cast<float>( m_nMotionTargetWidth );
	float flHeight = static_cast<float>( m_nMotionTargetHeight );
	float flLeft = 0;
	float flTop = 0;
	if ( m_nViewportCount > 0 )
	{
		flWidth = static_cast<float>( m_Viewports[0].m_nWidth );
		flHeight = static_cast<float>( m_Viewports[0].m_nHeight );
		flLeft = static_cast<float>( m_Viewports[0].m_nTopLeftX );
		flTop = static_cast<float>( m_Viewports[0].m_nTopLeftY );
	}
	constants[16] = flWidth > 0 ? 1.f / flWidth : 0;
	constants[17] = flHeight > 0 ? 1.f / flHeight : 0;
	constants[18] = flLeft;
	constants[19] = flTop;
	D3D12_VIEWPORT viewport{ flLeft, flTop, flWidth, flHeight, 0, 1 };
	D3D12_RECT scissor{ static_cast<LONG>( flLeft ), static_cast<LONG>( flTop ), static_cast<LONG>( flLeft + flWidth ), static_cast<LONG>( flTop + flHeight ) };
	ID3D12DescriptorHeap *pHeap = m_Pipeline.ResourceDescriptorHeap();
	pList->SetDescriptorHeaps( 1, &pHeap );
	pList->RSSetViewports( 1, &viewport );
	pList->RSSetScissorRects( 1, &scissor );
	pList->OMSetRenderTargets( 1, &m_MotionRtv, FALSE, nullptr );
	pList->SetGraphicsRootSignature( m_pMotionReprojectRoot.Get() );
	pList->SetGraphicsRootDescriptorTable( 0, srv.gpu );
	pList->SetGraphicsRoot32BitConstants( 1, 20, constants, 0 );
	pList->SetPipelineState( m_pMotionReprojectPso.Get() );
	pList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
	m_pDevice->GpuReceiverDraw( false );
	pList->DrawInstanced( 3, 1, 0, 0 );
	m_Pipeline.InvalidateGraphicsBindings();
}

//-----------------------------------------------------------------------------
// Purpose: INT_RENDERPARM_DX12_MOTION_PASS handler: begins, appends to or ends
//			a motion pass, suppressing it when its preconditions are not met
//-----------------------------------------------------------------------------
void CShaderAPIDX12::SetMotionPass( int nMode )
{
	const bool bCanRecord = m_pDevice && m_pDevice->IsRecordingOwner() && m_pDevice->CommandList();
	if ( nMode == DX12_MOTION_PASS_END )
	{
		if ( m_MotionPassState == MotionPassStateDX12::None )
			return;
		if ( bCanRecord )
		{
			if ( m_MotionPassState == MotionPassStateDX12::Active )
			{
				FlushBufferedPrimitives();
				ResolveMotionTarget();
			}
			else
			{
				MarkMotionTargetStale();
				++m_nMotionSuppressedPasses;
			}
		}
		if ( MotionLoggingEnabled() && m_nFrameCounter - m_nMotionLogFrame >= 120 )
		{
			m_nMotionLogFrame = m_nFrameCounter;
			Msg( "ShaderAPIDX12 motion: draws %u objects %u suppressed %u\n", m_nMotionPassDraws, m_nMotionPassObjects, m_nMotionSuppressedPasses );
		}
		m_MotionPassState = MotionPassStateDX12::None;
		m_hMotionResolveTarget = 0;
		return;
	}
	if ( m_MotionPassState != MotionPassStateDX12::None )
		SetMotionPass( DX12_MOTION_PASS_END );
	auto fail = [&]( unsigned nBit, const char *pszWhy )
	{
		if ( !( m_nMotionWarned & nBit ) )
		{
			m_nMotionWarned |= static_cast<uint8_t>( nBit );
			Warning( "ShaderAPIDX12: motion pass suppressed: %s\n", pszWhy );
		}
		m_MotionPassState = MotionPassStateDX12::Suppressed;
		// A failed append/viewmodel pass must not advertise its earlier main resolve as current.
		m_hMotionResolvedHandle = 0;
		m_nMotionResolvedFrame = ~0ull;
	};
	if ( !bCanRecord )
	{
		fail( 1, "off the recording thread" );
		return;
	}
	if ( nMode == DX12_MOTION_PASS_APPEND_MAIN && m_nMotionMainFrame != m_nFrameCounter )
	{
		if ( !( m_nMotionWarned & 2 ) )
		{
			m_nMotionWarned |= 2;
			Warning( "ShaderAPIDX12: motion append without a main pass; promoting to begin\n" );
		}
		nMode = DX12_MOTION_PASS_BEGIN_MAIN;
	}
	const bool bViewModel = nMode == DX12_MOTION_PASS_BEGIN_VIEWMODEL || nMode == DX12_MOTION_PASS_HISTORY_VIEWMODEL;
	if ( bViewModel && m_nMotionMainFrame != m_nFrameCounter )
	{
		fail( 4, "viewmodel pass without a main pass this frame" );
		return;
	}
	if ( !EnsureMotionResources() )
	{
		fail( 8, "private resources unavailable" );
		return;
	}
	TextureRecord *pRecord = FindTexture( m_RenderTargets[0] );
	if ( !pRecord || !( pRecord->flags & TEXTURE_CREATE_RENDERTARGET ) || pRecord->format != IMAGE_FORMAT_RGBA16161616F || pRecord->width != static_cast<int>( m_nMotionTargetWidth ) || pRecord->height != static_cast<int>( m_nMotionTargetHeight ) )
	{
		fail( 16, "render target 0 is not a scene-sized RGBA16F render target" );
		return;
	}
	FlushBufferedPrimitives();
	CommitTransforms();
	m_nMotionPassSlot = bViewModel ? 1 : 0;
	if ( nMode == DX12_MOTION_PASS_BEGIN_MAIN &&
		( m_nMotionMainFrame == ~0ull || m_nMotionMainFrame + 1 != m_nFrameCounter ) )
	{
		// No-consumer frames deliberately do not advance motion history. Reactivation starts
		// with current transforms, never a velocity spanning all of the skipped frames.
		// This table becomes the reader after the swap; the writer is cleared below.
		m_MotionHistory[m_nMotionHistoryCurrent].Clear();
		memset( m_MotionCurViewProjValid, 0, sizeof( m_MotionCurViewProjValid ) );
		memset( m_MotionPrevViewProjValid, 0, sizeof( m_MotionPrevViewProjValid ) );
		m_bUpscalerHistoryGap = m_bUpscalerNrHistoryGap = m_bFrameGenHistoryGap = true;
	}
	if ( nMode == DX12_MOTION_PASS_BEGIN_MAIN || bViewModel )
	{
		memcpy( m_MotionPrevViewProj[m_nMotionPassSlot], m_MotionCurViewProj[m_nMotionPassSlot], sizeof( m_MotionPrevViewProj[m_nMotionPassSlot] ) );
		m_MotionPrevViewProjValid[m_nMotionPassSlot] = m_MotionCurViewProjValid[m_nMotionPassSlot];
		memcpy( m_MotionCurViewProj[m_nMotionPassSlot], m_VsFloat[VERTEX_SHADER_VIEWPROJ], sizeof( float ) * 16 );
		m_MotionCurViewProjValid[m_nMotionPassSlot] = true;
	}
	if ( nMode == DX12_MOTION_PASS_HISTORY_VIEWMODEL )
	{
		// Empty viewmodels must still advance their camera VP exactly like BEGIN_VIEWMODEL.
		// Within the supported SDK/installed writer inventory the main resolve is unchanged:
		// no velocity draw or second copy is needed. Arbitrary target writers are unsupported.
		// Leave the pass closed so the caller's END is a no-op; failures above still suppress.
		m_nMotionLastObjectKey = INT_MIN;
		m_nMotionObjectOrdinal = 0;
		return;
	}
	if ( nMode == DX12_MOTION_PASS_BEGIN_MAIN )
	{
		// The frame generator's camera: this pass is always inside the main 3D view.
		memcpy( m_MotionMainView, m_Matrices[MATERIAL_VIEW].Base(), sizeof( m_MotionMainView ) );
		memcpy( m_MotionMainProj, m_Matrices[MATERIAL_PROJECTION].Base(), sizeof( m_MotionMainProj ) );
		m_nMotionMainFrame = m_nFrameCounter;
		m_nMotionHistoryCurrent ^= 1;
		m_MotionHistory[m_nMotionHistoryCurrent].Clear();
		m_nMotionPassDraws = m_nMotionPassObjects = 0;
		m_hMotionResolvedHandle = 0;
		m_nMotionResolvedFrame = ~0ull;
		TransitionMotionTarget( D3D12_RESOURCE_STATE_RENDER_TARGET );
		const float flClear[4] = { 0, 0, 0, 1 };
		m_pDevice->CommandList()->ClearRenderTargetView( m_MotionRtv, flClear, 0, nullptr );
		DrawMotionReprojection();
	}
	m_hMotionResolveTarget = m_RenderTargets[0];
	m_MotionPassState = MotionPassStateDX12::Active;
	m_nMotionLastObjectKey = INT_MIN;
	m_nMotionObjectOrdinal = 0;
}

//-----------------------------------------------------------------------------
// Purpose: Fills the DX12MotionVS block for the next draw, looking up the
//			object's previous-frame bone rows in the motion history
//-----------------------------------------------------------------------------
void CShaderAPIDX12::FillMotionBlock( const VertexBindingDX12 &vertexBinding, CIndexBufferDX12 *pIndexBuffer, size_t nIndexOffset, int nFirstIndex, int nIndexCount )
{
	const int nRows = Clamp( m_nMotionBoneRows, 1, NUM_MODEL_TRANSFORMS );
	const char *pszShaderName = m_ActiveSnapshot.vertexShaderName.c_str();
	const bool bTexTransform = m_ActiveSnapshot.alphaTest &&
	    ( V_strncmp( pszShaderName, "vertexlit_and_unlit_generic", 27 ) == 0 ||
	        V_strncmp( pszShaderName, "lightmappedgeneric", 18 ) == 0 );
	m_MotionBlock.cMotionParams[0] = m_nBoneCount > 0 ? 1.f : 0.f;
	m_MotionBlock.cMotionParams[1] = bTexTransform ? 1.f : 0.f;
	m_MotionBlock.cMotionParams[2] = m_MotionBlock.cMotionParams[3] = 0;
	if ( bTexTransform )
	{
		memcpy( m_MotionBlock.cBaseTexTransform[0], m_VsFloat[VERTEX_SHADER_SHADER_SPECIFIC_CONST_0], sizeof( float ) * 4 );
		memcpy( m_MotionBlock.cBaseTexTransform[1], m_VsFloat[VERTEX_SHADER_SHADER_SPECIFIC_CONST_1], sizeof( float ) * 4 );
	}
	else
	{
		memset( m_MotionBlock.cBaseTexTransform, 0, sizeof( m_MotionBlock.cBaseTexTransform ) );
	}

	const float *pPrevious = m_MotionPrevViewProjValid[m_nMotionPassSlot] ? m_MotionPrevViewProj[m_nMotionPassSlot] : m_MotionCurViewProj[m_nMotionPassSlot];
	memcpy( m_MotionBlock.cPrevViewProj, pPrevious, sizeof( float ) * 16 );
	const float *pCurrent = m_VsFloat[VERTEX_SHADER_MODEL];
	memset( m_MotionBlock.cPrevModel, 0, sizeof( m_MotionBlock.cPrevModel ) );

	if ( m_nMotionObjectKey == 0 )
	{
		memcpy( m_MotionBlock.cPrevModel, pCurrent, sizeof( float ) * nRows * 12 );
	}
	else
	{
		if ( m_nMotionObjectKey != m_nMotionLastObjectKey )
		{
			m_nMotionLastObjectKey = m_nMotionObjectKey;
			m_nMotionObjectOrdinal = 0;
			++m_nMotionPassObjects;
		}
		const uint64_t nMaterialVS = m_hBoundVS == VERTEX_SHADER_HANDLE_INVALID ? 0 : reinterpret_cast<ShaderRecordDX12 *>( m_hBoundVS )->identity;
		uint64 nKey;
		if ( pIndexBuffer && !pIndexBuffer->IsDynamic() )
		{
			const uint64 values[] = { static_cast<uint32_t>( m_nMotionObjectKey ), reinterpret_cast<uintptr_t>( pIndexBuffer ), nIndexOffset, static_cast<uint32_t>( nFirstIndex ), static_cast<uint32_t>( nIndexCount ) };
			nKey = MotionHash( values, ARRAYSIZE( values ) );
		}
		else
		{
			const uint64 values[] = { static_cast<uint32_t>( m_nMotionObjectKey ), nMaterialVS, vertexBinding.vertexCount, static_cast<uint32_t>( nIndexCount ), m_nMotionObjectOrdinal++ };
			nKey = MotionHash( values, ARRAYSIZE( values ) );
		}

		MotionHistoryTableDX12 &write = m_MotionHistory[m_nMotionHistoryCurrent];
		const MotionHistoryTableDX12 &read = m_MotionHistory[m_nMotionHistoryCurrent ^ 1];
		const UtlHashHandle_t hFound = read.entries.Find( nKey );
		const bool bHit = hFound != read.entries.InvalidHandle() &&
		    read.entries[hFound].materialVS == nMaterialVS &&
		    read.entries[hFound].count == static_cast<uint32_t>( nRows ) &&
		    read.entries[hFound].offset + read.entries[hFound].count * 12 <= static_cast<uint32_t>( read.rows.Count() );
		memcpy( m_MotionBlock.cPrevModel, bHit ? read.rows.Base() + read.entries[hFound].offset : pCurrent, sizeof( float ) * nRows * 12 );
		if ( write.entries.Find( nKey ) == write.entries.InvalidHandle() )
		{
			const MotionHistoryEntryDX12 entry{ static_cast<uint32_t>( write.rows.Count() ), static_cast<uint32_t>( nRows ), nMaterialVS };
			write.rows.AddMultipleToTail( nRows * 12, pCurrent );
			write.entries.Insert( nKey, entry );
		}
	}
	++m_nMotionBlockVersion;
	++m_nMotionPassDraws;
}

//-----------------------------------------------------------------------------
// Purpose: Releases every motion resource and resets the pass state
//-----------------------------------------------------------------------------
void CShaderAPIDX12::ReleaseMotionResources()
{
	for ( int i = 0; i < ARRAYSIZE( m_MotionVS ); ++i )
	{
		if ( m_MotionVS[i] )
			RetireShaderPipelines( m_MotionVS[i] );
		delete m_MotionVS[i];
		m_MotionVS[i] = nullptr;
	}
	if ( m_pMotionPS )
	{
		RetireShaderPipelines( m_pMotionPS );
		delete m_pMotionPS;
		m_pMotionPS = nullptr;
	}
	m_pMotionReprojectRoot.Reset();
	m_pMotionReprojectPso.Reset();
	m_nMotionReprojectSamples = 0;
	m_pMotionRtvHeap.Reset();
	m_pMotionTarget.Reset();
	m_MotionPassState = MotionPassStateDX12::None;
	m_bMotionUnavailable = false;
	m_nMotionMainFrame = ~0ull;
	m_hMotionResolvedHandle = 0;
	m_nMotionResolvedFrame = ~0ull;
	m_nMotionWarned = 0;
	m_hMotionResolveTarget = 0;
	m_nMotionTargetSamples = 0;
	m_RenderingInts[INT_RENDERPARM_DX12_MOTION_STATUS] = 0;
	m_MotionHistory[0].Clear();
	m_MotionHistory[1].Clear();
	memset( m_MotionPrevViewProjValid, 0, sizeof( m_MotionPrevViewProjValid ) );
	memset( m_MotionCurViewProjValid, 0, sizeof( m_MotionCurViewProjValid ) );
}

} // namespace shaderapidx12
