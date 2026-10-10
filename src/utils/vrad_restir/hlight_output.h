//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_HLIGHT_OUTPUT_H
#define RESTIR_HLIGHT_OUTPUT_H
#pragma once
#include "restir_types.h"
#include "hlight_bsp.h"
// Called before native RGBExp style pruning. Callback applies the ordinary
// encode-time minlight/macro treatment without native RGBExp/gamma conversion.
typedef Vector (*ReSTIRHighresColorFn)(void *, int, int, int, const Vector &);
bool ReSTIR_CaptureHighres(const ReSTIROptions &, const ReSTIRScene &, const ReSTIRLightmapResult &,
    ReSTIRHighresColorFn, void *);
class CReSTIRVulkanDevice;
// Dense SH-L2 ambient probe grid of the selected mode, written as the .hprobe pak member paired to the .hlight. No-op
// without -restir_shadowmaps. Runs after the leaf ambient stage and UploadFinalLightmap.
bool ReSTIR_CaptureAmbientProbes(const ReSTIROptions &, const ReSTIRScene &, CReSTIRVulkanDevice &);
// Paired reuse branch only: the HDR mode reuses the LDR transport and leaf ambient cubes, so it takes the LDR grid.
bool ReSTIR_ReusePairedAmbientProbes(const ReSTIROptions &);
// Actual baked selected-local face planes in Source units; excludes overflow
// exceptions. Palette-plane-luxel order, computed once per diagnostic face.
bool ReSTIR_BakedLocalDirectFace(const ReSTIRScene &, const ReSTIRLightmapResult &,
    const ReSTIRGpuFace &, int styles[MAXLIGHTMAPS], int &styleCount, CUtlVector<Vector> &);
int ReSTIR_SelectedLightCount();
bool ReSTIR_FinishSelectedLighting(const ReSTIROptions &, int selected);
// Called only by the whole-scene paired reuse branch, after both RGB captures.
bool ReSTIR_MarkPairedVisibilityReuse();
#endif
