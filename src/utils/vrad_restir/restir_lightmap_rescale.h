//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Rewrites the loaded BSP so brush faces get denser lightmaps than
//          VBSP compiled: scales texinfo luxel vectors, recomputes face extents
//          and splits faces that exceed the 32-luxel brush lightmap limit the
//          same way VBSP does (utils/vbsp/faces.cpp:1167 SubdivideFace).
//
//=============================================================================//
#ifndef RESTIR_LIGHTMAP_RESCALE_H
#define RESTIR_LIGHTMAP_RESCALE_H
#pragma once

#include "restir_types.h"

// Applies options.lightmapScale (< 1.0 = denser) to the selected face array and every
// face-indexed lump. No-op when the scale is 1.0. Returns false (after Warning) when a
// BSP limit would be exceeded; the in-memory BSP is then left unmodified.
bool ReSTIR_RescaleLightmaps( const ReSTIROptions &options );

#endif // RESTIR_LIGHTMAP_RESCALE_H
