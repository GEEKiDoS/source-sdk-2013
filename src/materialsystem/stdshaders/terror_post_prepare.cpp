//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "convar.h"
#include "screenspaceeffect_vs20.inc"
#include "terror_post_prepare_ps20.inc"
#include "terror_post_prepare_ps20b.inc"

#include "tier0/memdbgon.h"

// Original EnginePost preparation passes 0/1/2: non-HDR downsample,
// kernel-4 X blur and Y blur/neutral RGB. Transition passes 3/4 retain
// native HDR four-tap RGBA and uncleared Y; the same material owner selects them.
BEGIN_VS_SHADER_FLAGS( TerrorPostPrepare, "L4D2 EnginePost render-target preparation", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( PASS, SHADER_PARAM_TYPE_INTEGER, "0", "0 downsample, 1 X blur, 2 Y blur/neutral RGB, 3 HDR downsample, 4 Y blur" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		if ( !params[PASS]->IsDefined() ) params[PASS]->SetIntValue( 0 );
	}

	SHADER_FALLBACK
	{
		return NULL;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}

	SHADER_DRAW
	{
		const int nPass = params[PASS]->GetIntValue();
		const bool bPS2b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableBlending( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( bPS2b )
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_post_prepare_ps20b );
				SET_STATIC_PIXEL_SHADER_COMBO( PASS, nPass );
				SET_STATIC_PIXEL_SHADER( terror_post_prepare_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_post_prepare_ps20 );
				SET_STATIC_PIXEL_SHADER_COMBO( PASS, nPass );
				SET_STATIC_PIXEL_SHADER( terror_post_prepare_ps20 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			float value[4] = { 0, 0, 0, 0 };
			if ( nPass == 0 || nPass == 3 )
			{
				static ConVarRef r_bloomtintr( "r_bloomtintr" );
				static ConVarRef r_bloomtintg( "r_bloomtintg" );
				static ConVarRef r_bloomtintb( "r_bloomtintb" );
				static ConVarRef r_bloomtintexponent( "r_bloomtintexponent" );
				value[0] = r_bloomtintr.GetFloat(); value[1] = r_bloomtintg.GetFloat();
				value[2] = r_bloomtintb.GetFloat(); value[3] = r_bloomtintexponent.GetFloat();
				pShaderAPI->SetPixelShaderConstant( 0, value );
				int width, height;
				pShaderAPI->GetBackBufferDimensions( width, height );
				value[0] = 1.0f / width; value[1] = 1.0f / height;
			}
			else
			{
				ITexture *pTexture = params[BASETEXTURE]->GetTextureValue();
				value[0] = nPass == 1 ? 1.0f / pTexture->GetActualWidth() : 0.0f;
				value[1] = ( nPass == 2 || nPass == 4 ) ? 1.0f / pTexture->GetActualHeight() : 0.0f;
			}
			value[2] = value[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 4, value );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( bPS2b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_post_prepare_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( FLOAT_BACK_BUFFER, nPass == 0 && g_pHardwareConfig->GetHDRType() == HDR_TYPE_FLOAT );
				SET_DYNAMIC_PIXEL_SHADER( terror_post_prepare_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_post_prepare_ps20 );
				SET_DYNAMIC_PIXEL_SHADER( terror_post_prepare_ps20 );
			}
		}
		Draw();
	}
END_SHADER
