//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared material side of the DX12 PBR shader set. PBR_Metalness / PBR_Specular (pbr_dx12.cpp), the PBR_* adapters
//          (inherited shaders at the end of the LightmappedGeneric / VertexLitGeneric / WorldVertexTransition wrappers) and
//          those wrappers themselves (while mat_pbr_override is on) all draw through DrawPBR.
//          Native shaders: pbr_world_vs51/ps51 (lightmapped geometry) and pbr_model_vs51/ps51 (studio models).
//
//===========================================================================//
#ifndef PBR_HELPER_H
#define PBR_HELPER_H
#pragma once

#include "BaseVSShaderDX12.h"
#include "lightmappedgeneric_dx9_helper.h"
#include "vertexlitgeneric_dx9_helper.h"

//-----------------------------------------------------------------------------
// VMT keys every PBR-capable shader declares. The original wrappers declare them too, so the values survive in the
// material until mat_pbr_override routes it through DrawPBR.
//-----------------------------------------------------------------------------
#define PBR_SHADER_PARAMS \
	SHADER_PARAM( MRAOTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Metalness (r), roughness (g), ambient occlusion (b)" ) \
	SHADER_PARAM( METALNESS, SHADER_PARAM_TYPE_FLOAT, "0.0", "Metalness; multiplies $mraotexture.r when present" ) \
	SHADER_PARAM( ROUGHNESS, SHADER_PARAM_TYPE_FLOAT, "0.6", "Roughness; multiplies $mraotexture.g when present" ) \
	SHADER_PARAM( AMBIENTOCCLUSION, SHADER_PARAM_TYPE_FLOAT, "1.0", "Ambient occlusion; multiplies $mraotexture.b when present" ) \
	SHADER_PARAM( SPECULARTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "PBR_Specular: F0 (sRGB)" ) \
	SHADER_PARAM( SPECULARCOLOR, SHADER_PARAM_TYPE_COLOR, "[0.04 0.04 0.04]", "PBR_Specular: F0 without $speculartexture" ) \
	SHADER_PARAM( EMISSIONTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Emission (sRGB), scaled by $selfillumtint" ) \
	SHADER_PARAM( SUBSURFACE, SHADER_PARAM_TYPE_FLOAT, "0.0", "Wrap lighting / subsurface amount" ) \
	SHADER_PARAM( SUBSURFACETINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Subsurface color" ) \
	SHADER_PARAM( BACKLIGHT, SHADER_PARAM_TYPE_FLOAT, "0.0", "Transmitted (backlit) lighting amount" ) \
	SHADER_PARAM( BACKLIGHTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Transmitted light color" ) \
	SHADER_PARAM( THICKNESS, SHADER_PARAM_TYPE_FLOAT, "0.5", "Thickness (0 thin .. 1 thick) without $thicknesstexture" ) \
	SHADER_PARAM( THICKNESSTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "Thickness (r)" ) \
	SHADER_PARAM( ANISOTROPY, SHADER_PARAM_TYPE_FLOAT, "0.0", "Specular anisotropy -1..1" ) \
	SHADER_PARAM( ANISOTROPYROTATION, SHADER_PARAM_TYPE_FLOAT, "0.0", "Anisotropy direction rotation in degrees" ) \
	SHADER_PARAM( ENVMAPPARALLAXOBB1, SHADER_PARAM_TYPE_VEC4, "", "Parallax box row 0 (world to unit box), written by VBSP" ) \
	SHADER_PARAM( ENVMAPPARALLAXOBB2, SHADER_PARAM_TYPE_VEC4, "", "Parallax box row 1, written by VBSP" ) \
	SHADER_PARAM( ENVMAPPARALLAXOBB3, SHADER_PARAM_TYPE_VEC4, "", "Parallax box row 2, written by VBSP" ) \
	SHADER_PARAM( ENVMAPORIGIN, SHADER_PARAM_TYPE_VEC3, "", "Cubemap capture origin, written by VBSP" )

// Expand inside a shader class (the parameter names are per-shader statics) to fill the PBR_SHADER_PARAMS indices.
#define PBR_FILL_EXTRA_VARS( v ) \
	( v ).mrao = MRAOTEXTURE; \
	( v ).metalness = METALNESS; \
	( v ).roughness = ROUGHNESS; \
	( v ).ambientOcclusion = AMBIENTOCCLUSION; \
	( v ).specularTexture = SPECULARTEXTURE; \
	( v ).specularColor = SPECULARCOLOR; \
	( v ).emissionTexture = EMISSIONTEXTURE; \
	( v ).subsurface = SUBSURFACE; \
	( v ).subsurfaceTint = SUBSURFACETINT; \
	( v ).backlight = BACKLIGHT; \
	( v ).backlightTint = BACKLIGHTTINT; \
	( v ).thickness = THICKNESS; \
	( v ).thicknessTexture = THICKNESSTEXTURE; \
	( v ).anisotropy = ANISOTROPY; \
	( v ).anisotropyRotation = ANISOTROPYROTATION; \
	( v ).parallaxObb1 = ENVMAPPARALLAXOBB1; \
	( v ).parallaxObb2 = ENVMAPPARALLAXOBB2; \
	( v ).parallaxObb3 = ENVMAPPARALLAXOBB3; \
	( v ).envmapOrigin = ENVMAPORIGIN

//-----------------------------------------------------------------------------
// Material var indices, -1 = absent. Filled by every PBR shader / wrapper.
//-----------------------------------------------------------------------------
struct PBR_Vars_t
{
	PBR_Vars_t()
	{
		memset( this, 0xFF, sizeof( *this ) );
		model = standalone = specularWorkflow = false;
	}

	// Shared with the legacy helpers
	int base, baseFrame, baseTransform;
	int bump, bumpFrame, bumpTransform;
	int envmap, envmapFrame, envmapMask, envmapMaskFrame, envmapTint;
	int detail, detailFrame, detailTransform, detailScale, detailBlendMode, detailBlendFactor, detailTint;
	int selfIllumTint;
	int alphaTestReference;

	// World: second layer ($basetexture2 / $bumpmap2 blended by vertex alpha or $blendmodulatetexture), $ssbump, $nodiffusebumplighting,
	// $maskedblending / $alpha2 / $envmapmasktransform / $bumptransform2 / $bumpmask (lightmappedgeneric_ps51.fxc semantics)
	int base2, base2Frame, bump2, bump2Frame, blendModulate, blendMaskTransform;
	int ssBump, noDiffuseBumpLighting;
	int maskedBlending, alpha2, envmapMaskTransform, bumpTransform2, bumpMask, baseTextureNoEnvmap;

	// Model: legacy Phong adapter and the unbumped $ambientonly debug mode
	int phong, phongExponent, phongExponentTexture, baseMapAlphaPhongMask, ambientOnly;

	// PBR_Metalness / PBR_Specular only
	int normalTexture;

	// PBR_SHADER_PARAMS
	int mrao, metalness, roughness, ambientOcclusion, specularTexture, specularColor, emissionTexture;
	int subsurface, subsurfaceTint, backlight, backlightTint, thickness, thicknessTexture, anisotropy, anisotropyRotation;
	int parallaxObb1, parallaxObb2, parallaxObb3, envmapOrigin;

	// Legacy features the PBR shaders do not render (read by PBR_LegacySupported)
	int seamlessScale, lightWarp, outline, softEdges;
	int treeSway, distanceAlpha, seamlessBase, seamlessDetail, selfIllumFresnel, selfIllumMask, lightmap;

	bool model;				// studio-model draw (pbr_model_*); PBR_Metalness / PBR_Specular take it from the $model material flag
	bool standalone;		// no legacy helper initializes the material (PBR_Metalness / PBR_Specular)
	bool specularWorkflow;	// F0 from $speculartexture / $specularcolor instead of $metalness
};

// Legacy var tables -> PBR vars (world: LightmappedGeneric and WorldVertexTransition share one table).
void PBR_FillWorldVars( PBR_Vars_t &v, const LightmappedGeneric_DX9_Vars_t &info );
void PBR_FillModelVars( PBR_Vars_t &v, const VertexLitGeneric_DX9_Vars_t &info );

// SHADER_INIT_PARAMS of every PBR-capable shader (the three wrappers, the adapters, PBR_Metalness / PBR_Specular): the
// neutral values of the PBR keys. The default strings in SHADER_PARAM are editor help only; the engine initializes every
// unset float / color / vector parameter to 0 / (1 1 1) / 0 right after SHADER_INIT_PARAMS, so "unset" is observable
// only here and these values must be written here.
void InitParamsPBRDefaults( IMaterialVar **params, const PBR_Vars_t &v );
// PBR_Metalness / PBR_Specular only: flags and defaults the legacy InitParams helpers set for the other shaders.
void InitParamsPBR( IMaterialVar **params, const PBR_Vars_t &v );
// Loads every present texture the legacy helper (if any) did not load.
void InitPBR( CBaseVSShaderDX12 *pShader, IMaterialVar **params, const PBR_Vars_t &v );

// True while mat_pbr_override is committed on (the value the client commits at FRAME_START, never the cvar itself).
bool DX12PbrOverride();
// False: the material uses a feature the PBR shaders do not render and the original wrapper keeps its legacy helper.
bool PBR_LegacySupported( IMaterialVar **params, const PBR_Vars_t &v );

void DrawPBR( CBaseVSShaderDX12 *pShader, IMaterialVar **params, IShaderDynamicAPI *pShaderAPI, IShaderShadow *pShaderShadow,
	const PBR_Vars_t &v, VertexCompressionType_t vertexCompression, CBasePerMaterialContextData **pContextDataPtr );

#endif // PBR_HELPER_H
