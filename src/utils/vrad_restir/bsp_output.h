//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Serialize ordinary ReSTIR lighting through the BSP lighting contracts in
//          one temporary-BSP transaction, and capture -restir_shadowmaps receivers.
//
//=============================================================================//

#ifndef RESTIR_BSP_OUTPUT_H
#define RESTIR_BSP_OUTPUT_H
#pragma once

#include "restir_types.h"
#include "tier1/utlstring.h"

// Resets the enhanced storage for this bake and rejects inputs carrying an older in-BSP enhanced bake.
// With -restir_shadowmaps it also records the input's HDR worldlights before the ambient stage edits them.
bool ReSTIR_PrepareShadowMapStorage( const ReSTIROptions &options );
bool ReSTIR_InvalidateShadowMapTopology( const ReSTIROptions &options );
bool ReSTIR_StageReceiverPakFile( const char *name, const void *data, int bytes, bool compress = false );

class CReSTIRBSPOutput
{
public:
	CReSTIRBSPOutput() : m_LastLightingBytes( 0 ), m_LastWorldlights( 0 ), m_LastGameLumpCount( 0 ), m_LastGameLumpBytes( 0 ) {}
	// -restir_shadowmaps: captures every admitted authored float style/bump plane with the ordinary
	// encode-time minlight/macro treatment. No BSP lump is touched.
	bool CaptureHighres( const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result );
	// Ordinary output: reserves *pdlightdata like PrecompLightmapOffsets, encodes luxels and
	// per-channel median averages, and exports every scene worldlight.
	bool EncodeLightmaps( const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result );
	bool Validate( const ReSTIROptions &options );
	bool Write( const ReSTIROptions &options );
	void PrintWriteSummary() const;

private:
	CUtlString m_LastWritePath;
	int m_LastLightingBytes;
	int m_LastWorldlights;
	int m_LastGameLumpCount;
	int m_LastGameLumpBytes;
};

#endif // RESTIR_BSP_OUTPUT_H
