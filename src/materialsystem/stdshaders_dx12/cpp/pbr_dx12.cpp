//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: PBR_Metalness and PBR_Specular. Both draw lightmapped world geometry, or studio models with $model 1, through
//          DrawPBR; they differ only in the F0 source (see PBR_Vars_t::specularWorkflow):
//            PBR_Metalness: F0 = lerp(0.04, basecolor, metalness)       ($mraotexture / $metalness / $roughness)
//            PBR_Specular:  F0 = $speculartexture or $specularcolor      (diffuse color is the base color)
//
//===========================================================================//
#include "pbr_helper.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define PBR_STANDALONE_SHADER_PARAMS \
	SHADER_PARAM( NORMALTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Normal map (alias of $bumpmap)" ) \
	SHADER_PARAM( BUMPMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Normal map" ) \
	SHADER_PARAM( BUMPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $bumpmap" ) \
	SHADER_PARAM( BUMPTRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "$bumpmap texcoord transform" ) \
	SHADER_PARAM( ENVMAP, SHADER_PARAM_TYPE_TEXTURE, "", "Reflection cubemap (env_cubemap when unset)" ) \
	SHADER_PARAM( ENVMAPFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $envmap" ) \
	SHADER_PARAM( ENVMAPTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Reflection tint" ) \
	SHADER_PARAM( ENVMAPMASK, SHADER_PARAM_TYPE_TEXTURE, "", "Reflection mask" ) \
	SHADER_PARAM( ENVMAPMASKFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $envmapmask" ) \
	SHADER_PARAM( DETAIL, SHADER_PARAM_TYPE_TEXTURE, "", "Detail texture" ) \
	SHADER_PARAM( DETAILFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "frame number for $detail" ) \
	SHADER_PARAM( DETAILSCALE, SHADER_PARAM_TYPE_FLOAT, "4", "Scale of the detail texture" ) \
	SHADER_PARAM( DETAILBLENDMODE, SHADER_PARAM_TYPE_INTEGER, "0", "0 = mod2x, 1 = additive" ) \
	SHADER_PARAM( DETAILTEXTURETRANSFORM, SHADER_PARAM_TYPE_MATRIX, "center .5 .5 scale 1 1 rotate 0 translate 0 0", "$detail texcoord transform (models)" ) \
	SHADER_PARAM( SELFILLUMTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Emission tint" ) \
	SHADER_PARAM( ALPHATESTREFERENCE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Alpha test reference" )

#define PBR_FILL_STANDALONE_VARS( v ) \
	( v ).base = BASETEXTURE; \
	( v ).baseFrame = FRAME; \
	( v ).baseTransform = BASETEXTURETRANSFORM; \
	( v ).normalTexture = NORMALTEXTURE; \
	( v ).bump = BUMPMAP; \
	( v ).bumpFrame = BUMPFRAME; \
	( v ).bumpTransform = BUMPTRANSFORM; \
	( v ).envmap = ENVMAP; \
	( v ).envmapFrame = ENVMAPFRAME; \
	( v ).envmapTint = ENVMAPTINT; \
	( v ).envmapMask = ENVMAPMASK; \
	( v ).envmapMaskFrame = ENVMAPMASKFRAME; \
	( v ).detail = DETAIL; \
	( v ).detailFrame = DETAILFRAME; \
	( v ).detailScale = DETAILSCALE; \
	( v ).detailBlendMode = DETAILBLENDMODE; \
	( v ).detailTransform = DETAILTEXTURETRANSFORM; \
	( v ).selfIllumTint = SELFILLUMTINT; \
	( v ).alphaTestReference = ALPHATESTREFERENCE; \
	( v ).standalone = true

BEGIN_VS_SHADER( PBR, "Left4Neko/Momentum-style PBR (world and models)" )
	BEGIN_SHADER_PARAMS
		PBR_STANDALONE_SHADER_PARAMS
		PBR_L4N_SHADER_PARAMS
		PBR_SHADER_PARAMS
	END_SHADER_PARAMS

	const PBR_Vars_t &Vars()
	{
		static const PBR_Vars_t s_vars = []
		{
			PBR_Vars_t v;
			PBR_FILL_STANDALONE_VARS( v );
			PBR_FILL_EXTRA_VARS( v );
			PBR_FILL_L4N_VARS( v );
			v.selfIllumTint = EMISSIONTINT;
			v.allowDiffuseModulation = ALLOWDIFFUSEMODULATION;
			v.l4n = true;
			v.dynamicSpecularWorkflow = true;
			return v;
		}();
		return s_vars;
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		InitParamsPBR( params, Vars() );
	}

	SHADER_INIT
	{
		InitPBR( this, params, Vars() );
	}

	SHADER_DRAW
	{
		DrawPBR( this, params, pShaderAPI, pShaderShadow, Vars(), vertexCompression, pContextDataPtr );
	}
END_SHADER

BEGIN_VS_SHADER( PBR_Metalness, "Metalness/roughness PBR (world and models)" )
	BEGIN_SHADER_PARAMS
		PBR_STANDALONE_SHADER_PARAMS
		PBR_SHADER_PARAMS
	END_SHADER_PARAMS

	const PBR_Vars_t &Vars()
	{
		static const PBR_Vars_t s_vars = []
		{
			PBR_Vars_t v;
			PBR_FILL_STANDALONE_VARS( v );
			PBR_FILL_EXTRA_VARS( v );
			v.specularWorkflow = false;
			return v;
		}();
		return s_vars;
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		InitParamsPBR( params, Vars() );
	}

	SHADER_INIT
	{
		InitPBR( this, params, Vars() );
	}

	SHADER_DRAW
	{
		DrawPBR( this, params, pShaderAPI, pShaderShadow, Vars(), vertexCompression, pContextDataPtr );
	}
END_SHADER

BEGIN_VS_SHADER( PBR_Specular, "Specular color PBR (world and models)" )
	BEGIN_SHADER_PARAMS
		PBR_STANDALONE_SHADER_PARAMS
		PBR_SHADER_PARAMS
	END_SHADER_PARAMS

	const PBR_Vars_t &Vars()
	{
		static const PBR_Vars_t s_vars = []
		{
			PBR_Vars_t v;
			PBR_FILL_STANDALONE_VARS( v );
			PBR_FILL_EXTRA_VARS( v );
			v.specularWorkflow = true;
			return v;
		}();
		return s_vars;
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		InitParamsPBR( params, Vars() );
	}

	SHADER_INIT
	{
		InitPBR( this, params, Vars() );
	}

	SHADER_DRAW
	{
		DrawPBR( this, params, pShaderAPI, pShaderShadow, Vars(), vertexCompression, pContextDataPtr );
	}
END_SHADER

//-----------------------------------------------------------------------------
// PBR_Infected: the L4D2 Infected material (game shader Infected_DX9 falls back to it while the PBR route is on). The
// parameters are infected_dx9.cpp's, so the actor proxies keep driving palette rows, burn strength and wound packets;
// pbr_model_* INFECTED reproduces the infected albedo / specular mask / wound cutouts / eye glow and lights it with the
// ordinary PBR model lighting ($ambientocclusion is the PBR multiplier here, not the original's unused strength).
//-----------------------------------------------------------------------------
BEGIN_VS_SHADER( PBR_Infected, "L4D2 Infected through the PBR model shaders" )
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
		SHADER_PARAM( RTTSHADOWBUILD, SHADER_PARAM_TYPE_BOOL, "0", "Depth-only shadow pass" )
		SHADER_PARAM( TRANSLUCENT_MATERIAL, SHADER_PARAM_TYPE_MATERIAL, "", "Shadow-build material marker" )
		SHADER_PARAM( TERRORWOUNDENABLED, SHADER_PARAM_TYPE_BOOL, "0", "First wound packet present" )
		SHADER_PARAM( TERRORWOUNDENABLED2, SHADER_PARAM_TYPE_BOOL, "0", "Second wound packet present" )
		SHADER_PARAM( TERRORWOUNDUVMAPPING, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "First wound atlas region" )
		SHADER_PARAM( TERRORWOUNDUVMAPPING2, SHADER_PARAM_TYPE_VEC4, "[1 1 0 0]", "Second wound atlas region" )
		SHADER_PARAM( TERRORWOUNDUVSCALE, SHADER_PARAM_TYPE_VEC2, "[0.5 0.5]", "First wound UV offset" )
		SHADER_PARAM( TERRORWOUNDUVSCALE2, SHADER_PARAM_TYPE_VEC2, "[0.5 0.5]", "Second wound UV offset" )
		PBR_SHADER_PARAMS
	END_SHADER_PARAMS

	const PBR_Vars_t &Vars()
	{
		static const PBR_Vars_t s_vars = []
		{
			PBR_Vars_t v;
			v.model = true;
			v.infected = true;
			v.base = BASETEXTURE;
			v.baseFrame = FRAME;
			v.baseTransform = BASETEXTURETRANSFORM;
			v.bump = BUMPMAP;
			v.detail = DETAIL;
			v.detailFrame = DETAILFRAME;
			v.detailScale = DETAILSCALE;
			v.phongTint = PHONGTINT;
			v.phongBoost = PHONGBOOST;
			v.phongFresnelRanges = PHONGFRESNELRANGES;
			v.shinyBlood = SHINYBLOOD;
			v.allowDiffuseModulation = ALLOWDIFFUSEMODULATION;
			v.infDisableVariation = DISABLEVARIATION;
			v.infGradient = GRADIENTTEXTURE;
			v.infBurning = BURNING;
			v.infBurnTexture = BURNDETAILTEXTURE;
			v.infBurnStrength = BURNSTRENGTH;
			v.infBloodColor = BLOODCOLOR;
			v.infBloodMaskRange = BLOODMASKRANGE;
			v.infEyeGlow = EYEGLOW;
			v.infEyeGlowColor = EYEGLOWCOLOR;
			v.infDefaultExponent = DEFAULTPHONGEXPONENT;
			v.infSkinExponent = SKINPHONGEXPONENT;
			v.infDetailExponent = DETAILPHONGEXPONENT;
			v.infBloodExponent = BLOODPHONGEXPONENT;
			v.infBloodSpecBoost = BLOODSPECBOOST;
			v.shinyBloodExponent = SHINYBLOODEXPONENT;
			v.infSheetIndex = SHEETINDEX;
			v.infSkinTint = SKINTINTGRADIENT;
			v.infColorTint = COLORTINTGRADIENT;
			v.infWounded = WOUNDED;
			v.infWoundTexture = WOUNDCUTOUTTEXTURE;
			v.infCutoutBias = CUTOUTTEXTUREBIAS;
			v.infMappingScale = CUTOUTDECALMAPPINGSCALE;
			v.infDebugEllipsoids = DEBUGELLIPSOIDS;
			v.infEllipsoid2CullType = ELLIPSOID2CULLTYPE;
			v.infEllipsoidCenter[0] = ELLIPSOIDCENTER;
			v.infEllipsoidCenter[1] = ELLIPSOIDCENTER2;
			v.infEllipsoidLookAt[0] = ELLIPSOIDLOOKAT;
			v.infEllipsoidLookAt[1] = ELLIPSOIDLOOKAT2;
			v.infEllipsoidUp[0] = ELLIPSOIDUP;
			v.infEllipsoidUp[1] = ELLIPSOIDUP2;
			v.infEllipsoidScale[0] = ELLIPSOIDSCALE;
			v.infEllipsoidScale[1] = ELLIPSOIDSCALE2;
			v.infWoundEnabled[0] = TERRORWOUNDENABLED;
			v.infWoundEnabled[1] = TERRORWOUNDENABLED2;
			v.infWoundUVMapping[0] = TERRORWOUNDUVMAPPING;
			v.infWoundUVMapping[1] = TERRORWOUNDUVMAPPING2;
			v.infWoundUVScale[0] = TERRORWOUNDUVSCALE;
			v.infWoundUVScale[1] = TERRORWOUNDUVSCALE2;
			PBR_FILL_EXTRA_VARS( v );
			// The INFECTED pixel path builds F0 from the infected specular mask / Phong boost / Fresnel; the metalness
			// workflow would replace it with a flat 0.04 everywhere.
			v.specularWorkflow = true;
			return v;
		}();
		return s_vars;
	}

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT_PARAMS()
	{
		// infected_dx9.cpp SHADER_INIT_PARAMS (registration defaults and undefined-parameter values stay distinct).
		SET_FLAGS2( MATERIAL_VAR2_SUPPORTS_HW_SKINNING );
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_VERTEX_LIT );
		if ( !params[DISABLEVARIATION]->IsDefined() ) params[DISABLEVARIATION]->SetIntValue( 0 );
		if ( !params[PHONGTINT]->IsDefined() ) params[PHONGTINT]->SetVecValue( 1.0f, 1.0f, 1.0f );
		if ( !params[PHONGFRESNELRANGES]->IsDefined() ) params[PHONGFRESNELRANGES]->SetVecValue( 0.0f, 0.5f, 1.0f );
		const int zeroDefault[] = { SHINYBLOOD, BLENDTINTBYBASEALPHA, DETAILFRAME, BURNING, EYEGLOW, STATICPROP,
			SKINTINTGRADIENT, COLORTINTGRADIENT, SHEETINDEX, WOUNDED, DEBUGELLIPSOIDS, ELLIPSOID2CULLTYPE, RTTSHADOWBUILD };
		for ( int var : zeroDefault )
			if ( !params[var]->IsDefined() ) params[var]->SetIntValue( 0 );
		if ( !params[ALLOWDIFFUSEMODULATION]->IsDefined() ) params[ALLOWDIFFUSEMODULATION]->SetIntValue( 1 );
		if ( !params[BLOODCOLOR]->IsDefined() ) params[BLOODCOLOR]->SetVecValue( 1.0f, 0.0f, 0.0f );
		if ( !params[EYEGLOWCOLOR]->IsDefined() ) params[EYEGLOWCOLOR]->SetVecValue( 1.0f, 1.0f, 0.5f );
		if ( !params[BLOODMASKRANGE]->IsDefined() ) params[BLOODMASKRANGE]->SetVecValue( 0.0f, 1.0f );
		const int center[] = { ELLIPSOIDCENTER, ELLIPSOIDCENTER2 }, look[] = { ELLIPSOIDLOOKAT, ELLIPSOIDLOOKAT2 };
		const int up[] = { ELLIPSOIDUP, ELLIPSOIDUP2 }, scale[] = { ELLIPSOIDSCALE, ELLIPSOIDSCALE2 };
		for ( int i = 0; i < 2; ++i )
		{
			if ( !params[center[i]]->IsDefined() ) params[center[i]]->SetVecValue( 0.0f, 0.0f, 0.0f );
			if ( !params[look[i]]->IsDefined() ) params[look[i]]->SetVecValue( 1.0f, 0.0f, 0.0f );
			if ( !params[up[i]]->IsDefined() ) params[up[i]]->SetVecValue( 0.0f, 0.0f, 1.0f );
			if ( !params[scale[i]]->IsDefined() ) params[scale[i]]->SetVecValue( 10.0f, 10.0f, 10.0f );
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
		if ( params[DISABLEVARIATION]->GetIntValue() && params[BUMPMAP]->IsDefined() && !g_pConfig->m_bFastNoBump )
			SET_FLAGS2( MATERIAL_VAR2_NEEDS_TANGENT_SPACES );
		SET_FLAGS( MATERIAL_VAR_IGNORE_ALPHA_MODULATION );
		InitParamsPBRDefaults( params, Vars() );
	}

	SHADER_INIT
	{
		// infected_dx9.cpp SHADER_INIT
		LoadTexture( BASETEXTURE, params[DISABLEVARIATION]->GetIntValue() ? TEXTUREFLAGS_SRGB : 0 );
		if ( params[DETAIL]->IsDefined() ) LoadTexture( DETAIL );
		if ( params[GRADIENTTEXTURE]->IsDefined() ) LoadTexture( GRADIENTTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[BURNING]->GetIntValue() ) LoadTexture( BURNDETAILTEXTURE, TEXTUREFLAGS_SRGB );
		if ( params[DISABLEVARIATION]->GetIntValue() && params[BUMPMAP]->IsDefined() ) LoadBumpMap( BUMPMAP );
		if ( params[WOUNDCUTOUTTEXTURE]->IsDefined() ) LoadTexture( WOUNDCUTOUTTEXTURE );
	}

	SHADER_DRAW
	{
		DrawPBR( this, params, pShaderAPI, pShaderShadow, Vars(), vertexCompression, pContextDataPtr );
	}
END_SHADER
