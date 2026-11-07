//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "convar.h"
#include <emmintrin.h>
#include "screenspaceeffect_vs20.inc"
#include "terror_motion_blur_ps20.inc"
#include "terror_motion_blur_ps20b.inc"
#include "tier0/memdbgon.h"

// Native shader C22AE0..22DCF, 6a3915991277cec4. A real L4D2-owned shader;
// the SDK MotionBlur registration and HL2MP material remain unchanged.
#pragma float_control( precise, on, push )
BEGIN_VS_SHADER_FLAGS( TerrorMotionBlur, "L4D2 native motion blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( MOTIONBLURINTERNAL, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Internal motion blur value set by proxy" )
		SHADER_PARAM( MOTIONBLURVIEWPORTINTERNAL, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Internal motion blur value set by proxy" )
	END_SHADER_PARAMS
	SHADER_FALLBACK { return NULL; }
	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() ) LoadTexture( BASETEXTURE );
	}
	SHADER_DRAW
	{
		const bool ps2b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		SHADOW_STATE
		{
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( ps2b )
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_motion_blur_ps20b );
				SET_STATIC_PIXEL_SHADER( terror_motion_blur_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_motion_blur_ps20 );
				SET_STATIC_PIXEL_SHADER( terror_motion_blur_ps20 );
			}
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
		}
		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			BindTexture( SHADER_SAMPLER0, BASETEXTURE );
			// Reuse the target's genuine stock shader cvar owner, not another
			// registration or a copied per-material setting.
			static ConVarRef maximumPercent( "mat_motion_blur_percent_of_screen_max" );
			const float maximum = maximumPercent.GetFloat() * 0.01f;
			const float *viewport = params[MOTIONBLURVIEWPORTINTERNAL]->GetVecValue();
			float limit[4] = { maximum, 0, 0, 0 };
			if ( viewport[0] > 0.0f || viewport[1] > 0.0f || 1.0f > viewport[2] || 1.0f > viewport[3] )
				limit[0] *= 0.25f;
			pShaderAPI->SetPixelShaderConstant( 0, limit );
			const float *blur = params[MOTIONBLURINTERNAL]->GetVecValue();
			pShaderAPI->SetPixelShaderConstant( 1, blur );
			pShaderAPI->SetPixelShaderConstant( 2, viewport );
			const float amount = (float)( ( ( fabs( (double)blur[1] ) + fabs( (double)blur[0] ) ) +
				fabs( (double)blur[2] ) ) + fabs( (double)blur[3] ) );
			const int height = params[BASETEXTURE]->GetTextureValue()->GetActualHeight();
			const int maximumQuality = height >= 1080 ? 14 : height >= 720 ? 10 : 6;
			// Native 10022CC4 re-reads the cvar after c0/c1/c2 publication.
			const float qualityMaximum = maximumPercent.GetFloat() * 0.01f;
			const float denominator = 4.0f > qualityMaximum ? qualityMaximum : 4.0f;
			float fraction = ( amount * 2.0f ) / denominator;
			if ( fraction > 1.0f ) fraction = 1.0f;
			else if ( 0.0f > fraction ) fraction = 0.0f;
			int quality = _mm_cvttss_si32( _mm_set_ss( (float)maximumQuality * fraction ) );
			if ( quality < 1 ) quality = 1;
			if ( amount == 0.0f ) quality = 0;
			if ( quality > 14 ) quality = 14;
			if ( ps2b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_motion_blur_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, quality );
				SET_DYNAMIC_PIXEL_SHADER( terror_motion_blur_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_motion_blur_ps20 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, quality );
				SET_DYNAMIC_PIXEL_SHADER( terror_motion_blur_ps20 );
			}
		}
		Draw();
	}
END_SHADER
#pragma float_control( pop )
