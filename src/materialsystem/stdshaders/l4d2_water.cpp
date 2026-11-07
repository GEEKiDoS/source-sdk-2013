//========= Copyright Valve Corporation, All rights reserved. ============//
// Native L4D2 Water owner. CPU: 100420F0/10042560/10042650/10043320/10044640.
// GPU: original Water/WaterCheap VS20 and PS2b contracts, not SDK Water's
// fifth-power reflection/refraction lerp. Original materials stay mounted.
#include "BaseVSShader.h"
#include "shaderapi/ishaderapi.h"
#include "mathlib/vmatrix.h"
#include "tier0/icommandline.h"
#include "l4d2_water_vs20.inc"
#include "l4d2_water_ps20b.inc"

DEFINE_FALLBACK_SHADER( Water, Water_DX9_HDR )

BEGIN_VS_SHADER( Water_DX90, "L4D2 flow water" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( REFRACT, SHADER_PARAM_TYPE_INTEGER, "0", "Render refraction" )
		SHADER_PARAM( REFLECTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_WaterReflection", "" )
		SHADER_PARAM( REFRACTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0", "" )
		SHADER_PARAM( REFRACTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Refraction tint" )
		SHADER_PARAM( REFLECTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0.8", "" )
		SHADER_PARAM( REFLECTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Reflection tint" )
		SHADER_PARAM( NORMALMAP, SHADER_PARAM_TYPE_TEXTURE, "dev/water_normal", "Normal map" )
		SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "" )
		SHADER_PARAM( TIME, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( WATERDEPTH, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( CHEAPWATERSTARTDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( CHEAPWATERENDDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "env_cubemap", "" )
		SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( FOGCOLOR, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( FORCECHEAP, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTENTITIES, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( FOGSTART, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FOGEND, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( ABOVEWATER, SHADER_PARAM_TYPE_BOOL, "", "" )
		SHADER_PARAM( REFLECTBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( NOFRESNEL, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( NOLOWENDLIGHTMAP, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( SCROLL1, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( SCROLL2, SHADER_PARAM_TYPE_COLOR, "", "" )
		SHADER_PARAM( FLOWMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Flow map" )
		SHADER_PARAM( FLOWMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "" )
		SHADER_PARAM( FLOWMAPSCROLLRATE, SHADER_PARAM_TYPE_VEC2, "[0 0]", "" )
		SHADER_PARAM( FLOW_NOISE_TEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Flow noise" )
		SHADER_PARAM( FLOW_WORLDUVSCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_NORMALUVSCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_TIMEINTERVALINSECONDS, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_UVSCROLLDISTANCE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_BUMPSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_TIMESCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_NOISE_SCALE, SHADER_PARAM_TYPE_FLOAT, "", "" )
		SHADER_PARAM( FLOW_DEBUG, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( FLASHLIGHTTINT, SHADER_PARAM_TYPE_FLOAT, "0", "" )
		SHADER_PARAM( LIGHTMAPWATERFOG, SHADER_PARAM_TYPE_BOOL, "0", "" )
		// Native material-system/view controls must remain queryable by the
		// SDK host's game-owned water view, not become missing dummy vars.
		SHADER_PARAM( FOGENABLE, SHADER_PARAM_TYPE_BOOL, "", "Water volume fog" )
		SHADER_PARAM( REFLECTSKYBOXONLY, SHADER_PARAM_TYPE_BOOL, "0", "" )
		SHADER_PARAM( REFLECT2DSKYBOX, SHADER_PARAM_TYPE_BOOL, "1", "" )
		SHADER_PARAM( BOTTOMMATERIAL, SHADER_PARAM_TYPE_STRING, "", "Underwater material" )
		SHADER_PARAM( REFRACTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		params[REFRACTTEXTURE]->SetStringValue( g_pHardwareConfig->GetDXSupportLevel() < 92 ?
			"_rt_WaterRefraction" : (CommandLine()->FindParm( "-tools" ) ? "_rt_PowerOfTwoFB" : "_rt_FullFrameFB") );
		if ( !params[ABOVEWATER]->IsDefined() )
		{
			Warning( "***need to set $abovewater for material %s\n", pMaterialName );
			params[ABOVEWATER]->SetIntValue( 1 );
		}
		SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_FLASHLIGHT );
		if ( !params[CHEAPWATERSTARTDISTANCE]->IsDefined() )
			params[CHEAPWATERSTARTDISTANCE]->SetFloatValue( 500.0f );
		if ( !params[CHEAPWATERENDDISTANCE]->IsDefined() )
			params[CHEAPWATERENDDISTANCE]->SetFloatValue( 1000.0f );
		if ( !params[SCROLL1]->IsDefined() )
			params[SCROLL1]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[SCROLL2]->IsDefined() )
			params[SCROLL2]->SetVecValue( 0.0f, 0.0f, 0.0f );
		if ( !params[FOGCOLOR]->IsDefined() )
		{
			params[FOGCOLOR]->SetVecValue( 1.0f, 0.0f, 0.0f );
			Warning( "material %s needs to have a $fogcolor.\n", pMaterialName );
		}
		if ( !params[REFLECTENTITIES]->IsDefined() )
			params[REFLECTENTITIES]->SetIntValue( 0 );
		if ( !params[REFLECTBLENDFACTOR]->IsDefined() )
			params[REFLECTBLENDFACTOR]->SetFloatValue( 1.0f );
		if ( !params[FLOW_WORLDUVSCALE]->IsDefined() )
			params[FLOW_WORLDUVSCALE]->SetFloatValue( 1.0f );
		if ( !params[FLOW_NORMALUVSCALE]->IsDefined() )
			params[FLOW_NORMALUVSCALE]->SetFloatValue( 1.0f );
		if ( !params[FLOW_TIMEINTERVALINSECONDS]->IsDefined() )
			params[FLOW_TIMEINTERVALINSECONDS]->SetFloatValue( 0.4f );
		if ( !params[FLOW_UVSCROLLDISTANCE]->IsDefined() )
			params[FLOW_UVSCROLLDISTANCE]->SetFloatValue( 0.2f );
		if ( !params[FLOW_BUMPSTRENGTH]->IsDefined() )
			params[FLOW_BUMPSTRENGTH]->SetFloatValue( 1.0f );
		if ( !params[FLOW_TIMESCALE]->IsDefined() )
			params[FLOW_TIMESCALE]->SetFloatValue( 1.0f );
		if ( !params[FLOW_NOISE_SCALE]->IsDefined() )
			params[FLOW_NOISE_SCALE]->SetFloatValue( 0.0002f );
		if ( !params[FORCECHEAP]->IsDefined() )
			params[FORCECHEAP]->SetIntValue( 0 );
		if ( !params[FLASHLIGHTTINT]->IsDefined() )
			params[FLASHLIGHTTINT]->SetFloatValue( 1.0f );
		if ( !params[LIGHTMAPWATERFOG]->IsDefined() )
			params[LIGHTMAPWATERFOG]->SetIntValue( 0 );
		if ( !params[REFRACT]->IsDefined() )
			params[REFRACT]->SetIntValue( 0 );
		if ( params[BASETEXTURE]->IsDefined() && g_pConfig->UseBumpmapping() && params[NORMALMAP]->IsDefined() )
			SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
	}

	SHADER_FALLBACK
	{
		return g_pHardwareConfig->GetDXSupportLevel() < 92 ? "Water_DX81" : NULL;
	}

	SHADER_INIT
	{
		if ( params[REFLECTTEXTURE]->IsDefined() )
			LoadTexture( REFLECTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[ENVMAP]->IsDefined() )
			LoadCubeMap( ENVMAP, TEXTUREFLAGS_SRGB );
		if ( params[NORMALMAP]->IsDefined() )
			LoadBumpMap( NORMALMAP );
		if ( params[BASETEXTURE]->IsDefined() )
			LoadTexture( BASETEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[FLOWMAP]->IsDefined() )
			LoadTexture( FLOWMAP );
		if ( params[FLOW_NOISE_TEXTURE]->IsDefined() )
			LoadTexture( FLOW_NOISE_TEXTURE );
		if ( params[REFRACT]->GetIntValue() )
			LoadTexture( REFRACTTEXTURE, TEXTUREFLAGS_SRGB );
	}

	void SetScrollOffsets( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI )
	{
		float time = pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_L4D2_CURRENT_TIME );
		const float *pScroll1 = params[SCROLL1]->GetVecValue();
		const float *pScroll2 = params[SCROLL2]->GetVecValue();
		float scroll[4] = { time * pScroll1[0], time * pScroll1[1],
			time * pScroll2[0], time * pScroll2[1] };
		pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_3, scroll );
	}

	SHADER_DRAW
	{
		bool bRefract = params[REFRACT]->GetIntValue() != 0;
		bool bReflect = params[REFLECTTEXTURE]->IsTexture();
		bool bCheap = (!bReflect && !bRefract) || UsingEditor( params ) || params[FORCECHEAP]->GetIntValue() != 0;
		bool bFlow = params[FLOWMAP]->IsTexture();
		bool bBase = !bCheap && !bFlow && params[BASETEXTURE]->IsTexture();
		bool bMulti = !bFlow && !bBase && fabs( params[SCROLL1]->GetVecValue()[0] ) > 0.0f;
		bool bLightmapFog = !bCheap && !bBase && !bMulti && params[LIGHTMAPWATERFOG]->GetIntValue() != 0;
		bool bFlashlight = UsingFlashlight( params );
		if ( bFlashlight && (bCheap || bBase || bMulti) )
		{
			// These native branches have no flashlight contribution.
			Draw( false );
			return;
		}
		int nDebug = bFlow ? clamp( params[FLOW_DEBUG]->GetIntValue(), 0, 2 ) : 0;
		if ( bFlashlight && nDebug )
		{
			Draw( false );
			return;
		}
		int nNormalDecode = params[NORMALMAP]->IsTexture() ? params[NORMALMAP]->GetTextureValue()->GetNormalDecodeMode() : NORMAL_DECODE_NONE;
		SHADOW_STATE
		{
			SetInitialShadowState();
			if ( bFlashlight )
			{
				EnableAlphaBlending( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
				pShaderShadow->EnableDepthWrites( false );
				FogToBlack();
			}
			else
				FogToFogColor();
			pShaderShadow->EnableAlphaWrites( !bCheap && !bFlashlight );
			pShaderShadow->EnableSRGBWrite( !bCheap || g_pHardwareConfig->GetHDRType() != HDR_TYPE_NONE );
			if ( UsingEditor( params ) )
				pShaderShadow->EnableCulling( false );
			if ( bCheap || bRefract )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, !bCheap );
			}
			if ( !bCheap && bReflect && !bFlashlight )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
			}
			pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
			if ( bBase || bLightmapFog )
				pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
			if ( bBase || bFlow )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER4, bBase );
			}
			if ( bFlow )
				pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			if ( bCheap || bFlashlight )
				pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			if ( bFlashlight )
			{
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER6, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
				pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER7 );
				pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
			}
			int format = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_TANGENT_T;
			pShaderShadow->VertexShaderVertexFormat( format, bBase ? 3 : (bLightmapFog ? 2 : 1), NULL, 0 );
			DECLARE_STATIC_VERTEX_SHADER( l4d2_water_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( CHEAP, bCheap );
			SET_STATIC_VERTEX_SHADER_COMBO( BASETEXTURE, bBase );
			SET_STATIC_VERTEX_SHADER_COMBO( MULTITEXTURE, bMulti );
			SET_STATIC_VERTEX_SHADER_COMBO( FLASHLIGHT, bFlashlight );
			SET_STATIC_VERTEX_SHADER_COMBO( LIGHTMAPWATERFOG, bLightmapFog );
			SET_STATIC_VERTEX_SHADER( l4d2_water_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( l4d2_water_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( CHEAP, bCheap );
			SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE, bBase );
			SET_STATIC_PIXEL_SHADER_COMBO( MULTITEXTURE, bMulti );
			SET_STATIC_PIXEL_SHADER_COMBO( REFLECT, !bCheap && bReflect );
			SET_STATIC_PIXEL_SHADER_COMBO( REFRACT, !bCheap && bRefract );
			SET_STATIC_PIXEL_SHADER_COMBO( ABOVEWATER, bCheap || params[ABOVEWATER]->GetIntValue() != 0 );
			SET_STATIC_PIXEL_SHADER_COMBO( FLOWMAP, bFlow );
			SET_STATIC_PIXEL_SHADER_COMBO( FLOW_DEBUG, nDebug );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, bFlashlight );
			SET_STATIC_PIXEL_SHADER_COMBO( LIGHTMAPWATERFOG, bLightmapFog );
			SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, nNormalDecode );
			SET_STATIC_PIXEL_SHADER_COMBO( FRESNEL, !bCheap || params[NOFRESNEL]->GetIntValue() == 0 );
			SET_STATIC_PIXEL_SHADER( l4d2_water_ps20b );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetDefaultState();
			if ( bCheap )
			{
				BindTexture( SHADER_SAMPLER0, ENVMAP, ENVMAPFRAME );
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_NORMALIZATION_CUBEMAP_SIGNED );
			}
			else
			{
				if ( bRefract )
					BindTexture( SHADER_SAMPLER0, REFRACTTEXTURE );
				if ( bReflect && !bFlashlight )
					BindTexture( SHADER_SAMPLER1, REFLECTTEXTURE );
			}
			BindTexture( SHADER_SAMPLER2, NORMALMAP, BUMPFRAME );
			if ( bBase )
				BindTexture( SHADER_SAMPLER4, BASETEXTURE, FRAME );
			if ( bBase || bLightmapFog )
				pShaderAPI->BindStandardTexture( SHADER_SAMPLER3, TEXTURE_LIGHTMAP );
			if ( bFlow )
			{
				BindTexture( SHADER_SAMPLER4, FLOWMAP, bCheap ? -1 : FLOWMAPFRAME );
				BindTexture( SHADER_SAMPLER5, FLOW_NOISE_TEXTURE );
				float flow1[4] = { 1.0f / params[FLOW_WORLDUVSCALE]->GetFloatValue(),
					1.0f / params[FLOW_NORMALUVSCALE]->GetFloatValue(),
					params[FLOW_BUMPSTRENGTH]->GetFloatValue(), params[FLOW_TIMESCALE]->GetFloatValue() };
				float flow2[4] = { params[FLOW_TIMEINTERVALINSECONDS]->GetFloatValue(),
					params[FLOW_UVSCROLLDISTANCE]->GetFloatValue(), params[FLOW_NOISE_SCALE]->GetFloatValue(), 0.0f };
				pShaderAPI->SetPixelShaderConstant( 13, flow1 );
				pShaderAPI->SetPixelShaderConstant( 14, flow2 );
			}
			if ( !bCheap || bFlow )
			{
				float timeConstant[4] = { pShaderAPI->GetFloatRenderingParameter( FLOAT_RENDERPARM_L4D2_CURRENT_TIME ), 0, 0, 0 };
				pShaderAPI->SetPixelShaderConstant( 8, timeConstant );
			}
			if ( !bCheap )
			{
				VMatrix view;
				pShaderAPI->GetMatrix( MATERIAL_VIEW, view.Base() );
				Vector right( view.Base()[0], view.Base()[1], 0.0f );
				VectorNormalize( right );
				float basis0[4] = { right.x, right.y, right.z, 0 };
				float basis1[4] = { -right.y, right.x, 0, 0 };
				pShaderAPI->SetPixelShaderConstant( 22, basis0 );
				pShaderAPI->SetPixelShaderConstant( 23, basis1 );
				ShaderViewport_t viewport;
				pShaderAPI->GetViewports( &viewport, 1 );
				int backBufferWidth, backBufferHeight;
				pShaderAPI->GetBackBufferDimensions( backBufferWidth, backBufferHeight );
				float viewportTransform[4] = {
					1.0f * viewport.m_nWidth / backBufferWidth,
					1.0f * viewport.m_nHeight / backBufferHeight,
					1.0f * viewport.m_nTopLeftX / backBufferWidth,
					1.0f * viewport.m_nTopLeftY / backBufferHeight
				};
				pShaderAPI->SetPixelShaderConstant( 24, viewportTransform );
			}
			if ( bCheap )
			{
				SetPixelShaderConstant( 6, FOGCOLOR );
				float tint[4] = { 0, 0, 0, params[REFLECTBLENDFACTOR]->GetFloatValue() };
				if ( g_pConfig->bShowSpecular )
					params[REFLECTTINT]->GetVecValue( tint, 3 );
				pShaderAPI->SetPixelShaderConstant( 4, tint );
			}
			else
			{
				if ( bRefract )
					SetPixelShaderConstantGammaToLinear( 1, REFRACTTINT );
				if ( bReflect )
				{
					if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
					{
						float tint[4] = { 1, 1, 1, 1 };
						params[REFLECTTINT]->GetVecValue( tint, 3 );
						tint[0] = GammaToLinear( tint[0] ) * 4.0f;
						tint[1] = GammaToLinear( tint[1] ) * 4.0f;
						tint[2] = GammaToLinear( tint[2] ) * 4.0f;
						pShaderAPI->SetPixelShaderConstant( 4, tint );
					}
					else
					{
						SetPixelShaderConstantGammaToLinear( 4, REFLECTTINT );
					}
				}
				SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_1, BUMPTRANSFORM );
				SetScrollOffsets( params, pShaderAPI );
				float amounts[4] = { params[REFLECTAMOUNT]->GetFloatValue(), params[REFLECTAMOUNT]->GetFloatValue(),
					params[REFRACTAMOUNT]->GetFloatValue(), params[REFRACTAMOUNT]->GetFloatValue() };
				pShaderAPI->SetPixelShaderConstant( 5, amounts );
				float fogColor[4] = { 0, 0, 0, 0 };
				params[FOGCOLOR]->GetVecValue( fogColor, 3 );
				for ( int i = 0; i < 3; ++i )
					fogColor[i] = SrgbGammaToLinear( fogColor[i] );
				pShaderAPI->SetPixelShaderConstant( 6, fogColor );
				float fogParams[4] = { params[FOGSTART]->GetFloatValue(),
					params[FOGEND]->GetFloatValue() - params[FOGSTART]->GetFloatValue(), 1.0f, 0.0f };
				if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_INTEGER )
					fogParams[2] = 4.0f;
				pShaderAPI->SetPixelShaderConstant( 7, fogParams );
			}
			float eye[4];
			pShaderAPI->GetWorldSpaceCameraPosition( eye );
			eye[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 11, eye );
			pShaderAPI->SetPixelShaderFogParams( 12 );
			if ( bCheap && params[SCROLL1]->IsDefined() )
				SetScrollOffsets( params, pShaderAPI );
			bool bShadows = false;
			if ( bFlashlight )
			{
				VMatrix worldToTexture;
				ITexture *pDepthTexture;
				const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pDepthTexture );
				bShadows = state.m_bEnableShadows && pDepthTexture && g_pConfig->ShadowDepthTexture();
				BindTexture( SHADER_SAMPLER6, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );
				if ( bShadows )
				{
					BindTexture( SHADER_SAMPLER7, pDepthTexture, 0 );
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER8, TEXTURE_SHADOW_NOISE_2D );
				}
				SetFlashLightColorFromState( state, pShaderAPI, 28 );
				float attenuation[4] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
				float origin[4] = { state.m_vecLightOrigin.x, state.m_vecLightOrigin.y, state.m_vecLightOrigin.z, 0 };
				pShaderAPI->SetPixelShaderConstant( 15, attenuation );
				pShaderAPI->SetPixelShaderConstant( 16, origin );
				pShaderAPI->SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, worldToTexture.Base(), 4 );
				float tweaks[4] = { ShadowFilterFromState( state ), ShadowAttenFromState( state ), 0, 0 };
				HashShadow2DJitter( state.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
				pShaderAPI->SetPixelShaderConstant( 21, tweaks );
				int width, height;
				pShaderAPI->GetBackBufferDimensions( width, height );
				float screen[4] = { width / 32.0f, height / 32.0f, 0, 0 };
				pShaderAPI->SetPixelShaderConstant( 31, screen );
				SetPixelShaderConstant( 10, FLASHLIGHTTINT );
			}
			DECLARE_DYNAMIC_VERTEX_SHADER( l4d2_water_vs20 );
			SET_DYNAMIC_VERTEX_SHADER( l4d2_water_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( l4d2_water_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, bShadows );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( HDRENABLED, bCheap && IsHDREnabled() );
			SET_DYNAMIC_PIXEL_SHADER( l4d2_water_ps20b );
		}
		Draw();
	}
END_SHADER

BEGIN_INHERITED_SHADER( Water_DX9_HDR, Water_DX90, "L4D2 HDR water" )
	SHADER_FALLBACK
	{
		if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
			return "Water_DX90";
		return g_pHardwareConfig->GetDXSupportLevel() < 92 ? "Water_DX81" : NULL;
	}
END_INHERITED_SHADER
