//========= Copyright Valve Corporation, All rights reserved. ============//
// Vulkan compute backend for VRAD ReSTIR.
#ifndef RESTIR_VULKAN_H
#define RESTIR_VULKAN_H
#pragma once

#include "restir_types.h"

class CReSTIRVulkanDevice
{
public:
	CReSTIRVulkanDevice();
	~CReSTIRVulkanDevice();

	bool Init( const ReSTIROptions &options );
	void Shutdown();
	const ReSTIRDeviceInfo &GetDeviceInfo() const;
	bool UploadScene( const ReSTIRScene &scene );
	// Re-uploads faces after ReSTIR_ResolveFaceStyles; styles may only shrink.
	bool UpdateFaceStyles( const ReSTIRScene &scene );
	bool BakeLightmaps( const ReSTIROptions &options, ReSTIRLightmapResult &result );
	bool UploadFinalLightmap( const ReSTIRLightmapResult &result );
	bool TraceRays( const CUtlVector<ReSTIRGpuRay> &rays, unsigned int triangleMask, CUtlVector<ReSTIRGpuHit> &hits );
	bool GatherAmbient( const CUtlVector<ReSTIRGpuAmbientQuery> &queries, CUtlVector<ReSTIRGpuAmbientResult> &results );
	bool LightPoints( const CUtlVector<ReSTIRGpuPointQuery> &queries, CUtlVector<ReSTIRGpuPointResult> &results );
	const ReSTIRGpuTimings &GetTimings() const;
	struct Impl;

private:
	Impl *m_pImpl;
};

#endif // RESTIR_VULKAN_H
