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
	SHADER_PARAM( ENVMAPORIGIN, SHADER_PARAM_TYPE_VEC3, "", "Cubemap capture origin, written by VBSP" ) \
	SHADER_PARAM( PBR_LOCAL_ENVMAP, SHADER_PARAM_TYPE_INTEGER, "0", "Internal model env_cubemap routing state" )

#define PBR_L4N_SHADER_PARAMS \
	SHADER_PARAM( MRAOFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "$mraotexture frame" ) \
	SHADER_PARAM( MRAOSCALE, SHADER_PARAM_TYPE_VEC3, "[1 0.5 1]", "Per-channel MRAO strength" ) \
	SHADER_PARAM( EMISSIONFRAME, SHADER_PARAM_TYPE_INTEGER, "0", "$emissiontexture frame" ) \
	SHADER_PARAM( EMISSIONTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Emission color and strength" ) \
	SHADER_PARAM( EMISSIONFLASHLIGHTTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Emission tint under the flashlight (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIONFRESNEL, SHADER_PARAM_TYPE_VEC4, "[1 1 1 2]", "Emission fresnel RGB multiplier and exponent" ) \
	SHADER_PARAM( ALLOWDIFFUSEMODULATION, SHADER_PARAM_TYPE_BOOL, "1", "Allow per-instance diffuse modulation" ) \
	SHADER_PARAM( BLENDTINTBYMRAOALPHA, SHADER_PARAM_TYPE_BOOL, "0", "Tint by MRAO alpha" ) \
	SHADER_PARAM( BASETEXTURE2, SHADER_PARAM_TYPE_TEXTURE, "", "Second albedo layer" ) \
	SHADER_PARAM( FRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "$basetexture2 frame" ) \
	SHADER_PARAM( TEXTURE2BLENDSHIFT, SHADER_PARAM_TYPE_FLOAT, "0.0", "Second layer blend shift (not supported by DX12 PBR)" ) \
	SHADER_PARAM( MRAOTEXTURE2, SHADER_PARAM_TYPE_TEXTURE, "", "Second MRAO layer (not supported by DX12 PBR)" ) \
	SHADER_PARAM( MRAOFRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "$mraotexture2 frame" ) \
	SHADER_PARAM( MRAOSCALE2, SHADER_PARAM_TYPE_VEC3, "[1 0.5 1]", "Second MRAO strength (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIONTEXTURE2, SHADER_PARAM_TYPE_TEXTURE, "", "Second emission layer (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIONFRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "$emissiontexture2 frame" ) \
	SHADER_PARAM( EMISSIONTINT2, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "Second emission tint (not supported by DX12 PBR)" ) \
	SHADER_PARAM( BUMPMAP2, SHADER_PARAM_TYPE_TEXTURE, "", "Second normal layer" ) \
	SHADER_PARAM( BUMPFRAME2, SHADER_PARAM_TYPE_INTEGER, "0", "$bumpmap2 frame" ) \
	SHADER_PARAM( BASETEXTURE2TINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1 1]", "Second albedo tint (not supported by DX12 PBR)" ) \
	SHADER_PARAM( PARALLAX, SHADER_PARAM_TYPE_BOOL, "0", "Height parallax (not supported by DX12 PBR)" ) \
	SHADER_PARAM( PARALLAXDEPTH, SHADER_PARAM_TYPE_FLOAT, "0.003", "Height parallax depth (not supported by DX12 PBR)" ) \
	SHADER_PARAM( PARALLAXCENTER, SHADER_PARAM_TYPE_FLOAT, "0.5", "Height parallax center (not supported by DX12 PBR)" ) \
	SHADER_PARAM( PARALLAXSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "Height parallax sample count (not supported by DX12 PBR)" ) \
	SHADER_PARAM( PARALLAXDITHER, SHADER_PARAM_TYPE_BOOL, "1", "Height parallax dithering (not supported by DX12 PBR)" ) \
	SHADER_PARAM( OUTLINE, SHADER_PARAM_TYPE_BOOL, "0", "NekoToon outline (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDENABLED, SHADER_PARAM_TYPE_BOOL, "0", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDBASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDFLOWTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDTINT, SHADER_PARAM_TYPE_COLOR, "[1 1 1]", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDSTRENGTH, SHADER_PARAM_TYPE_FLOAT, "1", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( EMISSIVEBLENDSCROLLVECTOR, SHADER_PARAM_TYPE_VEC2, "[0 0]", "VLG emissive blend (not supported by DX12 PBR)" ) \
	SHADER_PARAM( F0, SHADER_PARAM_TYPE_FLOAT, "0.04", "Dielectric F0 for the metalness workflow" )

#define PBR_FILL_L4N_VARS( v ) \
	( v ).mraoFrame = MRAOFRAME; \
	( v ).mraoScale = MRAOSCALE; \
	( v ).emissionFrame = EMISSIONFRAME; \
	( v ).emissionTint = EMISSIONTINT; \
	( v ).emissionFlashlightTint = EMISSIONFLASHLIGHTTINT; \
	( v ).emissionFresnel = EMISSIONFRESNEL; \
	( v ).blendTintByMraoAlpha = BLENDTINTBYMRAOALPHA; \
	( v ).base2 = BASETEXTURE2; \
	( v ).base2Frame = FRAME2; \
	( v ).mrao2 = MRAOTEXTURE2; \
	( v ).mrao2Frame = MRAOFRAME2; \
	( v ).mrao2Scale = MRAOSCALE2; \
	( v ).emission2 = EMISSIONTEXTURE2; \
	( v ).emission2Frame = EMISSIONFRAME2; \
	( v ).emission2Tint = EMISSIONTINT2; \
	( v ).bump2 = BUMPMAP2; \
	( v ).bump2Frame = BUMPFRAME2; \
	( v ).base2Tint = BASETEXTURE2TINT; \
	( v ).f0 = F0

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
	( v ).envmapOrigin = ENVMAPORIGIN; \
	( v ).localEnvmap = PBR_LOCAL_ENVMAP

// Legacy adapters only (LightmappedGeneric, VertexLitGeneric, WorldVertexTransition): $surfaceprop plus the internal state
// CaptureLegacyAdapterState records at SHADER_INIT_PARAMS, before the legacy helper can rewrite $envmap.
#define PBR_LEGACY_ADAPTER_PARAMS \
	SHADER_PARAM( SURFACEPROP, SHADER_PARAM_TYPE_STRING, "", "Physics surface property; picks the default PBR roughness" ) \
	SHADER_PARAM( PBR_LEGACY_ENVMAP_AUTHORED, SHADER_PARAM_TYPE_INTEGER, "0", "Internal legacy PBR adapter state" ) \
	SHADER_PARAM( PBR_LEGACY_ROUGHNESS, SHADER_PARAM_TYPE_FLOAT, "0.6", "Internal legacy PBR adapter state" )
#define PBR_FILL_LEGACY_ADAPTER_VARS( v ) \
	( v ).surfaceProp = SURFACEPROP; \
	( v ).legacyEnvmapAuthored = PBR_LEGACY_ENVMAP_AUTHORED; \
	( v ).legacyRoughness = PBR_LEGACY_ROUGHNESS

//-----------------------------------------------------------------------------
// Material var indices, -1 = absent. Filled by every PBR shader / wrapper.
//-----------------------------------------------------------------------------
struct PBR_Vars_t
{
	PBR_Vars_t()
	{
		memset( this, 0xFF, sizeof( *this ) );
		model = standalone = specularWorkflow = infected = l4n = dynamicSpecularWorkflow = false;
	}
	// Shared with the legacy helpers
	int base, baseFrame, baseTransform;
	int bump, bumpFrame, bumpTransform;
	int envmap, envmapFrame, envmapMask, envmapMaskFrame, envmapTint;
	int detail, detailFrame, detailTransform, detailScale, detailBlendMode, detailBlendFactor, detailTint;
	int selfIllumTint, emissionTint, emissionFlashlightTint, emissionFresnel;
	int alphaTestReference;

	// World: second layer ($basetexture2 / $bumpmap2 blended by vertex alpha or $blendmodulatetexture), $ssbump, $nodiffusebumplighting,
	// $maskedblending / $alpha2 / $envmapmasktransform / $bumptransform2 / $bumpmask (lightmappedgeneric_ps51.fxc semantics)
	int base2, base2Frame, bump2, bump2Frame, blendModulate, blendMaskTransform;
	int ssBump, noDiffuseBumpLighting;
	int maskedBlending, alpha2, envmapMaskTransform, bumpTransform2, bumpMask, baseTextureNoEnvmap;

	// Model: legacy Phong adapter and the unbumped $ambientonly debug mode
	int phong, phongExponent, phongExponentTexture, baseMapAlphaPhongMask, ambientOnly;
	int phongTint, phongAlbedoTint, phongBoost, phongFresnelRanges, envmapFresnel, baseMapLuminancePhongMask, diffuseExponent;
	int phongWarp, rimLight, shinyBlood, shinyBloodExponent, flashlightNoLambert, blendTintByBaseAlpha, linearWrite, allowDiffuseModulation;
	// PBR_Metalness / PBR_Specular only
	int normalTexture;

	// PBR_SHADER_PARAMS
	int mrao, mraoFrame, mraoScale, mrao2, mrao2Frame, mrao2Scale, metalness, roughness, ambientOcclusion, specularTexture, specularColor, emissionTexture, emissionFrame, emission2, emission2Frame, emission2Tint;
	int subsurface, subsurfaceTint, backlight, backlightTint, thickness, thicknessTexture, anisotropy, anisotropyRotation;
	int parallaxObb1, parallaxObb2, parallaxObb3, envmapOrigin, localEnvmap;
	int base2Tint, f0, blendTintByMraoAlpha;
	// Legacy adapters only (PBR_LEGACY_ADAPTER_PARAMS); -1 elsewhere.
	int surfaceProp, legacyEnvmapAuthored, legacyRoughness;

	// Legacy features the PBR shaders still decline (read by PBR_LegacySupported)
	int seamlessScale, lightWarp, outline, softEdges;
	int distanceAlpha, seamlessBase, seamlessDetail, selfIllumFresnel, lightmap;

	// L4D2 model controls implemented by the PBR adapter.
	int treeSway, selfIllumMask;

	// World: L4D2 SSBump correction and blend-tint controls.
	int ssBumpMathFix, vertexFog;

	// L4D2 tree sway material rows (staged by the model adapter).
	int treeSwayHeight, treeSwayStartHeight, treeSwayRadius, treeSwayStartRadius;
	int treeSwaySpeed, treeSwaySpeedHighWindMultiplier, treeSwayStrength;
	int treeSwayScrumbleSpeed, treeSwayScrumbleStrength, treeSwayScrumbleFrequency;
	int treeSwayFalloffExp, treeSwayScrumbleFalloffExp, treeSwaySpeedLerpStart, treeSwaySpeedLerpEnd;
	int treeSwayStatic;

	// L4D2 Infected (PBR_Infected; the infected_dx9.cpp vocabulary). Palette rows, burn strength and the wound packets
	// are rewritten by the actor proxies before every draw and are staged per draw.
	int infDisableVariation, infGradient, infBurning, infBurnTexture, infBurnStrength, infBloodColor, infBloodMaskRange;
	int infEyeGlow, infEyeGlowColor, infDefaultExponent, infSkinExponent, infDetailExponent, infBloodExponent;
	int infBloodSpecBoost, infSheetIndex, infSkinTint, infColorTint, infWounded, infWoundTexture;
	int infCutoutBias, infMappingScale, infDebugEllipsoids, infEllipsoid2CullType;
	int infEllipsoidCenter[2], infEllipsoidLookAt[2], infEllipsoidUp[2], infEllipsoidScale[2];
	int infWoundEnabled[2], infWoundUVMapping[2], infWoundUVScale[2];
	bool model;				// studio-model draw (pbr_model_*); PBR_Metalness / PBR_Specular take it from the $model material flag
	bool standalone;		// no legacy helper initializes the material (PBR_Metalness / PBR_Specular)
	bool specularWorkflow;	// F0 from $speculartexture / $specularcolor instead of $metalness
	bool infected;			// PBR_Infected: pbr_model_* INFECTED variants (infected albedo, wound cutouts, eye glow)
	bool l4n;				// Left4Neko/Momentum-style PBR parameter contract
	bool dynamicSpecularWorkflow;	// L4N switches to specular workflow when $speculartexture is present
};

// Legacy var tables -> PBR vars (world: LightmappedGeneric and WorldVertexTransition share one table).
void PBR_FillWorldVars( PBR_Vars_t &v, const LightmappedGeneric_DX9_Vars_t &info );
void PBR_FillModelVars( PBR_Vars_t &v, const VertexLitGeneric_DX9_Vars_t &info );
// Legacy adapter SHADER_INIT_PARAMS, before the legacy helper runs: whether the VMT supplied $envmap (the helper can clear
// or rewrite it) and the default roughness of the material's $surfaceprop class.
void CaptureLegacyAdapterState( IMaterialVar **params, const PBR_Vars_t &v );
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
