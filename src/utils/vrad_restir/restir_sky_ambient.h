//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Bakes the 2D skybox / light_environment ambient brightness match (client r_sky_ambientmatch) into the
//          loaded BSP's worldspawn: _skyscale_ldr / _skyscale_hdr, the factor that makes the sky's cosine-weighted
//          mean luminance equal the ambient luminance of that lighting mode.
//
//=============================================================================//
#ifndef RESTIR_SKY_AMBIENT_H
#define RESTIR_SKY_AMBIENT_H
#pragma once

#include "restir_types.h"

// Computes the HDR factor and stores it in the loaded entities (UnparseEntities), removing a stale key when the sky or
// ambient cannot be matched. Ordinary (in-BSP) bakes only; a -restir_shadowmaps bake leaves the entity lump untouched.
// Never fails the bake: an unsupported sky only warns and leaves the map without a key.
void ReSTIR_WriteSkyAmbientMatch( const ReSTIROptions &options, const ReSTIRScene &scene );

// -restir_skyibl: composites the HDR skybox faces into scene.skyEnvironment (restir_types.h), normalized so an up-facing
// unoccluded surface receives the emit_skyambient luminance (which includes -restir_skyambientscale), with texels
// clamped to 8x the sphere's mean luminance so an HDR sun disc does not add a second sun. Leaves it empty (uniform
// ambient) with a warning when the sky has no HDR faces or cannot be read. Never fails the bake.
void ReSTIR_BuildSkyEnvironment( const ReSTIROptions &options, ReSTIRScene &scene );

#endif // RESTIR_SKY_AMBIENT_H
