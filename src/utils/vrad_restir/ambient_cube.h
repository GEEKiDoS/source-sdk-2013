//========= Copyright Valve Corporation, All rights reserved. ============//
// Per-leaf ambient cube generation for VRAD ReSTIR.
#ifndef VRAD_RESTIR_AMBIENT_CUBE_H
#define VRAD_RESTIR_AMBIENT_CUBE_H
#pragma once

#include "restir_types.h"

class CReSTIRVulkanDevice;

// Runs after denoising and UploadFinalLightmap. Writes the selected mode's
// ambient lumps and flags the exported worldlights used by ambient cubes.
bool ReSTIR_ComputeLeafAmbientLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, const ReSTIRLightmapResult &lightmap );

// Leaf/worldlight algorithms of the leaf cube, shared with the dense probe grid (ambient_probes.cpp).
int ReSTIR_PointLeaf( const Vector &point );
bool ReSTIR_PointInOpaqueLeafBrush( int leafIndex, const Vector &point );
// Distance falloff * angle of a surface worldlight towards a position (0: not lit); callers trace the visibility.
float ReSTIR_AmbientWorldLightRatio( const dworldlight_t &light, const Vector &position );
// A candidate-point probe ray invalidates the point as in VRAD CastRayInLeaf: t == 0 (start inside an opaque brush) or
// a displacement back face. `delta` is the full ray vector (hit.t is a fraction of it).
bool ReSTIR_AmbientRayBlocked( const ReSTIRScene &scene, const ReSTIRGpuHit &hit, const Vector &delta );

#endif // VRAD_RESTIR_AMBIENT_CUBE_H
