//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 post-processing compute passes (Froyok's custom bloom and lens flare). $pass selects
//          0 bloom downsample, 1 bloom upsample-combine, 2 lens flare threshold, 3 lens flare ghosts and halo,
//          4 dual Kawase down, 5 dual Kawase up, 6 glare. $srctexture/$srctexture2 are read at $srcmip/$srcmip2 and
//          $dsttexture is written at $dstmip. $sizes holds the source and destination size, $params0 the source
//          texel size and the per-pass parameters p0/p1.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "dx12_compute_material_common.h"
#include "postfx_downsample_cs51.inc"
#include "postfx_upsample_cs51.inc"
#include "postfx_flare_cs51.inc"
#include "postfx_kawase_down_cs51.inc"
#include "postfx_kawase_up_cs51.inc"
#include "postfx_glare_cs51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
// Mirrors cbuffer PostConstants : register( b0, space1 ) in postfx_common.hlsli.
struct PostConstantsDX12
{
	uint32 srcWidth, srcHeight, dstWidth, dstHeight;
	float texelX, texelY, p0, p1;
};
static_assert( sizeof( PostConstantsDX12 ) == 8 * sizeof( float ), "PostFX cbuffer layout mismatch" );

const int kPostThreadGroupSize = 8;

template < class StaticIndex, class DynamicIndex >
void SelectShader( ShaderAPIDX12ComputeDispatch_t &dispatch, const char *pName )
{
	StaticIndex staticIndex;
	DynamicIndex dynamicIndex;
	dispatch.m_pShaderName = pName;
	dispatch.m_nStaticIndex = staticIndex.GetIndex();
	dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
}
}

BEGIN_VS_SHADER( DX12_PostFX, "DX12 post-processing compute passes" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "PostFX pass" )
		SHADER_PARAM( SRCTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Source texture" )
		SHADER_PARAM( SRCTEXTURE2, SHADER_PARAM_TYPE_TEXTURE, "", "Previous bloom level (upsample-combine)" )
		SHADER_PARAM( DSTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Destination texture" )
		SHADER_PARAM( SRCMIP, SHADER_PARAM_TYPE_INTEGER, "0", "Source mip" )
		SHADER_PARAM( SRCMIP2, SHADER_PARAM_TYPE_INTEGER, "0", "Previous bloom level mip" )
		SHADER_PARAM( DSTMIP, SHADER_PARAM_TYPE_INTEGER, "0", "Destination mip" )
		SHADER_PARAM( SIZES, SHADER_PARAM_TYPE_VEC4, "[1 1 1 1]", "Source and destination dimensions" )
		SHADER_PARAM( PARAMS0, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Source texel size and p0/p1" )
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
			const int nPass = params[PASS]->GetIntValue();
			const float *pSizes = params[SIZES]->GetVecValue();
			const float *pParams0 = params[PARAMS0]->GetVecValue();

			PostConstantsDX12 constants = {};
			constants.srcWidth = static_cast<uint32>( pSizes[0] );
			constants.srcHeight = static_cast<uint32>( pSizes[1] );
			constants.dstWidth = static_cast<uint32>( pSizes[2] );
			constants.dstHeight = static_cast<uint32>( pSizes[3] );
			constants.texelX = pParams0[0];
			constants.texelY = pParams0[1];
			constants.p0 = pParams0[2];
			constants.p1 = pParams0[3];

			ShaderAPIDX12ComputeDispatch_t dispatch = {};
			dispatch.m_pConstants = &constants;
			dispatch.m_nConstantBytes = sizeof( constants );
			dispatch.m_nGroupsX = ( constants.dstWidth + kPostThreadGroupSize - 1 ) / kPostThreadGroupSize;
			dispatch.m_nGroupsY = ( constants.dstHeight + kPostThreadGroupSize - 1 ) / kPostThreadGroupSize;
			dispatch.m_nGroupsZ = 1;
			dispatch.m_Srv[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( SRCTEXTURE, -1 ), params[SRCMIP]->GetIntValue(), 1 );
			dispatch.m_Uav[0] = DX12MaterialTexture( GetShaderAPITextureBindHandle( DSTTEXTURE, -1 ), params[DSTMIP]->GetIntValue() );

			switch ( nPass )
			{
			case 0:
			case 2:
			{
				postfx_downsample_cs51_Static_Index staticIndex;
				staticIndex.SetTHRESHOLD( nPass == 2 );
				postfx_downsample_cs51_Dynamic_Index dynamicIndex;
				dispatch.m_pShaderName = "postfx_downsample_cs51";
				dispatch.m_nStaticIndex = staticIndex.GetIndex();
				dispatch.m_nDynamicIndex = dynamicIndex.GetIndex();
				break;
			}
			case 1:
				SelectShader< postfx_upsample_cs51_Static_Index, postfx_upsample_cs51_Dynamic_Index >( dispatch, "postfx_upsample_cs51" );
				dispatch.m_Srv[1] = DX12MaterialTexture( GetShaderAPITextureBindHandle( SRCTEXTURE2, -1 ), params[SRCMIP2]->GetIntValue(), 1 );
				break;
			case 3:
				SelectShader< postfx_flare_cs51_Static_Index, postfx_flare_cs51_Dynamic_Index >( dispatch, "postfx_flare_cs51" );
				break;
			case 4:
				SelectShader< postfx_kawase_down_cs51_Static_Index, postfx_kawase_down_cs51_Dynamic_Index >( dispatch, "postfx_kawase_down_cs51" );
				break;
			case 5:
				SelectShader< postfx_kawase_up_cs51_Static_Index, postfx_kawase_up_cs51_Dynamic_Index >( dispatch, "postfx_kawase_up_cs51" );
				break;
			default:
				SelectShader< postfx_glare_cs51_Static_Index, postfx_glare_cs51_Dynamic_Index >( dispatch, "postfx_glare_cs51" );
				break;
			}
			DX12ComputeAPI()->Dispatch( dispatch );
		}
		Draw( false );
	}
END_SHADER
