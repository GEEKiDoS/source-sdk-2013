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
// Actual baked selected-local face planes in Source units; excludes overflow
// exceptions. Palette-plane-luxel order, computed once per diagnostic face.
bool ReSTIR_BakedLocalDirectFace(const ReSTIRScene &, const ReSTIRLightmapResult &,
    const ReSTIRGpuFace &, int styles[MAXLIGHTMAPS], int &styleCount, CUtlVector<Vector> &);
int ReSTIR_SelectedLightCount();
bool ReSTIR_FinishSelectedLighting(const ReSTIROptions &, int selected);
// Called only by the whole-scene paired reuse branch, after both RGB captures.
bool ReSTIR_MarkPairedVisibilityReuse();
#endif
