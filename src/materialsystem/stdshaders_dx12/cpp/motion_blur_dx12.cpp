// Native SM5 port of materialsystem/stdshaders/motion_blur_dx9.cpp (cport.py + review): constants stage into the
// legacy register file of BaseVSShaderDX12; hardware-config branches are resolved for the DX12 config.
//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShaderDX12.h"
#include "motion_blur_vs51.inc"
#include "motion_blur_ps51.inc"
#include "convar.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar mat_motion_blur_percent_of_screen_max( "mat_motion_blur_percent_of_screen_max", "4.0" );

DEFINE_FALLBACK_SHADER( MotionBlur, MotionBlur_dx9 )
BEGIN_VS_SHADER_FLAGS( MotionBlur_dx9, "Motion Blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( MOTIONBLURINTERNAL, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Internal motion blur value set by proxy" )
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
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, 0, 0 );

			const bool bForceSRGBReadsAndWrites = false;
			
			// NOTE: sRGB is disabled because of the NV8800 brokenness
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, bForceSRGBReadsAndWrites );
			pShaderShadow->EnableSRGBWrite( bForceSRGBReadsAndWrites );

			DECLARE_STATIC_VERTEX_SHADER( motion_blur_vs51 );
			SET_STATIC_VERTEX_SHADER( motion_blur_vs51 );

			{
				DECLARE_STATIC_PIXEL_SHADER( motion_blur_ps51 );
				SET_STATIC_PIXEL_SHADER( motion_blur_ps51 );
			}

			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( false );
		}

		DYNAMIC_STATE
		{
			DECLARE_DYNAMIC_VERTEX_SHADER( motion_blur_vs51 );
			SET_DYNAMIC_VERTEX_SHADER( motion_blur_vs51 );

			// Bind textures
			BindTexture( SHADER_SAMPLER0, BASETEXTURE );

			// Get texture dimensions
			ITexture *src_texture = params[BASETEXTURE]->GetTextureValue();
			//int flTextureWidth = src_texture->GetActualWidth();
			int flTextureHeight = src_texture->GetActualHeight();

			// Percent of screen clamp
			float vConst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			vConst[0] = mat_motion_blur_percent_of_screen_max.GetFloat() / 100.0f;
			DX12SetPixelShaderConstant( 0, vConst, 1 );

			// Set values from material proxy
			DX12SetPixelShaderConstant( 1, params[MOTIONBLURINTERNAL]->GetVecValue(), 1 );

			// Quality based on screen resolution height
			int nQuality = 1;
			if ( flTextureHeight >= 1080 ) // 1080p and higher
				nQuality = 3;
			else if ( flTextureHeight >= 720 ) // 720p to 1080p
				nQuality = 2;
			else // Lower resolution than 720p
				nQuality = 1;

			if ( fabs( params[MOTIONBLURINTERNAL]->GetVecValue()[0] ) + fabs( params[MOTIONBLURINTERNAL]->GetVecValue()[1] ) +
				 fabs( params[MOTIONBLURINTERNAL]->GetVecValue()[2] ) + fabs( params[MOTIONBLURINTERNAL]->GetVecValue()[3] ) == 0.0f )
			{
				// No motion blur this frame, so force quality to 0
				nQuality = 0;
			}

			{
				DECLARE_DYNAMIC_PIXEL_SHADER( motion_blur_ps51 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, nQuality );
				SET_DYNAMIC_PIXEL_SHADER( motion_blur_ps51 );
			}
		}

		Draw();
	}
END_SHADER
