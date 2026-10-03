//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 GTAO compute passes. $pass selects 0 depth prefilter, 1 full-resolution view depth,
//          2 main pass, 3 denoise. Scene depth is bound by the backend; $depthmips, $viewdepth, $aoin,
//          $aoout and $edges are client render targets. The vectors fill cbuffer GtaoParams
//          (gtao_common.hlsli); $viewport is the size the pass works at.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "dx12_compute_material_common.h"
#include "gtao_prefilter_cs51.inc"
#include "gtao_viewdepth_cs51.inc"
#include "gtao_main_cs51.inc"
#include "gtao_denoise_cs51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
// Mirrors cbuffer GtaoParams : register( b0, space1 ) in gtao_common.hlsli.
struct GtaoConstantsDX12
{
	int32 viewportSize[2];
	float viewportPixelSize[2];
	float depthUnpackConsts[2];
	float cameraTanHalfFOV[2];
	float ndcToViewMul[2];
	float ndcToViewAdd[2];
	float ndcToViewMulXPixelSize[2];
	float effectRadius, effectFalloffRange, radiusMultiplier, finalValuePower;
	float denoiseBlurBeta, sampleDistributionPower, thinOccluderCompensation, depthMIPSamplingOffset;
	int32 noiseIndex, sliceCount, stepsPerSlice, denoiseFinal;
	int32 sceneOrigin[2];
	float depthScale[2];
};
static_assert( sizeof( GtaoConstantsDX12 ) == 30 * sizeof( float ), "GTAO cbuffer layout mismatch" );

const int kGtaoDepthMipLevels = 5;

int GroupCount( int nSize, int nPixelsPerGroup )
{
	return ( nSize + nPixelsPerGroup - 1 ) / nPixelsPerGroup;
}
}

BEGIN_VS_SHADER( DX12_GTAO, "DX12 GTAO compute passes" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "0 prefilter, 1 view depth, 2 main, 3 denoise" )
		SHADER_PARAM( DEPTHMIPS, SHADER_PARAM_TYPE_TEXTURE, "", "GTAO view-depth mip chain" )
		SHADER_PARAM( VIEWDEPTH, SHADER_PARAM_TYPE_TEXTURE, "", "Full-resolution view depth" )
		SHADER_PARAM( AOIN, SHADER_PARAM_TYPE_TEXTURE, "", "AO term input (denoise)" )
		SHADER_PARAM( AOOUT, SHADER_PARAM_TYPE_TEXTURE, "", "AO term output" )
		SHADER_PARAM( EDGES, SHADER_PARAM_TYPE_TEXTURE, "", "Packed depth edges" )
		SHADER_PARAM( VIEWPORT, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "Width, height, 1/width, 1/height" )
		SHADER_PARAM( DEPTHUNPACK, SHADER_PARAM_TYPE_VEC4, "[0 1 0 0]", "NDC depth to view depth" )
		SHADER_PARAM( CAMERATANHFOV, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Camera tan half FOV" )
		SHADER_PARAM( NDCTOVIEWMUL, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "UV to view multiplier" )
		SHADER_PARAM( NDCTOVIEWADD, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "UV to view offset" )
		SHADER_PARAM( NDCTOVIEWMULXPIXELSIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "UV to view multiplier times pixel size" )
		SHADER_PARAM( EFFECT, SHADER_PARAM_TYPE_VEC4, "[24 .615 1.457 2.2]", "Radius, falloff range, radius multiplier, power" )
		SHADER_PARAM( DENOISE, SHADER_PARAM_TYPE_VEC4, "[1.2 2 0 3.3]", "Blur beta, distribution power, thin occluder, mip offset" )
		SHADER_PARAM( NOISE, SHADER_PARAM_TYPE_VEC4, "[0 3 3 0]", "Noise index, slices, steps, final denoise" )
		SHADER_PARAM( SCENEORIGIN, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Viewport origin in scene pixels" )
		SHADER_PARAM( DEPTHSCALE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Scene pixels per working pixel" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
	}

	SHADER_DRAW
	{
		DYNAMIC_STATE
		{
			const float *pViewport = params[VIEWPORT]->GetVecValue();
			const float *pDepthUnpack = params[DEPTHUNPACK]->GetVecValue();
			const float *pCameraTan = params[CAMERATANHFOV]->GetVecValue();
			const float *pMul = params[NDCTOVIEWMUL]->GetVecValue();
			const float *pAdd = params[NDCTOVIEWADD]->GetVecValue();
			const float *pMulPixel = params[NDCTOVIEWMULXPIXELSIZE]->GetVecValue();
			const float *pEffect = params[EFFECT]->GetVecValue();
			const float *pDenoise = params[DENOISE]->GetVecValue();
			const float *pNoise = params[NOISE]->GetVecValue();
			const float *pOrigin = params[SCENEORIGIN]->GetVecValue();
			const float *pScale = params[DEPTHSCALE]->GetVecValue();

			GtaoConstantsDX12 constants;
			constants.viewportSize[0] = static_cast<int32>( pViewport[0] );
			constants.viewportSize[1] = static_cast<int32>( pViewport[1] );
			constants.viewportPixelSize[0] = pViewport[2];
			constants.viewportPixelSize[1] = pViewport[3];
			constants.depthUnpackConsts[0] = pDepthUnpack[0];
			constants.depthUnpackConsts[1] = pDepthUnpack[1];
			constants.cameraTanHalfFOV[0] = pCameraTan[0];
			constants.cameraTanHalfFOV[1] = pCameraTan[1];
			constants.ndcToViewMul[0] = pMul[0];
			constants.ndcToViewMul[1] = pMul[1];
			constants.ndcToViewAdd[0] = pAdd[0];
			constants.ndcToViewAdd[1] = pAdd[1];
			constants.ndcToViewMulXPixelSize[0] = pMulPixel[0];
			constants.ndcToViewMulXPixelSize[1] = pMulPixel[1];
			constants.effectRadius = pEffect[0];
			constants.effectFalloffRange = pEffect[1];
			constants.radiusMultiplier = pEffect[2];
			constants.finalValuePower = pEffect[3];
			constants.denoiseBlurBeta = pDenoise[0];
			constants.sampleDistributionPower = pDenoise[1];
			constants.thinOccluderCompensation = pDenoise[2];
			constants.depthMIPSamplingOffset = pDenoise[3];
			constants.noiseIndex = static_cast<int32>( pNoise[0] );
			constants.sliceCount = static_cast<int32>( pNoise[1] );
			constants.stepsPerSlice = static_cast<int32>( pNoise[2] );
			constants.denoiseFinal = static_cast<int32>( pNoise[3] );
			constants.sceneOrigin[0] = static_cast<int32>( pOrigin[0] );
			constants.sceneOrigin[1] = static_cast<int32>( pOrigin[1] );
			constants.depthScale[0] = pScale[0];
			constants.depthScale[1] = pScale[1];

			IShaderAPIDX12Compute *pCompute = DX12ComputeAPI();
			const int nMSAA = pCompute->SceneSampleCount() > 1 ? 1 : 0;
			ShaderAPIDX12ComputeDispatch_t dispatch = {};
			dispatch.m_pConstants = &constants;
			dispatch.m_nConstantBytes = sizeof( constants );
			dispatch.m_nGroupsZ = 1;

			switch ( params[PASS]->GetIntValue() )
			{
			case 0:
			{
				// Each thread filters a 2x2 quad, so an 8x8 group covers 16x16 pixels.
				gtao_prefilter_cs51_Static_Index staticIndex;
				staticIndex.SetMSAA( nMSAA );
				gtao_prefilter_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pShaderName = "gtao_prefilter_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialSceneDepth();
				const ShaderAPITextureHandle_t hDepthMips = GetShaderAPITextureBindHandle( DEPTHMIPS, -1 );
				for ( int nMip = 0; nMip < kGtaoDepthMipLevels; ++nMip )
					dispatch.m_Uav[nMip] = DX12MaterialTexture( hDepthMips, nMip );
				dispatch.m_nGroupsX = GroupCount( constants.viewportSize[0], 16 );
				dispatch.m_nGroupsY = GroupCount( constants.viewportSize[1], 16 );
				break;
			}
			case 1:
			{
				gtao_viewdepth_cs51_Static_Index staticIndex;
				staticIndex.SetMSAA( nMSAA );
				gtao_viewdepth_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pShaderName = "gtao_viewdepth_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialSceneDepth();
				dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( VIEWDEPTH, -1 ) );
				dispatch.m_nGroupsX = GroupCount( constants.viewportSize[0], 8 );
				dispatch.m_nGroupsY = GroupCount( constants.viewportSize[1], 8 );
				break;
			}
			case 2:
			{
				gtao_main_cs51_Static_Index staticIndex;
				gtao_main_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pShaderName = "gtao_main_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( DEPTHMIPS, -1 ), 0, kGtaoDepthMipLevels );
				dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( AOOUT, -1 ) );
				dispatch.m_Uav[1] = DX12MaterialTexture( GetShaderAPITextureBindHandle( EDGES, -1 ) );
				dispatch.m_nGroupsX = GroupCount( constants.viewportSize[0], 8 );
				dispatch.m_nGroupsY = GroupCount( constants.viewportSize[1], 8 );
				break;
			}
			default:
			{
				// Two horizontal pixels per thread.
				gtao_denoise_cs51_Static_Index staticIndex;
				staticIndex.SetFINAL( constants.denoiseFinal != 0 );
				gtao_denoise_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pShaderName = "gtao_denoise_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( AOIN, -1 ) );
				dispatch.m_Srv[1] = DX12MaterialTexture( GetShaderAPITextureBindHandle( EDGES, -1 ) );
				dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( AOOUT, -1 ) );
				dispatch.m_nGroupsX = GroupCount( constants.viewportSize[0], 16 );
				dispatch.m_nGroupsY = GroupCount( constants.viewportSize[1], 8 );
				break;
			}
			}
			pCompute->Dispatch( dispatch );
		}
		Draw( false );
	}
END_SHADER
