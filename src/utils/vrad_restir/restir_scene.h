//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_SCENE_H
#define RESTIR_SCENE_H
#pragma once

#include "restir_types.h"

class CReSTIRSceneBuilder
{
public:
	// Builds the deterministic CPU scene consumed by the Vulkan backend.
	// shadowMaps resolves entity selection/provenance and full-record shadowLights once (sun first, then locals);
	// shadow-only emitter sizes never alter sunSpreadAngle or the transport's light sampling.
	bool Build( const ReSTIROptions &options, ReSTIRScene &scene );
};

class CReSTIRVulkanDevice;
// After UploadScene: resolves faces with more than MAXLIGHTMAPS source candidates using GPU
// shadow rays and re-uploads the surviving source slots. Only receiver styles consume the native budget.
bool ReSTIR_ResolveFaceStyles( ReSTIRScene &scene, CReSTIRVulkanDevice &device );

// Returns the material index, creating it on first use. pOutAlphaTested is
// true only when material alpha can affect visibility under textureShadows.
int ReSTIR_GetOrAddMaterial( ReSTIRScene &scene, const char *pMaterialName,
	const Vector &reflectivity, bool textureShadows, bool *pOutAlphaTested );
// Enables per-texel bounce albedo on an already registered lit material (-restir_texturealbedo).
void ReSTIR_LoadMaterialAlbedo( ReSTIRScene &scene, int materialIndex, const char *pMaterialName, int textureWidth, int textureHeight );

// Emission of a material as the engine shades it, cached by material name for the scene build:
//  - UnlitGeneric: base texture rgb * $color * $color2 (both linearized when <= 1), * alpha with
//    $translucent, * (alpha >= $alphatestreference) with $alphatest; $hdrcolorscale in HDR passes.
//  - $selfillum on any other shader: $selfillumtint * base rgb * $color * $color2 * mask, mask = base alpha,
//    or $selfillummask rgb when that texture is set; the engine disables $selfillum without a mask
//    source (base texture without an alpha channel), so does this.
// Base texture rgb is linearized with VBSP's 2.2 curve. `intensity` is the linear per-area emission in
// VRAD light units (a surface displaying linear colour D emits D * 255 / pi) times options.emissiveScale,
// and `texture` the per-texel factor (scene texture index, RGBA8, rgb = factor^(1/2.2), -1 = uniform 1).
// Returns false for non-emissive materials, tool/render-target materials, unloadable base textures
// and emissions that are black everywhere.
struct ReSTIRMaterialEmission
{
	Vector	intensity;
	int		texture;
	bool	twoSided;		// $nocull
};
bool ReSTIR_GetMaterialEmission( ReSTIRScene &scene, const ReSTIROptions &options, const char *pMaterialName,
	ReSTIRMaterialEmission &emission );
// Mean linear emission factor (rgb) of `texture` over a triangle with normalized UVs uv[6] (repeat
// addressing); (1,1,1) for texture < 0. Host-side estimate for emitter importance sampling.
Vector ReSTIR_EmissionTriangleMean( const ReSTIRScene &scene, int texture, const float uv[6] );

// Appends one emit_surface light over `count` front-facing triangles (copied into
// scene.emitterTriangles; uv.w is overwritten with the selection CDF built from area * mean emission,
// mixed with a pure area term so every emitting point keeps a non-zero pdf). Fills origin, mean
// normal, bounding radius and power estimate (restir_types.h ReSTIRGpuLight). Style 0. Returns the
// light index, or -1 when the triangles have no area or emit nothing.
int ReSTIR_AddSurfaceEmitter( ReSTIRScene &scene, const ReSTIRGpuEmitterTriangle *pTriangles, int count,
	const Vector &intensity, int texture, int lightFlags );

#endif // RESTIR_SCENE_H
