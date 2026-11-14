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
// Dense SH-L2 ambient probe grid, written as the .hprobe member paired to the .hlight. No-op without
// -restir_shadowmaps. Runs after the leaf ambient stage and UploadFinalLightmap.
bool ReSTIR_CaptureAmbientProbes(const ReSTIROptions &, const ReSTIRScene &, CReSTIRVulkanDevice &);
// -restir_shadowmaps output: builds the .hlight, .hprobe and rshd manifest of the captured HDR lighting and writes
// them to <map>.light beside the input BSP, which is not modified.
bool ReSTIR_WriteLightSidecar(const ReSTIROptions &);
// Actual baked selected-local face planes in Source units; excludes overflow
// exceptions. Palette-plane-luxel order, computed once per diagnostic face.
bool ReSTIR_BakedLocalDirectFace(const ReSTIRScene &, const ReSTIRLightmapResult &,
    const ReSTIRGpuFace &, int styles[MAXLIGHTMAPS], int &styleCount, CUtlVector<Vector> &);
#endif
