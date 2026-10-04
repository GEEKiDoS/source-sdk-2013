//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Serialize ReSTIR lightmaps and worldlights into the ordinary BSP
//          lighting contracts.
//
//=============================================================================//

#ifndef RESTIR_BSP_OUTPUT_H
#define RESTIR_BSP_OUTPUT_H
#pragma once

#include "restir_types.h"

class CReSTIRBSPOutput
{
public:
	// Reserves *pdlightdata like PrecompLightmapOffsets, encodes luxels and
	// per-channel median averages, and exports the scene worldlights.
	bool EncodeLightmaps( const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result );
	bool Validate( const ReSTIROptions &options );
	bool Write( const ReSTIROptions &options );
};

#endif // RESTIR_BSP_OUTPUT_H
