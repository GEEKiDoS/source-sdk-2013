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
int ReSTIR_SelectedLightCount();
bool ReSTIR_FinishSelectedLighting(const ReSTIROptions &, int selected);
#endif
