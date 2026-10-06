// Native SM5 port of materialsystem/stdshaders/cable_dx9.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: A wet version of base * lightmap
//
// $Header: $
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "lightmappedgeneric_dx9_helper.h"

#include "cable_vs51.inc"
#include "cable_ps51.inc"
#include "cpp_shader_constant_register_map.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar mat_fullbright;

DEFINE_FALLBACK_SHADER( Cable, Cable_DX9 )

BEGIN_VS_SHADER( Cable_DX9, 
			  "Help for Cable shader" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "cable/cablenormalmap", "bumpmap texture" )
		SHADER_PARAM( MINLIGHT, SHADER_PARAM_TYPE_FLOAT, "0.1", "Minimum amount of light (0-1 value)" )
		SHADER_PARAM( MAXLIGHT, SHADER_PARAM_TYPE_FLOAT, "0.3", "Maximum amount of light" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadBumpMap( BUMPMAP );
		LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
	}

	SHADER_DRAW
	{
		bool bShadowmapReceiver = false;
		if ( !DX12ShadowmapReceiverSnapshot( pContextDataPtr, pShaderShadow, "Cable", bShadowmapReceiver ) )
			return;
		BlendType_t nBlendType = EvaluateBlendRequirements( BASETEXTURE, true );
		bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !IS_FLAG_SET(MATERIAL_VAR_ALPHATEST); //dest alpha is free for special use

		SHADOW_STATE
		{
			// Enable blending?
			if ( IS_FLAG_SET( MATERIAL_VAR_TRANSLUCENT ) )
			{
				pShaderShadow->EnableDepthWrites( false );
				pShaderShadow->EnableBlending( true );
				pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			}

			pShaderShadow->EnableAlphaTest( IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			{
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
			}
			
			int tCoordDimensions[] = {2,2};
			pShaderShadow->VertexShaderVertexFormat( 
				VERTEX_POSITION | VERTEX_COLOR | VERTEX_TANGENT_S | VERTEX_TANGENT_T | ( bShadowmapReceiver ? VERTEX_NORMAL : 0 ),
				2, tCoordDimensions, 0 );

			DECLARE_STATIC_VERTEX_SHADER( cable_vs51 );
			DX12_SET_STATIC_VERTEX_SHADER( cable_vs51, cable_shadowmap_vs51 );

			{
				DECLARE_STATIC_PIXEL_SHADER( cable_ps51 );
				DX12_SET_STATIC_PIXEL_SHADER( cable_ps51, cable_shadowmap_ps51 );
			}

			// we are writing linear values from this shader.
			// This is kinda wrong.  We are writing linear or gamma depending on "IsHDREnabled" below.
			// The COLOR really decides if we are gamma or linear.  
			pShaderShadow->EnableSRGBWrite( true );

			FogToFogColor();

			pShaderShadow->EnableAlphaWrites( bFullyOpaque );
		}
		DYNAMIC_STATE
		{
			bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE );

			BindTexture( SHADER_SAMPLER0, BUMPMAP );
			if ( bLightingOnly )
			{
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER1, TEXTURE_GREY );

			}
			else
			{
				BindTexture( SHADER_SAMPLER1, BASETEXTURE );			
			}

			pShaderAPI->SetPixelShaderFogParams( PSREG_FOG_PARAMS );		

			float vEyePos_SpecExponent[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEyePos_SpecExponent );
			vEyePos_SpecExponent[3] = 0.0f;
			DX12SetPixelShaderConstant( PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1 );

			DECLARE_DYNAMIC_VERTEX_SHADER( cable_vs51 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG, pShaderAPI->GetSceneFogMode() == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
			DX12_SET_DYNAMIC_VERTEX_SHADER( cable_vs51, cable_shadowmap_vs51 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( cable_ps51 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo1( true ) );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, bFullyOpaque && pShaderAPI->ShouldWriteDepthToDestAlpha() );
				DX12_SET_DYNAMIC_PIXEL_SHADER( cable_ps51, cable_shadowmap_ps51 );
			}
		}
		Draw();
	}
END_SHADER
