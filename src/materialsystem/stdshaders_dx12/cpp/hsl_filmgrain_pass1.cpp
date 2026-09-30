// Native SM5 port of materialsystem/stdshaders/hsl_filmgrain_pass1.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================

#include "BaseVSShaderDX12.h"
#include "convar.h"
#include "filmgrain_vs51.inc"
#include "hsl_filmgrain_pass1_ps51.inc"

//
// First pass converts from RGB to HSL and tweaks with noise similar to After Effects
//

BEGIN_VS_SHADER( hsl_filmgrain_pass1, "Help for Film Grain" )
	BEGIN_SHADER_PARAMS
		// Input textures
		SHADER_PARAM( INPUT, SHADER_PARAM_TYPE_TEXTURE, "", "" )
		SHADER_PARAM( GRAIN, SHADER_PARAM_TYPE_TEXTURE, "", "" )

		// Grain parameters to control positioning and noise params
		SHADER_PARAM( SCALEBIAS, SHADER_PARAM_TYPE_VEC4, "", "Scale and bias for grain placement" )
		SHADER_PARAM( HSLNOISESCALE, SHADER_PARAM_TYPE_VEC4, "", "Strength of film grain" )

	END_SHADER_PARAMS

	SHADER_INIT
	{
		LoadTexture( INPUT );
		LoadTexture( GRAIN );
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( false );
			pShaderShadow->EnableBlending( false );
			pShaderShadow->EnableCulling( false );
//			pShaderShadow->PolyMode( SHADER_POLYMODEFACE_FRONT_AND_BACK, SHADER_POLYMODE_LINE );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			DECLARE_STATIC_VERTEX_SHADER( filmgrain_vs51 );
			SET_STATIC_VERTEX_SHADER( filmgrain_vs51 );

			{
				DECLARE_STATIC_PIXEL_SHADER( hsl_filmgrain_pass1_ps51 );
				SET_STATIC_PIXEL_SHADER( hsl_filmgrain_pass1_ps51 );
			}
		}

		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, INPUT, -1 );
			BindTexture( SHADER_SAMPLER1, GRAIN, -1 );

			SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, SCALEBIAS );

			SetPixelShaderConstant( 0, HSLNOISESCALE );

			DECLARE_DYNAMIC_VERTEX_SHADER( filmgrain_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( filmgrain_vs51 );

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( hsl_filmgrain_pass1_ps51 );
				SET_DYNAMIC_PIXEL_SHADER( hsl_filmgrain_pass1_ps51 );
			}
		}
		Draw();
	}
END_SHADER
