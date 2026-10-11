// Native SM5.1 motion-vector motion blur material shader.
// The legacy MotionBlur shader remains unchanged; this material consumes the
// DX12 velocity buffer produced by the backend.
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "BaseVSShaderDX12.h"
#include "motion_blur_vs51.inc"
#include "motion_blur_mv_ps51.inc"
#include "dx12_compute_material_common.h"
#include "motion_blur_tile_cs51.inc"
#include "motion_blur_tile_dilate_cs51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar mat_motion_blur_quality( "mat_motion_blur_quality", "2", FCVAR_ARCHIVE, "DX12 velocity motion blur gather: 0 low (8 taps), 1 medium (12), 2 high (16). High is the 1080p gather.", true, 0.f, true, 2.f );

BEGIN_VS_SHADER_FLAGS( MotionBlurMV, "Motion-vector motion blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM_OVERRIDE( BASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "Scene colour texture", 0 )
		SHADER_PARAM( VELOCITYTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_MotionVectors", "Per-pixel motion vectors" )
		SHADER_PARAM( TILETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_MotionBlurTiles", "Dilated per-tile maximum velocity length" )
		SHADER_PARAM( MOTIONBLURINTERNAL, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Internal motion-vector blur values set by proxy" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
		{
			LoadTexture( BASETEXTURE );
		}
		if ( params[VELOCITYTEXTURE]->IsDefined() )
		{
			LoadTexture( VELOCITYTEXTURE );
		}
		if ( params[TILETEXTURE]->IsDefined() )
		{
			LoadTexture( TILETEXTURE );
		}
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			// Both inputs are linear data. The final output follows the legacy
			// motion blur path: no forced sRGB read or write conversion.
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, false );
			pShaderShadow->EnableSRGBWrite( false );

			DECLARE_STATIC_VERTEX_SHADER( motion_blur_vs51 );
			SET_STATIC_VERTEX_SHADER( motion_blur_vs51 );

			DECLARE_STATIC_PIXEL_SHADER( motion_blur_mv_ps51 );
			SET_STATIC_PIXEL_SHADER( motion_blur_mv_ps51 );

			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
		}

		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( motion_blur_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( motion_blur_vs51 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE );
			BindTexture( SHADER_SAMPLER1, VELOCITYTEXTURE );

			const float *pMotionBlur = params[MOTIONBLURINTERNAL]->GetVecValue();
			DX12SetPixelShaderConstant( 0, pMotionBlur, 1 );

			// 0..2 → 8/12/16 taps. Default high is the 1080p gather this shader used before the cvar.
			int quality = Clamp( mat_motion_blur_quality.GetInt(), 0, 2 ) + 1;
			if ( pMotionBlur[0] == 0.0f )
				quality = 0;
			const int nTiles = ( quality != 0 && pMotionBlur[2] > 0.0f ) ? 1 : 0;
			if ( nTiles )
				BindTexture( SHADER_SAMPLER2, TILETEXTURE );

			DECLARE_DYNAMIC_PIXEL_SHADER( motion_blur_mv_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, quality );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( TILE, nTiles );
			SET_DYNAMIC_PIXEL_SHADER( motion_blur_mv_ps51 );
		}

		Draw();
	}
END_SHADER

namespace
{
struct MotionTileReduceConstantsDX12
{
	uint32 m_nVelocitySize[2];
	uint32 m_nTileCount[2];
};
struct MotionTileDilateConstantsDX12
{
	uint32 m_nVelocitySize[2];
	uint32 m_nTileCount[2];
	float m_flSearchRadius;
	float m_flPad[3];
};
static_assert( sizeof( MotionTileReduceConstantsDX12 ) == 16, "motion tile reduce cbuffer" );
static_assert( sizeof( MotionTileDilateConstantsDX12 ) == 32, "motion tile dilate cbuffer" );

int TileGroupCount( int nSize, int nThreads )
{
	return ( nSize + nThreads - 1 ) / nThreads;
}
}

BEGIN_VS_SHADER( DX12_MotionBlurTile, "DX12 motion-blur tile classification" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "0 reduce 16x16, 1 dilate" )
		SHADER_PARAM( VELOCITY, SHADER_PARAM_TYPE_TEXTURE, "", "Per-pixel motion vectors" )
		SHADER_PARAM( TILEIN, SHADER_PARAM_TYPE_TEXTURE, "", "Raw tile maxima" )
		SHADER_PARAM( TILEOUT, SHADER_PARAM_TYPE_TEXTURE, "", "Tile output" )
		SHADER_PARAM( SIZES, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "Velocity width/height and tile count" )
		SHADER_PARAM( RADIUS, SHADER_PARAM_TYPE_FLOAT, "0", "Gather search radius in UV" )
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
			const float *pSizes = params[SIZES]->GetVecValue();
			const int nPass = params[PASS]->GetIntValue();
			const int nTileX = MAX( static_cast<int>( pSizes[2] ), 1 );
			const int nTileY = MAX( static_cast<int>( pSizes[3] ), 1 );
			ShaderAPIDX12ComputeDispatch_t dispatch = {};
			dispatch.m_nGroupsZ = 1;
			if ( nPass == 0 )
			{
				MotionTileReduceConstantsDX12 constants = {};
				constants.m_nVelocitySize[0] = static_cast<uint32>( pSizes[0] );
				constants.m_nVelocitySize[1] = static_cast<uint32>( pSizes[1] );
				constants.m_nTileCount[0] = static_cast<uint32>( nTileX );
				constants.m_nTileCount[1] = static_cast<uint32>( nTileY );
				motion_blur_tile_cs51_Static_Index staticIndex;
				motion_blur_tile_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pConstants = &constants;
				dispatch.m_nConstantBytes = sizeof( constants );
				dispatch.m_pShaderName = "motion_blur_tile_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( VELOCITY, -1 ) );
				dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( TILEOUT, -1 ) );
				dispatch.m_nGroupsX = nTileX;
				dispatch.m_nGroupsY = nTileY;
				DX12ComputeAPI()->Dispatch( dispatch );
			}
			else
			{
				MotionTileDilateConstantsDX12 constants = {};
				constants.m_nVelocitySize[0] = static_cast<uint32>( pSizes[0] );
				constants.m_nVelocitySize[1] = static_cast<uint32>( pSizes[1] );
				constants.m_nTileCount[0] = static_cast<uint32>( nTileX );
				constants.m_nTileCount[1] = static_cast<uint32>( nTileY );
				constants.m_flSearchRadius = params[RADIUS]->GetFloatValue();
				motion_blur_tile_dilate_cs51_Static_Index staticIndex;
				motion_blur_tile_dilate_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pConstants = &constants;
				dispatch.m_nConstantBytes = sizeof( constants );
				dispatch.m_pShaderName = "motion_blur_tile_dilate_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				dispatch.m_Srv[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( TILEIN, -1 ) );
				dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( TILEOUT, -1 ) );
				dispatch.m_nGroupsX = TileGroupCount( nTileX, 8 );
				dispatch.m_nGroupsY = TileGroupCount( nTileY, 8 );
				DX12ComputeAPI()->Dispatch( dispatch );
			}
		}
		Draw( false );
	}
END_SHADER
