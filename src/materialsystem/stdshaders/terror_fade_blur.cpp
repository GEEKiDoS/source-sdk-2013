//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "screenspaceeffect_vs20.inc"
#include "fade_blur_ps20.inc"
#include "fade_blur_ps20b.inc"
#include "tier0/memdbgon.h"
BEGIN_VS_SHADER_FLAGS( TerrorFadeBlur, "L4D2 native transition blur composition", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( C0_X, SHADER_PARAM_TYPE_FLOAT, "0", "Native fade alpha" )
		SHADER_PARAM( C1_X, SHADER_PARAM_TYPE_FLOAT, "0", "Native desaturation amount" )
	END_SHADER_PARAMS
	SHADER_FALLBACK { return NULL; }
	SHADER_INIT { LoadTexture( BASETEXTURE ); }
	SHADER_DRAW
	{
		const bool ps2b = g_pHardwareConfig->SupportsPixelShaders_2_b();
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableAlphaWrites( true );
			EnableAlphaBlending( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( ps2b )
			{
				DECLARE_STATIC_PIXEL_SHADER( fade_blur_ps20b );
				SET_STATIC_PIXEL_SHADER( fade_blur_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( fade_blur_ps20 );
				SET_STATIC_PIXEL_SHADER( fade_blur_ps20 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			float constant[4] = { params[C0_X]->GetFloatValue(), 0, 0, 0 };
			pShaderAPI->SetPixelShaderConstant( 0, constant );
			constant[0] = params[C1_X]->GetFloatValue();
			pShaderAPI->SetPixelShaderConstant( 1, constant );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( ps2b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( fade_blur_ps20b );
				SET_DYNAMIC_PIXEL_SHADER( fade_blur_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( fade_blur_ps20 );
				SET_DYNAMIC_PIXEL_SHADER( fade_blur_ps20 );
			}
		}
		Draw();
	}
END_SHADER
