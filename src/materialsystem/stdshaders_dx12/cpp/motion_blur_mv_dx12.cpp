// Native SM5.1 motion-vector motion blur material shader.
// The legacy MotionBlur shader remains unchanged; this material consumes the
// DX12 velocity buffer produced by the backend.
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "BaseVSShaderDX12.h"
#include "motion_blur_vs51.inc"
#include "motion_blur_mv_ps51.inc"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

BEGIN_VS_SHADER_FLAGS( MotionBlurMV, "Motion-vector motion blur", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM_OVERRIDE( BASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "Scene colour texture", 0 )
		SHADER_PARAM( VELOCITYTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_MotionVectors", "Per-pixel motion vectors" )
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

			ITexture *srcTexture = params[BASETEXTURE]->GetTextureValue();
			int textureHeight = srcTexture ? srcTexture->GetActualHeight() : 0;
			int quality = 1;
			if ( textureHeight >= 1080 )
				quality = 3;
			else if ( textureHeight >= 720 )
				quality = 2;

			// The client proxy's .x is the blur multiplier k. Do not spend
			// shader work when the current frame has no requested blur.
			if ( pMotionBlur[0] == 0.0f )
				quality = 0;

			DECLARE_DYNAMIC_PIXEL_SHADER( motion_blur_mv_ps51 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( QUALITY, quality );
			SET_DYNAMIC_PIXEL_SHADER( motion_blur_mv_ps51 );
		}

		Draw();
	}
END_SHADER
