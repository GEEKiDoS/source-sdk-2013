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

// Imports checked high-resolution mode storage before mutation. Frozen rshd v3 and
// .hlight v1/v2/v3 formats are accepted only as rebake inputs; publication is always
// .hlight v4 + rshd v5. Topology changes invalidate imported enhanced modes.
// Paired bakes publish the final BSP once, after both passes succeed.
bool ReSTIR_PrepareShadowMapStorage( const ReSTIROptions &options );
bool ReSTIR_InvalidateShadowMapTopology( const ReSTIROptions &options );
bool ReSTIR_WriteShadowMapSidecar( const ReSTIROptions &options );
bool ReSTIR_StageReceiverPakFile( const char *name, const void *data, int bytes, bool compress = false );
// Equivalent pair on the same loaded BSP only, after both native encodings.
// Includes the ambient gather's native worldlight flag mutations.
bool ReSTIR_ReusePairedLeafAmbientLighting();

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
