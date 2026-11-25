// Flashlight bounce, recording owner. Finish ALL measurements before binding RT0 for addition.
#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"

namespace shaderapidx12
{
namespace
{

struct BounceMeasureConstants
{
	float clipToWorld[16], worldToClip[16];
	float worldToLight[16], originFar[4], colorConst[4], attenuation[4];
	float viewport[4], eyeExposure[4], settings[4];
};

void BindBounceTexture( CPipelineCacheDX12::BindingInputDX12 &input, int slot, ID3D12Resource *resource,
    DXGI_FORMAT format, bool multisampled, UINT plane = 0 )
{
	input.textures[slot] = resource;
	input.srvSources[slot] = {};
	D3D12_SHADER_RESOURCE_VIEW_DESC &srv = input.srvDescs[slot];
	srv = {};
	srv.Format = format;
	srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srv.ViewDimension = multisampled ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
	if ( !multisampled )
	{
		srv.Texture2D.MipLevels = 1;
		srv.Texture2D.PlaneSlice = plane;
	}
}
} // namespace

void CShaderAPIDX12::MeasureBounce()
{
	DX12ProjectedLightDesc spots[DX12_PBR_RESERVED_FLASHLIGHTS];
	ShaderAPITextureHandle_t cookies[DX12_PBR_RESERVED_FLASHLIGHTS];
	const int count = m_pDevice->Lighting().GetBounceSpots( spots, cookies );
	if ( !count )
		return;
	const auto &camera = m_ScreenSpace.constants;
	const D3D12_RESOURCE_STATES before = m_pDevice->SceneColorState();
	m_pDevice->BeginGpuPIXEvent( "Flashlight bounce measure" );
	m_pDevice->TransitionSceneColor( D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE );
	for ( int i = 0; i < count; ++i )
	{
		BounceSlotDX12 &slot = m_BounceSlots[i];
		const int write = slot.current ^ 1;
		if ( !EnsureSSRTexture( slot.history[write], 16, 5, IMAGE_FORMAT_RGBA32323232F, false, "DX12_BounceMeasurement" ) ||
		    !EnsureSSRTexture( slot.history[slot.current], 16, 5, IMAGE_FORMAT_RGBA32323232F, false, "DX12_BounceHistory" ) )
			continue;
		BounceMeasureConstants cb{};
		memcpy( cb.clipToWorld, camera.clipToWorld, sizeof( cb.clipToWorld ) );
		memcpy( cb.worldToClip, camera.worldToClip, sizeof( cb.worldToClip ) );
		memcpy( cb.worldToLight, spots[i].worldToTexture, sizeof( cb.worldToLight ) );
		memcpy( cb.originFar, spots[i].origin, sizeof( cb.originFar ) );
		memcpy( cb.colorConst, spots[i].color, sizeof( cb.colorConst ) );
		cb.attenuation[0] = spots[i].linearAttn;
		cb.attenuation[1] = spots[i].quadraticAttn;
		memcpy( cb.viewport, camera.viewport, sizeof( cb.viewport ) );
		memcpy( cb.eyeExposure, camera.eyeLength, 3 * sizeof( float ) );
		cb.eyeExposure[3] = MAX( 0.01f, m_ToneScale.x );
		cb.settings[0] = m_flBounceScale;
		cb.settings[1] = slot.identity == spots[i].flags && slot.frame + 1 == m_nFrameCounter;
		cb.settings[2] = camera.depth[0];
		cb.settings[3] = m_bBounceDebug;
		ShaderAPIDX12ComputeDispatch_t dispatch{};
		dispatch.m_pShaderName = "flashlight_bounce_cs51";
		dispatch.m_nStaticIndex = camera.control[1] > 1;
		dispatch.m_pConstants = &cb;
		dispatch.m_nConstantBytes = sizeof( cb );
		dispatch.m_nGroupsX = dispatch.m_nGroupsY = 4;
		dispatch.m_nGroupsZ = 1;
		dispatch.m_Srv[0].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_COLOR;
		dispatch.m_Srv[1].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH;
		dispatch.m_Srv[2].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS;
		dispatch.m_Srv[3].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL;
		dispatch.m_Srv[4] = { SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE_SRGB, cookies[i] };
		dispatch.m_Srv[5] = { SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE, slot.history[slot.current].handle };
		dispatch.m_Srv[6].m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_PBR_ALBEDO;
		dispatch.m_Uav[0] = { SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE, slot.history[write].handle };
		if ( Dispatch( dispatch ) )
		{
			slot.identity = spots[i].flags;
			slot.frame = m_nFrameCounter;
			slot.current = write;
		}
	}
	m_pDevice->TransitionSceneColor( before );
	m_pDevice->EndGpuPIXEvent();
}

void CShaderAPIDX12::ApplyBounce( bool viewModel )
{
	if ( m_nBounceFrame != m_nFrameCounter || !m_flBounceScale || !m_pDevice || !m_pDevice->CommandList() )
		return;
	bool active = false;
	for ( const auto &slot : m_BounceSlots )
		active |= slot.frame == m_nFrameCounter;
	if ( !active )
		return;
	FlushBufferedPrimitives();
	RenderTargetBindingDX12 target{};
	if ( !PrepareRenderTargets( target, false ) || target.color != m_pDevice->SceneColor() || target.depth != m_pDevice->SceneDepth() )
		return;
	DX12ScreenSpaceViewConstants camera = m_ScreenSpace.constants;
	if ( viewModel )
	{
		if ( !m_nViewportCount || m_Viewports[0].m_flMaxZ != 0.1f )
			return;
		VMatrix forward, inverse;
		MatrixMultiply( m_Matrices[MATERIAL_PROJECTION], m_Matrices[MATERIAL_VIEW], forward );
		const bool jitter = UpscalerJitterActive( false );
		const float x = ( 1.f + ( jitter ? 2.f * m_UpscalerJitter[0] : 0.f ) ) / camera.viewport[2];
		const float y = ( 1.f + ( jitter ? 2.f * m_UpscalerJitter[1] : 0.f ) ) / camera.viewport[3];
		for ( int i = 0; i < 4; ++i )
		{
			forward[0][i] += x * forward[3][i];
			forward[1][i] -= y * forward[3][i];
		}
		if ( !MatrixInverseGeneral( forward, inverse ) )
			return;
		// Preserve the world projection for sampling scene depth during the visibility trace.
		memcpy( camera.clipToWorld, inverse.Base(), sizeof( camera.clipToWorld ) );
	}
	// A private copy of the camera block: these unused contact lanes carry scene fog, independent of GTAO.
	const float invRange = m_flFogEnd != m_flFogStart ? 1.f / ( m_flFogEnd - m_flFogStart ) : 1.f;
	const float density = m_FogMode == MATERIAL_FOG_NONE ? 0.f : clamp( m_flFogMaxDensity, 0.f, 1.f );
	camera.control[0] = !ShouldUsePixelFog();
	camera.control[2] = GetPixelFogCombo1( true );
	camera.eyeLength[3] = MAX( 0.01f, m_ToneScale.x );
	camera.contactParams[1] = m_flFogEnd;
	camera.contactParams[2] = invRange;
	camera.contactParams[3] = density;
	camera.contactDepth[0] = m_flFogStart * invRange;
	camera.contactDepth[1] = m_flFogZ;
	camera.contactDepth[2] = density;
	camera.contactDepth[3] = invRange;
	// Convert device Z to homogeneous W along the main perspective view: z = A + B / W.
	const float *zRow = camera.worldToClip + 8, *wRow = camera.worldToClip + 12;
	const float norm = wRow[0] * wRow[0] + wRow[1] * wRow[1] + wRow[2] * wRow[2];
	camera.depth[1] = norm > 0.f ? ( zRow[0] * wRow[0] + zRow[1] * wRow[1] + zRow[2] * wRow[2] ) / norm : 0.f;
	camera.depth[2] = zRow[3] - camera.depth[1] * wRow[3];
	const bool msaa = target.sampleCount > 1;
	auto *vs = ResolveNamedShader( "flashlight_bounce_vs51", false, viewModel, 0 );
	auto *ps = ResolveNamedShader( "flashlight_bounce_ps51", true, msaa, 0 );
	if ( !ps || !vs )
		return;
	const uint64_t fence = m_pDevice->NextFenceValue();
	PipelineKeyDX12 key{};
	key.vs = vs->identity;
	key.ps = ps->identity;
	key.vsVariant = vs->activeVariantKey;
	key.psVariant = ps->activeVariantKey;
	key.topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	key.color = key.colorFormats[0] = target.colorFormat;
	key.colorCount = 1;
	key.depth = target.depthFormat;
	key.samples = target.sampleCount;
	key.sampleQuality = target.sampleQuality;
	key.blend = 1;
	key.blendSource = SHADER_BLEND_ONE;
	key.blendDestination = kBlendSecondSourceColor;
	key.separateAlpha = true;
	key.blendAlphaSource = SHADER_BLEND_ZERO;
	key.blendAlphaDestination = SHADER_BLEND_ONE;
	key.blendOperation = SHADER_BLEND_OP_ADD;
	key.depthWrite = key.culling = key.stencil = false;
	// Test the compressed hand depth range in hardware before paying for the pixel shader.
	key.depthTest = true;
	key.depthFunction = viewModel ? SHADER_DEPTHFUNC_FARTHER : SHADER_DEPTHFUNC_NEAREROREQUAL;
	key.colorWrites = true;
	key.alphaWrites = false;
	const D3D12_INPUT_LAYOUT_DESC layout{};
	auto *pso = m_Pipeline.CapturePipeline( key, vs->Bytecode(), ps->Bytecode(), {}, layout, fence )->Resolve();
	CPipelineCacheDX12::BindingInputDX12 input( m_Pipeline.NullShaderResourceView() );
	if ( !pso || !m_Pipeline.UploadTransient( &camera, sizeof( camera ), 512, 256, fence, input.screenSpaceView ) )
		return;
	input.retireFence = fence;
	input.vertexTextures = true;
	input.geometryStage = false;
	for ( auto &sampler : input.samplerDescs )
		sampler = CPipelineCacheDX12::DefaultSamplerDesc();
	input.samplerTables = m_Pipeline.PrepareSamplerTables( input.samplerDescs, input.samplerIds, fence );
	if ( !input.samplerTables.Valid() )
		return;
	BindBounceTexture( input, 0, target.depth, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, msaa );
	BindBounceTexture( input, 1, GBufferTarget( 0 ), GBufferFormat( 0 ), msaa );
	BindBounceTexture( input, 2, target.depth, DXGI_FORMAT_X24_TYPELESS_G8_UINT, msaa, 1 );
	BindBounceTexture( input, 3, GBufferTarget( kGBufferAlbedoTarget ), GBufferFormat( kGBufferAlbedoTarget ), msaa );
	const D3D12_VIEWPORT viewport{ camera.viewport[0], camera.viewport[1], camera.viewport[2], camera.viewport[3], 0.f, 1.f };
	const D3D12_RECT scissor{ LONG( viewport.TopLeftX ), LONG( viewport.TopLeftY ), LONG( viewport.TopLeftX + viewport.Width ), LONG( viewport.TopLeftY + viewport.Height ) };
	const auto readOnlyDSV = m_pDevice->SceneReadOnlyDSV();
	const auto before = m_pDevice->SceneDepthState();
	m_pDevice->BeginGpuPIXEvent( viewModel ? "Flashlight bounce hands" : "Flashlight bounce apply" );
	m_pDevice->TransitionSceneDepth( D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_DEPTH_READ );
	auto *list = m_pDevice->CommandList();
	for ( auto &slot : m_BounceSlots )
	{
		if ( slot.frame != m_nFrameCounter )
			continue;
		TextureRecord *record = FindTexture( slot.history[slot.current].handle );
		ID3D12Resource *resource = nullptr;
		D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
		D3D12_SAMPLER_DESC sampler{};
		if ( !record || !PrepareSampledTexture( slot.history[slot.current].handle, false, &resource, srv, sampler ) )
			continue;
		BindBounceTexture( input, 16, resource, srv.Format, false );
		if ( !m_Pipeline.PrepareBindings( list, input ) )
			continue;
		m_Pipeline.BindDrawState( list, viewport, scissor, fence );
		m_Pipeline.BindRenderTargets( list, 1, &target.rtv, &target.color, &readOnlyDSV, target.depth, fence );
		m_Pipeline.BindPipelineState( list, pso, 0, fence );
		list->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP );
		list->DrawInstanced( 4, 1, 0, 0 );
	}
	m_pDevice->TransitionSceneDepth( before );
	m_Pipeline.InvalidateGraphicsBindings();
	m_pDevice->EndGpuPIXEvent();
}

void CShaderAPIDX12::ReleaseBounce()
{
	for ( auto &slot : m_BounceSlots )
	{
		for ( auto &texture : slot.history )
			if ( texture.handle )
				DeleteTexture( texture.handle );
		slot = {};
	}
	m_nBounceFrame = ~0ull;
}
} // namespace shaderapidx12
