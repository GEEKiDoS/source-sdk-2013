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
