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
	// With selected shadow-map lights, bake full transport plus receiver RGB in the same iterations;
	// only receiver non-PATH direct is stripped. Ordinary results leave sourceRadiance empty.
	// Selected suns and locals produce independent geometric-luxel visibility, even at zero intensity.
	// No-selected bakes clear visibility; RGB denoising and source upload do not alter it.
	bool BakeLightmaps( const ReSTIROptions &options, ReSTIRLightmapResult &result );
	// Bounce/probe/prop gathers always read sourceRadiance when present, otherwise the ordinary radiance.
	bool UploadFinalLightmap( const ReSTIRLightmapResult &result );
	bool TraceRays( const CUtlVector<ReSTIRGpuRay> &rays, unsigned int triangleMask, CUtlVector<ReSTIRGpuHit> &hits );
	bool GatherAmbient( const CUtlVector<ReSTIRGpuAmbientQuery> &queries, CUtlVector<ReSTIRGpuAmbientResult> &results );
	// SH L2 irradiance projection of the final lightmap (style 0) plus material emitters at each position; one result per query.
	bool ProjectProbes( const CUtlVector<ReSTIRGpuAmbientQuery> &queries, CUtlVector<ReSTIRGpuProbeResult> &results );
	bool LightPoints( const CUtlVector<ReSTIRGpuPointQuery> &queries, CUtlVector<ReSTIRGpuPointResult> &results );
	// Immutable finite-segment disk visibility. Requires UploadScene, not UploadFinalLightmap.
	bool ComputeLocalVisibility( const CUtlVector<ReSTIRGpuVisibilityQuery> &queries, CUtlVector<float> &visibility );
	const ReSTIRGpuTimings &GetTimings() const;
	struct Impl;

private:
	Impl *m_pImpl;
};

#endif // RESTIR_VULKAN_H
