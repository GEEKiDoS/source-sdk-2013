//========= Copyright Valve Corporation, All rights reserved. ============//
#include "BaseVSShader.h"
#include "infected_vs20.inc"
#include "infected_ps20b.inc"

#include "tier0/memdbgon.h"

BEGIN_VS_SHADER( Infected_DX9, "L4D2 infected" )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DISABLEVARIATION, SHADER_PARAM_TYPE_BOOL, "1", "Disable palette and blood-pattern variation" )
		SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "models/shadertest/shader1_normal", "Normal map for non-variation materials" )
		SHADER_PARAM( PHONGTINT, SHADER_PARAM_TYPE_VEC3, "5.0", "Phong tint" )
		SHADER_PARAM( PHONGFRESNELRANGES, SHADER_PARAM_TYPE_VEC3, "[0 0.5 1]", "Fresnel remapping" )
		SHADER_PARAM( PHONGBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "Phong boost" )
		SHADER_PARAM( BLENDTINTBYBASEALPHA, SHADER_PARAM_TYPE_BOOL, "0", "Blend tint by base alpha" )
		SHADER_PARAM( SHINYBLOOD, SHADER_PARAM_TYPE_BOOL, "1", "Derive a specular mask from red in non-variation materials" )
		SHADER_PARAM( SHINYBLOODEXPONENT, SHADER_PARAM_TYPE_FLOAT, "17.0", "Red-mask exponent" )
		SHADER_PARAM( ALLOWDIFFUSEMODULATION, SHADER_PARAM_TYPE_BOOL, "1", "Allow diffuse modulation" )
		SHADER_PARAM( DETAIL, SHADER_PARAM_TYPE_TEXTURE, "shadertest/detail", "Detail map" )
		SHADER_PARAM( DETAILFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "Detail frame" )
		SHADER_PARAM( DETAILSCALE, SHADER_PARAM_TYPE_FLOAT, "4", "Detail scale" )
		SHADER_PARAM( DETAILBLENDFACTOR, SHADER_PARAM_TYPE_FLOAT, "1", "Detail blend" )
		SHADER_PARAM( BURNING, SHADER_PARAM_TYPE_BOOL, "0", "Enable the burn map" )
		SHADER_PARAM( BURNDETAILTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "shadertest/detail", "Burn map" )
		SHADER_PARAM( BLOODCOLOR, SHADER_PARAM_TYPE_VEC3, "[1 0 0]", "Blood color" )
		SHADER_PARAM( EYEGLOW, SHADER_PARAM_TYPE_BOOL, "0", "Enable eye glow" )
		SHADER_PARAM( EYEGLOWCOLOR, SHADER_PARAM_TYPE_VEC3, "[1 1 0.5]", "Eye glow color" )
		SHADER_PARAM( EYEGLOWFLASHLIGHTBOOST, SHADER_PARAM_TYPE_FLOAT, "100", "Flashlight eye glow boost" )
		SHADER_PARAM( DEFAULTPHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "5.0", "Default exponent" )
		SHADER_PARAM( SKINPHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "5.0", "Skin exponent" )
		SHADER_PARAM( DETAILPHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "5.0", "Detail exponent" )
		SHADER_PARAM( BLOODPHONGEXPONENT, SHADER_PARAM_TYPE_FLOAT, "5.0", "Blood exponent" )
		SHADER_PARAM( BLOODSPECBOOST, SHADER_PARAM_TYPE_FLOAT, "1.0", "Blood specular boost" )
		SHADER_PARAM( BLOODMASKRANGE, SHADER_PARAM_TYPE_VEC2, "[0 1]", "Blood remapping range" )
		SHADER_PARAM( GRADIENTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Palette texture" )
		SHADER_PARAM( STATICPROP, SHADER_PARAM_TYPE_BOOL, "0", "Use authored palette indices instead of instance data" )
		SHADER_PARAM( SKINTINTGRADIENT, SHADER_PARAM_TYPE_INTEGER, "0", "Skin palette row" )
		SHADER_PARAM( COLORTINTGRADIENT, SHADER_PARAM_TYPE_INTEGER, "0", "Clothing palette row" )
		SHADER_PARAM( SHEETINDEX, SHADER_PARAM_TYPE_INTEGER, "0", "Blood atlas quadrant" )
		SHADER_PARAM( BURNSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "0", "Authored burn strength" )
		SHADER_PARAM( WOUNDED, SHADER_PARAM_TYPE_BOOL, "0", "Enable wound cutouts" )
		SHADER_PARAM( DEBUGELLIPSOIDS, SHADER_PARAM_TYPE_BOOL, "0", "Use authored ellipsoids" )
		SHADER_PARAM( ELLIPSOIDCENTER, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "First wound center" )
		SHADER_PARAM( ELLIPSOIDLOOKAT, SHADER_PARAM_TYPE_VEC3, "[1 0 0]", "First wound direction" )
		SHADER_PARAM( ELLIPSOIDUP, SHADER_PARAM_TYPE_VEC3, "[0 0 1]", "First wound up direction" )
		SHADER_PARAM( ELLIPSOIDSCALE, SHADER_PARAM_TYPE_VEC3, "[10 10 10]", "First wound radii" )
		SHADER_PARAM( ELLIPSOIDCENTER2, SHADER_PARAM_TYPE_VEC3, "[0 0 0]", "Second wound center" )
		SHADER_PARAM( ELLIPSOIDLOOKAT2, SHADER_PARAM_TYPE_VEC3, "[1 0 0]", "Second wound direction" )
		SHADER_PARAM( ELLIPSOIDUP2, SHADER_PARAM_TYPE_VEC3, "[0 0 1]", "Second wound up direction" )
		SHADER_PARAM( ELLIPSOIDSCALE2, SHADER_PARAM_TYPE_VEC3, "[10 10 10]", "Second wound radii" )
		SHADER_PARAM( ELLIPSOID2CULLTYPE, SHADER_PARAM_TYPE_INTEGER, "0", "Second wound cull mode" )
		SHADER_PARAM( CUTOUTDECALMAPPINGSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "Wound projection scale" )
		SHADER_PARAM( WOUNDCUTOUTTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Wound atlas" )
		SHADER_PARAM( CUTOUTTEXTUREBIAS, SHADER_PARAM_TYPE_FLOAT, "", "Cutout threshold" )
		SHADER_PARAM( AMBIENTOCCLUSION, SHADER_PARAM_TYPE_FLOAT, "", "Ambient occlusion strength" )
		SHADER_PARAM( RTTSHADOWBUILD, SHADER_PARAM_TYPE_BOOL, "0", "Depth-only shadow pass" )
		SHADER_PARAM( TRANSLUCENT_MATERIAL, SHADER_PARAM_TYPE_MATERIAL, "", "Shadow-build material marker" )

		// Native game-owned transport for the original engine's per-instance
		// wound packet. The proxy supplies the unmodified actor/ragdoll fields.
		SHADER_PARAM( TERRORWOUNDENABLED, SHADER_PARAM_TYPE_BOOL, "0", "First wound packet present" )
		SHADER_PARAM( TERRORWOUNDENABLED2, SHADER_PARAM_TYPE_BOOL, "0", "Second wound packet present" )
		SHADER_PARAM( TERRORWOUNDUVMAPPING, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "First wound atlas region" )
		SHADER_PARAM( TERRORWOUNDUVMAPPING2, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Second wound atlas region" )
		SHADER_PARAM( TERRORWOUNDUVSCALE, SHADER_PARAM_TYPE_VEC2, "[0.5 0.5]", "First wound UV offset" )
		SHADER_PARAM( TERRORWOUNDUVSCALE2, SHADER_PARAM_TYPE_VEC2, "[0.5 0.5]", "Second wound UV offset" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// Original stdshader_dx9.dll:0x17070. Registration defaults and
		// undefined-parameter initialization are deliberately kept distinct.
		params[FLASHLIGHTTEXTURE]->SetStringValue( "effects/flashlight001" );
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		if ( !params[DISABLEVARIATION]->IsDefined() ) params[DISABLEVARIATION]->SetIntValue( 0 );
		if ( !params[PHONGTINT]->IsDefined() ) params[PHONGTINT]->SetVecValue( 1.0f, 1.0f, 1.0f );
		if ( !params[PHONGFRESNELRANGES]->IsDefined() ) params[PHONGFRESNELRANGES]->SetVecValue( 0.0f, 0.5f, 1.0f );
		const int nZeroDefault[] = { SHINYBLOOD, BLENDTINTBYBASEALPHA, DETAILFRAME, BURNING, EYEGLOW, STATICPROP,
			SKINTINTGRADIENT, COLORTINTGRADIENT, SHEETINDEX, WOUNDED, DEBUGELLIPSOIDS, ELLIPSOID2CULLTYPE, RTTSHADOWBUILD };
		for ( int i = 0; i < ARRAYSIZE( nZeroDefault ); ++i )
		{
			if ( !params[nZeroDefault[i]]->IsDefined() ) params[nZeroDefault[i]]->SetIntValue( 0 );
		}
		if ( !params[ALLOWDIFFUSEMODULATION]->IsDefined() ) params[ALLOWDIFFUSEMODULATION]->SetIntValue( 1 );
		if ( !params[BLOODCOLOR]->IsDefined() ) params[BLOODCOLOR]->SetVecValue( 1.0f, 0.0f, 0.0f );
		if ( !params[EYEGLOWCOLOR]->IsDefined() ) params[EYEGLOWCOLOR]->SetVecValue( 1.0f, 1.0f, 0.5f );
		if ( !params[BLOODMASKRANGE]->IsDefined() ) params[BLOODMASKRANGE]->SetVecValue( 0.0f, 1.0f );
		const int nCenterDefault[] = { ELLIPSOIDCENTER, ELLIPSOIDCENTER2 };
		const int nLookDefault[] = { ELLIPSOIDLOOKAT, ELLIPSOIDLOOKAT2 };
		const int nUpDefault[] = { ELLIPSOIDUP, ELLIPSOIDUP2 };
		const int nScaleDefault[] = { ELLIPSOIDSCALE, ELLIPSOIDSCALE2 };
		for ( int i = 0; i < 2; ++i )
		{
			if ( !params[nCenterDefault[i]]->IsDefined() ) params[nCenterDefault[i]]->SetVecValue( 0.0f, 0.0f, 0.0f );
			if ( !params[nLookDefault[i]]->IsDefined() ) params[nLookDefault[i]]->SetVecValue( 1.0f, 0.0f, 0.0f );
			if ( !params[nUpDefault[i]]->IsDefined() ) params[nUpDefault[i]]->SetVecValue( 0.0f, 0.0f, 1.0f );
			if ( !params[nScaleDefault[i]]->IsDefined() ) params[nScaleDefault[i]]->SetVecValue( 10.0f, 10.0f, 10.0f );
		}
		INIT_FLOAT_PARM( PHONGBOOST, 1.0f );
		INIT_FLOAT_PARM( SHINYBLOODEXPONENT, 13.0f );
		INIT_FLOAT_PARM( DETAILSCALE, 4.0f );
		INIT_FLOAT_PARM( DETAILBLENDFACTOR, 1.0f );
		INIT_FLOAT_PARM( EYEGLOWFLASHLIGHTBOOST, 100.0f );
		INIT_FLOAT_PARM( DEFAULTPHONGEXPONENT, 5.0f );
		INIT_FLOAT_PARM( SKINPHONGEXPONENT, 5.0f );
		INIT_FLOAT_PARM( DETAILPHONGEXPONENT, 5.0f );
		INIT_FLOAT_PARM( BLOODPHONGEXPONENT, 5.0f );
		INIT_FLOAT_PARM( BLOODSPECBOOST, 1.0f );
		INIT_FLOAT_PARM( BURNSTRENGTH, 0.0f );
		INIT_FLOAT_PARM( CUTOUTDECALMAPPINGSCALE, 1.0f );
		INIT_FLOAT_PARM( CUTOUTTEXTUREBIAS, 0.9f );
		INIT_FLOAT_PARM( AMBIENTOCCLUSION, 0.0f );
		if ( params[DISABLEVARIATION]->GetIntValue() && params[BUMPMAP]->IsDefined() && !g_pConfig->m_bFastNoBump )
			SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		SET_FLAGS( MATERIAL_VAR_IGNORE_ALPHA_MODULATION );
	}

	SHADER_FALLBACK
	{
		return NULL;
	}

	SHADER_INIT
	{
		// Original stdshader_dx9.dll:0x17980.
		if ( !params[RTTSHADOWBUILD]->GetIntValue() )
		{
			LoadTexture( FLASHLIGHTTEXTURE, TEXTUREFLAGS_SRGB );
			LoadTexture( BASETEXTURE, params[DISABLEVARIATION]->GetIntValue() ? TEXTUREFLAGS_SRGB : 0 );
			if ( params[DETAIL]->IsDefined() ) LoadTexture( DETAIL );
			if ( params[GRADIENTTEXTURE]->IsDefined() ) LoadTexture( GRADIENTTEXTURE, TEXTUREFLAGS_SRGB );
			if ( params[BURNING]->GetIntValue() ) LoadTexture( BURNDETAILTEXTURE, TEXTUREFLAGS_SRGB );
			if ( params[DISABLEVARIATION]->GetIntValue() && params[BUMPMAP]->IsDefined() ) LoadBumpMap( BUMPMAP );
		}
		if ( params[WOUNDCUTOUTTEXTURE]->IsDefined() ) LoadTexture( WOUNDCUTOUTTEXTURE );
	}

	void SetWoundConstants( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI )
	{
		const bool bDebug = params[DEBUGELLIPSOIDS]->GetIntValue() != 0;
		const bool bPacket = params[TERRORWOUNDENABLED]->GetIntValue() || params[TERRORWOUNDENABLED2]->GetIntValue();
		const int nCenter[2] = { ELLIPSOIDCENTER, ELLIPSOIDCENTER2 };
		const int nLook[2] = { ELLIPSOIDLOOKAT, ELLIPSOIDLOOKAT2 };
		const int nUp[2] = { ELLIPSOIDUP, ELLIPSOIDUP2 };
		const int nScale[2] = { ELLIPSOIDSCALE, ELLIPSOIDSCALE2 };
		const int nCenterRegister[2] = { 52, 56 };
		const int nLookRegister[2] = { 53, 57 };
		const int nUpRegister[2] = { 54, 14 };
		const int nSideRegister[2] = { 55, 15 };
		const float flMappingScale = params[CUTOUTDECALMAPPINGSCALE]->GetFloatValue();
		const float flCull = bDebug ? ( params[ELLIPSOID2CULLTYPE]->GetIntValue() ? 1.0f : 0.0f ) :
			( params[ELLIPSOID2CULLTYPE]->GetIntValue() ? 0.0f : 1.0f );
		for ( int i = 0; i < 2; ++i )
		{
			Vector vecLook, vecUp, vecSide, vecScale;
			params[nLook[i]]->GetVecValue( vecLook.Base(), 3 );
			params[nUp[i]]->GetVecValue( vecUp.Base(), 3 );
			params[nScale[i]]->GetVecValue( vecScale.Base(), 3 );
			VectorNormalize( vecLook );
			VectorNormalize( vecUp );
			CrossProduct( vecLook, vecUp, vecSide );
			CrossProduct( vecSide, vecLook, vecUp );
			vecSide *= 1.0f / vecScale.x;
			vecUp *= 1.0f / vecScale.y;
			vecLook *= 1.0f / vecScale.z;
			float v[4];
			params[nCenter[i]]->GetVecValue( v, 3 );
			if ( !bDebug && !bPacket ) { v[0] = 1000000.0f; v[1] = v[2] = 0.0f; }
			v[3] = 0.0f;
			pShaderAPI->SetVertexShaderConstant( nCenterRegister[i], v );
			VectorCopy( vecLook.Base(), v ); v[3] = flMappingScale;
			pShaderAPI->SetVertexShaderConstant( nLookRegister[i], v );
			VectorCopy( vecUp.Base(), v ); v[3] = 0.0f;
			pShaderAPI->SetVertexShaderConstant( nUpRegister[i], v );
			VectorCopy( vecSide.Base(), v ); v[3] = flCull;
			pShaderAPI->SetVertexShaderConstant( nSideRegister[i], v );
		}

		float vRegion[2][4] = { { 1, 1, 0, 0 }, { 1, 1, 0, 0 } };
		float vOffset[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
		float vUVScale[4] = { 1, 1, 1, 1 };
		if ( bPacket && !bDebug )
		{
			params[TERRORWOUNDUVMAPPING]->GetVecValue( vRegion[0], 4 );
			params[TERRORWOUNDUVMAPPING2]->GetVecValue( vRegion[1], 4 );
			params[TERRORWOUNDUVSCALE]->GetVecValue( vOffset, 2 );
			params[TERRORWOUNDUVSCALE2]->GetVecValue( vOffset + 2, 2 );
			ITexture *pTexture = params[WOUNDCUTOUTTEXTURE]->GetTextureValue();
			const float flAspect = pTexture ? (float)pTexture->GetActualWidth() / (float)pTexture->GetActualHeight() : 1.0f;
			const float a = vRegion[1][0] * flAspect;
			const float b = vRegion[1][1];
			// The original adjusts only the first pair from the SECOND region.
			// Preserve this asymmetry (shaderapidx9.dll:0x227C3).
			vUVScale[0] = b > a ? 1.0f : a / b;
			vUVScale[1] = b > a ? b / a : 1.0f;
		}
		pShaderAPI->SetVertexShaderConstant( 48, vUVScale );
		pShaderAPI->SetVertexShaderConstant( 49, vOffset );
		pShaderAPI->SetPixelShaderConstant( 7, vRegion[0] );
		pShaderAPI->SetPixelShaderConstant( 8, vRegion[1] );
	}

	void DrawInfected( IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow, VertexCompressionType_t vertexCompression, bool bFlashlight )
	{
		const bool bDepthOnly = params[RTTSHADOWBUILD]->GetIntValue() != 0;
		const bool bNoVariation = params[DISABLEVARIATION]->GetIntValue() != 0 && !bDepthOnly;
		const bool bBump = bNoVariation && params[BUMPMAP]->IsTexture() && !g_pConfig->m_bFastNoBump;
		const bool bDetail = params[DETAIL]->IsTexture() && !bNoVariation && !bDepthOnly;
		const bool bBurn = params[BURNING]->GetIntValue() && !bNoVariation && !bDepthOnly;
		const bool bEyes = params[EYEGLOW]->GetIntValue() && !bNoVariation && !bDepthOnly;
		const bool bShinyBlood = params[SHINYBLOOD]->GetIntValue() && bNoVariation && !bDepthOnly;
		const bool bWounded = params[WOUNDED]->GetIntValue() != 0;
		const bool bHalfLambert = IS_FLAG_SET( MATERIAL_VAR_HALFLAMBERT ) && !bDepthOnly;
		const bool bTranslucent = IsTranslucent( params );
		const bool bStaticControlFlow = g_pHardwareConfig->SupportsStaticControlFlow();
		const int nShadowFilter = bFlashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0;

		if ( pShaderShadow )
		{
			if ( bFlashlight )
			{
				SetAdditiveBlendingShadowState( BASETEXTURE, true );
				pShaderShadow->EnableDepthWrites( false );
				pShaderShadow->EnableAlphaWrites( false );
				FogToBlack();
			}
			else
			{
				SetDefaultBlendingShadowState( BASETEXTURE, true );
				pShaderShadow->EnableAlphaWrites( true );
				DefaultFog();
			}
			pShaderShadow->EnableSRGBWrite( true );
			if ( bDepthOnly ) pShaderShadow->EnableColorWrites( false );
			unsigned int nFlags = VERTEX_POSITION | VERTEX_FORMAT_COMPRESSED;
			if ( !bDepthOnly ) nFlags |= VERTEX_NORMAL;
			// $staticprop selects authored palette data, not a COLOR0 stream.
			// Static lighting arrives separately through the SDK's COLOR1 stream.
			int nTexCoordSize = 2;
			pShaderShadow->VertexShaderVertexFormat( nFlags, 1, &nTexCoordSize, bBump ? 4 : 0 );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER5, !bBump );
			pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
			if ( bDetail ) pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
			if ( bBurn )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER8, true );
			}
			if ( bFlashlight )
			{
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER1 );
				pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
				pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, true );
			}
			DECLARE_STATIC_VERTEX_SHADER( infected_vs20 );
			SET_STATIC_VERTEX_SHADER_COMBO( WOUNDED, bWounded );
			SET_STATIC_VERTEX_SHADER_COMBO( DEPTHONLY, bDepthOnly );
			SET_STATIC_VERTEX_SHADER_COMBO( HALFLAMBERT, bHalfLambert );
			SET_STATIC_VERTEX_SHADER_COMBO( USE_STATIC_CONTROL_FLOW, bStaticControlFlow );
			SET_STATIC_VERTEX_SHADER_COMBO( BUMP, bBump );
			SET_STATIC_VERTEX_SHADER( infected_vs20 );
			DECLARE_STATIC_PIXEL_SHADER( infected_ps20b );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, bFlashlight );
			SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHTDEPTHFILTERMODE, nShadowFilter );
			SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, bDetail );
			SET_STATIC_PIXEL_SHADER_COMBO( WOUNDED, bWounded );
			SET_STATIC_PIXEL_SHADER_COMBO( DEPTHONLY, bDepthOnly );
			SET_STATIC_PIXEL_SHADER_COMBO( BURNDETAILTEXTURE, bBurn );
			SET_STATIC_PIXEL_SHADER_COMBO( EYEGLOW, bEyes );
			SET_STATIC_PIXEL_SHADER_COMBO( TRANSLUCENT, bTranslucent );
			SET_STATIC_PIXEL_SHADER_COMBO( DISABLEVARIATION, bNoVariation );
			SET_STATIC_PIXEL_SHADER_COMBO( SHINYBLOOD, bShinyBlood );
			SET_STATIC_PIXEL_SHADER_COMBO( BUMP, bBump );
			SET_STATIC_PIXEL_SHADER( infected_ps20b );
		}
		else
		{
			if ( params[BASETEXTURE]->IsTexture() ) BindTexture( SHADER_SAMPLER0, BASETEXTURE, FRAME );
			else pShaderAPI->BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
			if ( bBump ) BindTexture( SHADER_SAMPLER5, BUMPMAP );
			else if ( params[GRADIENTTEXTURE]->IsTexture() ) BindTexture( SHADER_SAMPLER5, GRADIENTTEXTURE );
			else pShaderAPI->BindStandardTexture( SHADER_SAMPLER5, TEXTURE_WHITE );
			if ( params[WOUNDCUTOUTTEXTURE]->IsTexture() ) BindTexture( SHADER_SAMPLER6, WOUNDCUTOUTTEXTURE );
			else pShaderAPI->BindStandardTexture( SHADER_SAMPLER6, TEXTURE_BLACK );
			if ( bDetail ) BindTexture( SHADER_SAMPLER4, DETAIL, DETAILFRAME );
			if ( bBurn ) BindTexture( SHADER_SAMPLER8, BURNDETAILTEXTURE );

			LightState_t lightState = { 0, false, false };
			if ( params[STATICPROP]->GetIntValue() || !bFlashlight ) pShaderAPI->GetDX9LightState( &lightState );
			bool bFlashlightShadows = false;
			if ( bFlashlight )
			{
				VMatrix worldToTexture;
				ITexture *pDepthTexture;
				const FlashlightState_t &state = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pDepthTexture );
				bFlashlightShadows = pDepthTexture && state.m_bEnableShadows && g_pConfig->ShadowDepthTexture();
				BindTexture( SHADER_SAMPLER3, state.m_pSpotlightTexture, state.m_nSpotlightTextureFrame );
				if ( bFlashlightShadows )
				{
					BindTexture( SHADER_SAMPLER1, pDepthTexture, 0 );
					pShaderAPI->BindStandardTexture( SHADER_SAMPLER2, TEXTURE_SHADOW_NOISE_2D );
					// FLASHLIGHT forces NUM_LIGHTS=0. Reuse its dead local-light
					// constants so PS20b does not spill the original literal taps.
					static const float s_vShadowPoisson[4][4] =
					{
						{ 0.3475f, 0.0042f, 0.8806f, 0.3430f },
						{ -0.0041f, -0.6197f, 0.0472f, 0.4964f },
						{ -0.3730f, 0.0874f, -0.9217f, -0.3177f },
						{ -0.6289f, 0.7388f, 0.5744f, -0.7741f }
					};
					pShaderAPI->SetPixelShaderConstant( 20, s_vShadowPoisson[0], 4 );
				}
				SetFlashLightColorFromState( state, pShaderAPI, 28 );
				float v[4] = { state.m_fConstantAtten, state.m_fLinearAtten, state.m_fQuadraticAtten, state.m_FarZ };
				pShaderAPI->SetPixelShaderConstant( 13, v );
				VectorCopy( state.m_vecLightOrigin.Base(), v ); v[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 14, v );
				pShaderAPI->SetPixelShaderConstant( 15, worldToTexture.Base(), 4 );
				v[0] = ShadowFilterFromState( state ); v[1] = ShadowAttenFromState( state );
				HashShadow2DJitter( state.m_flShadowJitterSeed, &v[2], &v[3] );
				pShaderAPI->SetPixelShaderConstant( 2, v );
				int nWidth, nHeight;
				pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
				v[0] = (float)nWidth / 32.0f; v[1] = (float)nHeight / 32.0f; v[2] = v[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 31, v );
			}
			DECLARE_DYNAMIC_VERTEX_SHADER( infected_vs20 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SKINNING, pShaderAPI->GetCurrentNumBones() > 0 );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( COMPRESSED_VERTS, (int)vertexCompression );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( STATICLIGHT, !bDepthOnly && lightState.m_bStaticLightVertex );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( NUM_LIGHTS, bStaticControlFlow ? 0 : lightState.m_nNumLights );
			SET_DYNAMIC_VERTEX_SHADER_COMBO( SCENEFOG, pShaderAPI->GetSceneFogMode() != MATERIAL_FOG_NONE );
			SET_DYNAMIC_VERTEX_SHADER( infected_vs20 );
			DECLARE_DYNAMIC_PIXEL_SHADER( infected_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( NUM_LIGHTS, bDepthOnly || bFlashlight ? 0 : lightState.m_nNumLights );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FLASHLIGHTSHADOWS, bFlashlightShadows );
			SET_DYNAMIC_PIXEL_SHADER( infected_ps20b );

			SetAmbientCubeDynamicStateVertexShader();
			if ( !bFlashlight )
				pShaderAPI->CommitPixelShaderLighting( 20 );
			float vEye[4];
			pShaderAPI->GetWorldSpaceCameraPosition( vEye );
			vEye[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 11, vEye );
			// Retail's VS c12 is the view direction; the SDK uses that register
			// for a projection-Z row which this shader does not consume.
			float matView[16];
			pShaderAPI->GetMatrix( MATERIAL_VIEW, matView );
			vEye[0] = matView[2]; vEye[1] = matView[6]; vEye[2] = matView[10];
			pShaderAPI->SetVertexShaderConstant( 12, vEye );
			float v[4];
			ComputeModulationColor( v );
			const int nSheet = params[SHEETINDEX]->GetIntValue();
			const float flSkin = (float)params[SKINTINTGRADIENT]->GetIntValue();
			const float flClothing = (float)params[COLORTINTGRADIENT]->GetIntValue();
			const float flAlpha = v[3];
			ITexture *pGradient = params[GRADIENTTEXTURE]->IsTexture() ? params[GRADIENTTEXTURE]->GetTextureValue() : NULL;
			const float flGradientHeight = pGradient ? (float)pGradient->GetActualHeight() : 1.0f;
			v[0] = (float)( nSheet / 2 ) * 0.5f; v[1] = (float)( nSheet % 2 ) * 0.5f;
			v[2] = ( flSkin + 0.5f ) / flGradientHeight; v[3] = ( flClothing + 0.5f ) / flGradientHeight;
			pShaderAPI->SetPixelShaderConstant( 10, v );
			v[0] = params[BURNSTRENGTH]->GetFloatValue() * 2.0f;
			v[1] = v[2] = v[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 5, v );
			v[0] = v[1] = v[2] = 1.0f; v[3] = flAlpha;
			pShaderAPI->SetPixelShaderConstant( 1, v );
			SetVertexShaderTextureScaledTransform( 50, BASETEXTURETRANSFORM, DETAILSCALE );
			SetPixelShaderConstant( 0, EYEGLOWCOLOR, BLOODSPECBOOST );
			SetPixelShaderConstant( 6, EYEGLOWFLASHLIGHTBOOST );
			v[0] = params[DETAILPHONGEXPONENT]->GetFloatValue(); v[1] = params[SKINPHONGEXPONENT]->GetFloatValue();
			v[2] = params[DEFAULTPHONGEXPONENT]->GetFloatValue(); v[3] = params[BLOODPHONGEXPONENT]->GetFloatValue();
			pShaderAPI->SetPixelShaderConstant( 27, v );
			SetPixelShaderConstant( 26, PHONGTINT, SHINYBLOODEXPONENT );
			SetPixelShaderConstant( 19, PHONGFRESNELRANGES, PHONGBOOST );
			params[BLOODMASKRANGE]->GetVecValue( v, 2 ); v[2] = params[CUTOUTTEXTUREBIAS]->GetFloatValue(); v[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 3, v );
			params[BLOODCOLOR]->GetVecValue( v, 3 ); v[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 4, v );
			pShaderAPI->SetPixelShaderFogParams( 12 );
			if ( bWounded ) SetWoundConstants( params, pShaderAPI );
		}
		Draw();
	}

	SHADER_DRAW
	{
		const bool bFlashlight = UsingFlashlight( params ) && !params[RTTSHADOWBUILD]->GetIntValue();
		if ( bFlashlight )
		{
			DrawInfected( params, pShaderAPI, pShaderShadow, vertexCompression, false );
			if ( pShaderShadow ) SetInitialShadowState();
		}
		DrawInfected( params, pShaderAPI, pShaderShadow, vertexCompression, bFlashlight );
	}
END_SHADER
