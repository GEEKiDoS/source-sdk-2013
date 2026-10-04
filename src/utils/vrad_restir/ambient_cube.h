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

#endif // VRAD_RESTIR_AMBIENT_CUBE_H
