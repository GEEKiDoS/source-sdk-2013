//========= Copyright Valve Corporation, All rights reserved. ============//
// L4D2's post-TAA halo: native colour/blend with spatial interior coverage.
#include "BaseVSShaderDX12.h"
#include "screenspaceeffect_vs51.inc"
#include "l4d2_glow_composite_ps51.inc"
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DX12_GlowComposite, "L4D2 spatially antialiased halo" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( C0_X, SHADER_PARAM_TYPE_FLOAT, "0", "Native dim value" )
		SHADER_PARAM( C0_Y, SHADER_PARAM_TYPE_FLOAT, "0", "Native unused constant" )
		SHADER_PARAM( ALPHA_BLEND_COLOR_OVERLAY, SHADER_PARAM_TYPE_INTEGER, "0", "Native overlay blend" )
		SHADER_PARAM( LINEARREAD_BASETEXTURE, SHADER_PARAM_TYPE_INTEGER, "0", "Native linear read" )
		SHADER_PARAM( LINEARWRITE, SHADER_PARAM_TYPE_INTEGER, "0", "Native linear write" )
		SHADER_PARAM( COVERAGETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Full-frame R8 exterior coverage" )
		SHADER_PARAM( COVERAGESIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Inverse full-frame size" )
		SHADER_PARAM( MASKWRITE, SHADER_PARAM_TYPE_INTEGER, "0", "Write zero to stencil-selected interior" )
	END_SHADER_PARAMS
	SHADER_FALLBACK { return NULL; }
	SHADER_INIT
	{
		if ( !params[MASKWRITE]->GetIntValue() )
		{
			LoadTexture( BASETEXTURE );
			LoadTexture( COVERAGETEXTURE );
		}
	}
	SHADER_DRAW
	{
		const bool maskWrite = params[MASKWRITE]->GetIntValue() != 0;
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableCulling( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, !maskWrite );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, !maskWrite );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, !maskWrite && params[LINEARREAD_BASETEXTURE]->GetIntValue() == 0 );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
			pShaderShadow->EnableSRGBWrite( !maskWrite && params[LINEARWRITE]->GetFloatValue() == 0.0f );
			int texCoordSize = 2;
			pShaderShadow->EnableAlphaTest( !maskWrite );
			pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GREATER, 0.0f );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, &texCoordSize, 0 );
			pShaderShadow->EnableBlending( false );
			if ( !maskWrite )
			{
				if ( IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) ) EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
				if ( params[ALPHA_BLEND_COLOR_OVERLAY]->GetIntValue() )
					EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			}
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_STATIC_PIXEL_SHADER( l4d2_glow_composite_ps51 );
			SET_STATIC_PIXEL_SHADER( l4d2_glow_composite_ps51 );
		}
		DYNAMIC_STATE
		{
			if ( !maskWrite )
			{
				BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
				BindTexture( SHADER_SAMPLER1, COVERAGETEXTURE, -1 );
			}
			const float *size = params[COVERAGESIZE]->GetVecValue();
			const float constants[4] = { params[C0_X]->GetFloatValue(), maskWrite ? 1.0f : 0.0f, size[0], size[1] };
			DX12SetPixelShaderConstant( 0, constants );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_glow_composite_ps51 );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_glow_composite_ps51 );
		}
		Draw();
	}
END_SHADER
