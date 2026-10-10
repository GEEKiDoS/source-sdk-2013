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

// Computes the factor for options.hdr's mode and stores it in the loaded entities (UnparseEntities), removing a stale
// key when the sky or ambient cannot be matched. The entity lump is not part of any .hlight identity. Never fails the
// bake: an unsupported sky only warns and leaves the map without a key.
void ReSTIR_WriteSkyAmbientMatch( const ReSTIROptions &options, const ReSTIRScene &scene );

#endif // RESTIR_SKY_AMBIENT_H
