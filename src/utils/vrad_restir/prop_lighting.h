//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_PROP_LIGHTING_H
#define RESTIR_PROP_LIGHTING_H
#pragma once
#include "restir_types.h"
class CReSTIRVulkanDevice;
void VRadRestirDetailProps_SetHDRMode( bool bHDR );
// Local visibility is always computed for static receivers; staticPropLighting
// controls native RGB only. Successful mode data are exported from restir_staticprops.h.
bool ReSTIR_ComputeStaticPropLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device );
bool ReSTIR_ComputeDetailPropLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device );
// Paired equivalent scenes only, while the completed LDR prop meshes and owned
// visibility remain alive. HDR aliases visibility and is the final shared RGB writer.
bool ReSTIR_ReuseStaticPropLighting( const ReSTIROptions &options );
bool ReSTIR_ReuseDetailPropLighting();
#endif
