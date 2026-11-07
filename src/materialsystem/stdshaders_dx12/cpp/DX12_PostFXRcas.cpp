//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 post-processing sharpen: AMD FidelityFX FSR1 RCAS over $basetexture. $normscale maps the
//          input into 0..1 for the kernel (HDR output peak); $texelsize holds the source texel size.
//
//===========================================================================//
#include "BaseVSShaderDX12.h"
#include "screenspaceeffect_vs51.inc"
#include "postfx_rcas_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( DX12_PostFXRcas, "DX12 post-processing RCAS sharpen" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SHARPNESS, SHADER_PARAM_TYPE_FLOAT, "0", "Sharpness 0..1" )
		SHADER_PARAM( NORMSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "Input normalization scale" )
		SHADER_PARAM( TEXELSIZE, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Source texel size in x/y" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, nullptr, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_STATIC_PIXEL_SHADER( postfx_rcas_ps51 );
			SET_STATIC_PIXEL_SHADER( postfx_rcas_ps51 );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			// FSR1: sharpness 1 -> 0 stops of attenuation, 0 -> 2 stops.
			const float *pTexel = params[TEXELSIZE]->GetVecValue();
			const float constants[4] = { 2.0f * ( 1.0f - params[SHARPNESS]->GetFloatValue() ), params[NORMSCALE]->GetFloatValue(), pTexel[0], pTexel[1] };
			DX12SetPixelShaderConstant( 0, constants );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs51 );
			DECLARE_DYNAMIC_PIXEL_SHADER( postfx_rcas_ps51 );
			SET_DYNAMIC_PIXEL_SHADER( postfx_rcas_ps51 );
		}
		Draw();
	}
END_SHADER
