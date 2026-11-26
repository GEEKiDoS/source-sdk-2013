//========= Copyright Valve Corporation, All rights reserved. ============//
// Dense baked SH-L2 ambient probe grid (public/hprobe_bsp.h) for VRAD ReSTIR.
#ifndef VRAD_RESTIR_AMBIENT_PROBES_H
#define VRAD_RESTIR_AMBIENT_PROBES_H
#pragma once

#include "restir_types.h"
#include "hprobe_bsp.h"

class CReSTIRVulkanDevice;

// One lighting mode's grid: exactly the grid section of the .hprobe file, in disk layout.
struct ReSTIRAmbientProbeGrid
{
	hprobe::GridDisk grid;
	CUtlVector<uint32> indirection, dc, freeDistance;
	CUtlVector<int8> bands;
	CUtlVector<uint8> validity;

	void Purge()
	{
		memset( &grid, 0, sizeof( grid ) );
		indirection.Purge();
		freeDistance.Purge();
		bands.Purge();
		validity.Purge();
	}
};

// The grid's brick allocation and budget check alone, from the built scene (the same triangles the build uses):
// run before the GPU bake so a grid over -restir_ambientgrid_budget fails in seconds, not after the lightmaps.
bool ReSTIR_PlanAmbientProbeGrid( const ReSTIROptions &options, const ReSTIRScene &scene );

// Runs after ReSTIR_ComputeLeafAmbientLighting (which flags the INAMBIENTCUBE surface worldlights) and
// UploadFinalLightmap. Folds the full-source lightmap gather, the INAMBIENTCUBE surface worldlights and the
// material emitters into per-probe SH L2 irradiance in the same units as the leaf cubes.
bool ReSTIR_BuildAmbientProbeGrid( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device, ReSTIRAmbientProbeGrid &out );

#endif // VRAD_RESTIR_AMBIENT_PROBES_H
