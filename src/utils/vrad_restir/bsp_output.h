//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Serialize ordinary and shadow-map-owned ReSTIR receivers through
//          the BSP lighting contracts and one temporary-BSP transaction.
//
//=============================================================================//

#ifndef RESTIR_BSP_OUTPUT_H
#define RESTIR_BSP_OUTPUT_H
#pragma once

#include "restir_types.h"

// Imports checked high-resolution mode storage before mutation. The frozen v3
// format is accepted only as a rebake input; publication is always .hlight +
// rshd v4 selected lights. Topology changes invalidate imported enhanced modes.
// Paired bakes publish the final BSP once, after both passes succeed.
bool ReSTIR_PrepareShadowMapStorage( const ReSTIROptions &options );
bool ReSTIR_InvalidateShadowMapTopology( const ReSTIROptions &options );
bool ReSTIR_WriteShadowMapSidecar( const ReSTIROptions &options );
bool ReSTIR_StageReceiverPakFile( const char *name, const void *data, int bytes, bool compress = false );

class CReSTIRBSPOutput
{
public:
	// Reserves *pdlightdata like PrecompLightmapOffsets, encodes luxels and
	// per-channel median averages, and exports unselected scene worldlights.
	// A selected sun requires result.sunVisibility for every geometric luxel.
	// Captures every admitted authored float style/bump plane before RGBExp
	// pruning; native BSP RGB is sampled at its unchanged original endpoints.
	bool EncodeLightmaps( const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result );
	bool Validate( const ReSTIROptions &options );
	bool Write( const ReSTIROptions &options );
};

#endif // RESTIR_BSP_OUTPUT_H
