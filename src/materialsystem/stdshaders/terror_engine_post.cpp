//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "convar.h"
#include "screenspaceeffect_vs20.inc"
#include "terror_engine_post_ps20.inc"
#include "terror_engine_post_ps20b.inc"

#include "tier0/memdbgon.h"

static ConVar mat_grain_scale_override( "mat_grain_scale_override", "1.0", FCVAR_ARCHIVE | FCVAR_ARCHIVE_XBOX );
static ConVar mat_local_contrast_scale_override( "mat_local_contrast_scale_override", "0.0" );
static ConVar mat_local_contrast_midtone_mask_override( "mat_local_contrast_midtone_mask_override", "-1.0" );
static ConVar mat_local_contrast_vignette_start_override( "mat_local_contrast_vignette_start_override", "-1.0" );
static ConVar mat_local_contrast_vignette_end_override( "mat_local_contrast_vignette_end_override", "-1.0" );
static ConVar mat_local_contrast_edge_scale_override( "mat_local_contrast_edge_scale_override", "-1000.0" );

// Original stdshader_dx9 SHA256 6a391599...: draw RVA 0xCA40.
// A distinct real shader, selected only by L4D2's native owned material.
// No override of the stock Engine_Post registration or unrelated SDK targets.
BEGIN_VS_SHADER_FLAGS( TerrorEnginePost, "L4D2 EnginePost composition", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "Full framebuffer" )
		SHADER_PARAM( AAENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Retail gameplay AA combo is zero" )
		SHADER_PARAM( AAINTERNAL1, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Native proxy AA values" )
		SHADER_PARAM( AAINTERNAL2, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Bloom to framebuffer UV transform" )
		SHADER_PARAM( AAINTERNAL3, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Native proxy AA values" )
		SHADER_PARAM( BLOOMENABLE, SHADER_PARAM_TYPE_BOOL, "1", "Bloom addition" )
		SHADER_PARAM( BLOOMAMOUNT, SHADER_PARAM_TYPE_FLOAT, "1.0", "Bloom amount" )
		SHADER_PARAM( ALLOWVIGNETTE, SHADER_PARAM_TYPE_BOOL, "0", "Allow vignette" )
		SHADER_PARAM( VIGNETTEENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Enable vignette" )
		SHADER_PARAM( INTERNAL_VIGNETTETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "dev/vignette", "Vignette" )
		SHADER_PARAM( TOPVIGNETTESCALE, SHADER_PARAM_TYPE_FLOAT, "1.0", "Vignette strength" )
		SHADER_PARAM( ALLOWNOISE, SHADER_PARAM_TYPE_BOOL, "0", "Allow grain" )
		SHADER_PARAM( NOISEENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Enable grain" )
		SHADER_PARAM( NOISESCALE, SHADER_PARAM_TYPE_FLOAT, "0", "Grain strength" )
		SHADER_PARAM( NOISETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Noise texture" )
		SHADER_PARAM( ALLOWLOCALCONTRAST, SHADER_PARAM_TYPE_BOOL, "0", "Allow local contrast" )
		SHADER_PARAM( LOCALCONTRASTENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Enable local contrast" )
		SHADER_PARAM( LOCALCONTRASTSCALE, SHADER_PARAM_TYPE_FLOAT, "0", "Local contrast scale" )
		SHADER_PARAM( LOCALCONTRASTMIDTONEMASK, SHADER_PARAM_TYPE_FLOAT, "0", "Retail unused lane" )
		SHADER_PARAM( LOCALCONTRASTVIGNETTESTART, SHADER_PARAM_TYPE_FLOAT, "0", "Radial start" )
		SHADER_PARAM( LOCALCONTRASTVIGNETTEEND, SHADER_PARAM_TYPE_FLOAT, "0", "Radial end" )
		SHADER_PARAM( LOCALCONTRASTEDGESCALE, SHADER_PARAM_TYPE_FLOAT, "0", "Radial edge contrast" )
		SHADER_PARAM( BLURREDVIGNETTEENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Blurred vignette" )
		SHADER_PARAM( BLURREDVIGNETTESCALE, SHADER_PARAM_TYPE_FLOAT, "0", "Blurred vignette strength" )
		SHADER_PARAM( FADETOBLACKSCALE, SHADER_PARAM_TYPE_FLOAT, "0", "Postprocess fade to black" )
		SHADER_PARAM( VOMITCOLOR1, SHADER_PARAM_TYPE_COLOR, "[0 0 0]", "First vomit color" )
		SHADER_PARAM( VOMITCOLOR2, SHADER_PARAM_TYPE_COLOR, "[0 0 0]", "Second vomit color" )
		SHADER_PARAM( VOMITREFRACTSCALE, SHADER_PARAM_TYPE_FLOAT, "0.15", "Vomit refraction" )
		SHADER_PARAM( VOMITENABLE, SHADER_PARAM_TYPE_BOOL, "0", "Actual screen PCF present" )
		SHADER_PARAM( FADECOLOR, SHADER_PARAM_TYPE_VEC4, "[0 0 0 0]", "Original vieweffects RGBA" )
		SHADER_PARAM( FADE, SHADER_PARAM_TYPE_INTEGER, "0", "0 off, 1 lerp, 2 modulate" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_FULL_FRAME_BUFFER_TEXTURE );
		params[INTERNAL_VIGNETTETEXTURE]->SetStringValue( "dev/vignette" );
		const int nZero[] = { AAENABLE, NOISEENABLE, VIGNETTEENABLE,
			LOCALCONTRASTENABLE, BLURREDVIGNETTEENABLE, VOMITENABLE, FADE };
		for ( int i = 0; i < ARRAYSIZE( nZero ); ++i )
			if ( !params[nZero[i]]->IsDefined() ) params[nZero[i]]->SetIntValue( 0 );
		const int nOne[] = { BLOOMENABLE, ALLOWNOISE, ALLOWVIGNETTE, ALLOWLOCALCONTRAST };
		for ( int i = 0; i < ARRAYSIZE( nOne ); ++i )
			if ( !params[nOne[i]]->IsDefined() ) params[nOne[i]]->SetIntValue( 1 );
		INIT_FLOAT_PARM( BLOOMAMOUNT, 1.0f );
		INIT_FLOAT_PARM( NOISESCALE, 1.0f );
		INIT_FLOAT_PARM( TOPVIGNETTESCALE, 1.0f );
		INIT_FLOAT_PARM( LOCALCONTRASTSCALE, 1.0f );
		INIT_FLOAT_PARM( LOCALCONTRASTMIDTONEMASK, 1000.0f );
		INIT_FLOAT_PARM( LOCALCONTRASTVIGNETTESTART, 0.7f );
		INIT_FLOAT_PARM( LOCALCONTRASTVIGNETTEEND, 1.0f );
		INIT_FLOAT_PARM( LOCALCONTRASTEDGESCALE, 0.0f );
		INIT_FLOAT_PARM( BLURREDVIGNETTESCALE, 0.0f );
		INIT_FLOAT_PARM( FADETOBLACKSCALE, 0.0f );
		INIT_FLOAT_PARM( VOMITREFRACTSCALE, 0.15f );
		if ( !params[VOMITCOLOR1]->IsDefined() ) params[VOMITCOLOR1]->SetVecValue( 1.0f, 1.0f, 0.0f );
		if ( !params[VOMITCOLOR2]->IsDefined() ) params[VOMITCOLOR2]->SetVecValue( 0.0f, 1.0f, 0.0f );
	}

	SHADER_FALLBACK
	{
		return NULL;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
		LoadTexture( FBTEXTURE );
		LoadTexture( INTERNAL_VIGNETTETEXTURE );
		if ( params[NOISETEXTURE]->IsDefined() ) LoadTexture( NOISETEXTURE );
	}

	SHADER_DRAW
	{
		const bool bPS2b = g_pHardwareConfig->GetDXSupportLevel() >= 92;
		// DX12 keeps even its tone-mapped framebuffer in linear scRGB, independent of the legacy HDR mode.
		const bool bLinearOutput = !Q_stricmp( g_pHardwareConfig->GetShaderDLLName(), "stdshader_dx12" );
		const bool bConvertFromLinear = bLinearOutput || g_pHardwareConfig->GetHDRType() == HDR_TYPE_FLOAT;
		SHADOW_STATE
		{
			pShaderShadow->EnableBlending( false );
			pShaderShadow->EnableSRGBWrite( false );
			for ( int i = 0; i < 8; ++i )
			{
				pShaderShadow->EnableTexture( (Sampler_t)i, true );
				pShaderShadow->EnableSRGBRead( (Sampler_t)i, false );
			}
			pShaderShadow->VertexShaderVertexFormat( VERTEX_POSITION, 1, NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_STATIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( bPS2b )
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_engine_post_ps20b );
				SET_STATIC_PIXEL_SHADER_COMBO( CONVERT_FROM_LINEAR, bConvertFromLinear );
				SET_STATIC_PIXEL_SHADER_COMBO( LINEAR_OUTPUT, bLinearOutput );
				SET_STATIC_PIXEL_SHADER( terror_engine_post_ps20b );
			}
			else
			{
				DECLARE_STATIC_PIXEL_SHADER( terror_engine_post_ps20 );
				SET_STATIC_PIXEL_SHADER( terror_engine_post_ps20 );
			}
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			BindTexture( SHADER_SAMPLER1, FBTEXTURE, -1 );
			ShaderColorCorrectionInfo_t ccInfo;
			pShaderAPI->GetCurrentColorCorrection( &ccInfo );
			const int nLookups = ccInfo.m_bIsEnabled ? MIN( ccInfo.m_nLookupCount, 3 ) : 0;
			for ( int i = 0; i < nLookups; ++i )
				pShaderAPI->BindStandardTexture( (Sampler_t)(SHADER_SAMPLER2 + i),
					(StandardTextureId_t)(TEXTURE_COLOR_CORRECTION_VOLUME_0 + i) );

			const float *pUV = params[AAINTERNAL2]->GetVecValue();
			float uv[4] = { pUV[0], pUV[1], pUV[3], pUV[2] };
			pShaderAPI->SetPixelShaderConstant( 2, uv );
			pShaderAPI->SetPixelShaderConstant( 3, &ccInfo.m_flDefaultWeight );
			pShaderAPI->SetPixelShaderConstant( 4, ccInfo.m_pLookupWeights );
			float bloom = params[BLOOMENABLE]->GetIntValue() ? params[BLOOMAMOUNT]->GetFloatValue() : 0.0f;
			float value[4] = { bloom, bloom, bloom, bloom };
			pShaderAPI->SetPixelShaderConstant( 5, value );

			bool bVignette = params[ALLOWVIGNETTE]->GetIntValue() && params[VIGNETTEENABLE]->GetIntValue();
			if ( bVignette ) BindTexture( SHADER_SAMPLER7, INTERNAL_VIGNETTETEXTURE, -1 );
			bool bNoise = params[ALLOWNOISE]->GetIntValue() && params[NOISEENABLE]->GetIntValue() &&
				params[FBTEXTURE]->GetTextureValue()->GetActualHeight() >= 720;
			float noise = params[NOISESCALE]->GetFloatValue();
			if ( mat_grain_scale_override.GetFloat() != -1.0f ) noise *= mat_grain_scale_override.GetFloat();
			if ( noise <= 0.0f ) bNoise = false;
			if ( bNoise ) BindTexture( SHADER_SAMPLER6, NOISETEXTURE, -1 );
			value[0] = noise; value[1] = value[2] = value[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 6, value );
			float time = pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_L4D2_CURRENT_TIME );
			value[0] = time - (float)(int)( time * 0.001f ) * 1000.0f;
			pShaderAPI->SetPixelShaderConstant( 7, value );

			const bool bContrast = params[ALLOWLOCALCONTRAST]->GetIntValue() && params[LOCALCONTRASTENABLE]->GetIntValue();
			const bool bBlurredVignette = bContrast && params[BLURREDVIGNETTEENABLE]->GetIntValue();
			value[0] = params[LOCALCONTRASTSCALE]->GetFloatValue();
			value[1] = params[LOCALCONTRASTMIDTONEMASK]->GetFloatValue();
			value[2] = params[BLURREDVIGNETTESCALE]->GetFloatValue(); value[3] = 0.0f;
			if ( mat_local_contrast_scale_override.GetFloat() != 0.0f ) value[0] = mat_local_contrast_scale_override.GetFloat();
			if ( mat_local_contrast_midtone_mask_override.GetFloat() >= 0.0f ) value[1] = mat_local_contrast_midtone_mask_override.GetFloat();
			pShaderAPI->SetPixelShaderConstant( 8, value );
			value[0] = params[LOCALCONTRASTVIGNETTESTART]->GetFloatValue();
			value[1] = params[LOCALCONTRASTVIGNETTEEND]->GetFloatValue();
			value[2] = params[LOCALCONTRASTEDGESCALE]->GetFloatValue();
			if ( mat_local_contrast_vignette_start_override.GetFloat() >= 0.0f ) value[0] = mat_local_contrast_vignette_start_override.GetFloat();
			if ( mat_local_contrast_vignette_end_override.GetFloat() >= 0.0f ) value[1] = mat_local_contrast_vignette_end_override.GetFloat();
			if ( mat_local_contrast_edge_scale_override.GetFloat() >= -1.0f ) value[2] = mat_local_contrast_edge_scale_override.GetFloat();
			pShaderAPI->SetPixelShaderConstant( 9, value );
			value[0] = params[FADETOBLACKSCALE]->GetFloatValue();
			value[1] = params[TOPVIGNETTESCALE]->GetFloatValue(); value[2] = value[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 10, value );

			const bool bVomit = params[VOMITENABLE]->GetIntValue() != 0;
			if ( bVomit )
			{
				const float *pColor = params[VOMITCOLOR1]->GetVecValue();
				value[0] = pColor[0]; value[1] = pColor[1]; value[2] = pColor[2];
				value[3] = params[VOMITREFRACTSCALE]->GetFloatValue();
				pShaderAPI->SetPixelShaderConstant( 11, value );
				pColor = params[VOMITCOLOR2]->GetVecValue();
				value[0] = pColor[0]; value[1] = pColor[1]; value[2] = pColor[2]; value[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 12, value );
			}
			if ( bVomit || bContrast )
			{
				ShaderViewport_t viewport;
				pShaderAPI->GetViewports( &viewport, 1 );
				const int x = viewport.m_nTopLeftX, y = viewport.m_nTopLeftY;
				const int w = viewport.m_nWidth, h = viewport.m_nHeight;
				int backWidth, backHeight;
				pShaderAPI->GetBackBufferDimensions( backWidth, backHeight );
				value[0] = (float)backWidth / w; value[1] = (float)backHeight / h;
				value[2] = -(float)x / w; value[3] = -(float)y / h;
				pShaderAPI->SetPixelShaderConstant( 13, value );
				value[0] = (float)w / backWidth; value[1] = (float)h / backHeight;
				value[2] = (float)x / backWidth; value[3] = (float)y / backHeight;
				pShaderAPI->SetPixelShaderConstant( 14, value );
			}
			float fade[4] = { 0, 0, 0, 0 };
			if ( params[FADE]->GetIntValue() ) params[FADECOLOR]->GetVecValue( fade, 4 );
			pShaderAPI->SetPixelShaderConstant( 15, fade );
			value[0] = 0.0f; value[1] = params[FADE]->GetIntValue() == 2 ? 1.0f : 0.0f;
			value[2] = value[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 16, value );
			DECLARE_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( screenspaceeffect_vs20 );
			if ( bPS2b )
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_engine_post_ps20b );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( COL_CORRECT_NUM_LOOKUPS, nLookups );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( NOISE_ENABLE, bNoise );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( VIGNETTE_ENABLE, bVignette );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( LOCAL_CONTRAST_ENABLE, bContrast );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( BLURRED_VIGNETTE_ENABLE, bBlurredVignette );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( VOMIT_ENABLE, bVomit );
				SET_DYNAMIC_PIXEL_SHADER( terror_engine_post_ps20b );
			}
			else
			{
				DECLARE_DYNAMIC_PIXEL_SHADER( terror_engine_post_ps20 );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( COL_CORRECT_NUM_LOOKUPS, nLookups );
				SET_DYNAMIC_PIXEL_SHADER_COMBO( VOMIT_ENABLE, bVomit );
				SET_DYNAMIC_PIXEL_SHADER( terror_engine_post_ps20 );
			}
		}
		Draw();
	}
END_SHADER
