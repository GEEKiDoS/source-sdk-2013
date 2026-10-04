//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_PROP_LIGHTING_H
#define RESTIR_PROP_LIGHTING_H
#pragma once
#include "restir_types.h"
class CReSTIRVulkanDevice;
void VRadRestirDetailProps_SetHDRMode( bool bHDR );
bool ReSTIR_ComputeStaticPropLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device );
bool ReSTIR_ComputeDetailPropLighting( const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device );
#endif
