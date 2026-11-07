//========= Copyright Valve Corporation, All rights reserved. ============//
#include "restir_vulkan_internal.h"
#include <limits.h>
#include <float.h>

void CReSTIRVulkanDevice::Impl::RunService( ReSTIRPipeline pipeline, const void *input, unsigned int inputStride, void *output, unsigned int outputStride, unsigned int count, unsigned int styles, unsigned int mask )
{
	if ( !scene || !pipelineLayout )
		Fail( "GPU service requires UploadScene" );
	if ( pipeline != RESTIR_PIPE_TRACE && pipeline != RESTIR_PIPE_LOCAL_VISIBILITY && !finalUploaded )
		Fail( "lightmap gather requires UploadFinalLightmap" );
	if ( !styles )
		Fail( "GPU services require at least style 0" );
	VkDeviceSize outputItemBytes = (VkDeviceSize)outputStride * styles;
	VkDeviceSize capacity = MIN( buffers[RESTIR_BIND_SERVICE_IN].size / inputStride, buffers[RESTIR_BIND_SERVICE_OUT].size / outputItemBytes );
	capacity = MIN( capacity, RESTIR_STAGING_BYTES / MAX( (VkDeviceSize)inputStride, outputItemBytes ) );
	if ( !capacity )
		Fail( "service item exceeds staging ring capacity" );
	for ( unsigned int first = 0; first < count; )
	{
		unsigned int chunk = (unsigned int)MIN( (VkDeviceSize)count - first, capacity );
		Upload( RESTIR_BIND_SERVICE_IN, (const unsigned char *)input + (VkDeviceSize)first * inputStride, (VkDeviceSize)chunk * inputStride );
		ResetQueries();
		push.rayMask = mask;
		push.seed = options.seed;
		VkCommandBuffer command = BeginCommands();
		unsigned int start = Timestamp( command );
		Dispatch( command, pipeline, chunk, pipeline == RESTIR_PIPE_LOCAL_VISIBILITY ? ~0u :
			( pipeline == RESTIR_PIPE_TRACE ? 0 : scene->faceNeighbors.Count() ) );
		Barrier( command );
		unsigned int end = Timestamp( command );
		Submit( command );
		Download( RESTIR_BIND_SERVICE_OUT, (unsigned char *)output + (VkDeviceSize)first * outputItemBytes, (VkDeviceSize)chunk * outputItemBytes );
		double duration = TimestampMs( start, end );
		if ( pipeline == RESTIR_PIPE_AMBIENT || pipeline == RESTIR_PIPE_PROBE_SH )
			timings.ambientMs += duration;
		else if ( pipeline == RESTIR_PIPE_POINTS )
			timings.propMs += duration;
		first += chunk;
	}
}

bool CReSTIRVulkanDevice::TraceRays( const CUtlVector<ReSTIRGpuRay> &rays, unsigned int triangleMask, CUtlVector<ReSTIRGpuHit> &hits )
{
	hits.SetCount( rays.Count() );
	m_pImpl->RunService( RESTIR_PIPE_TRACE, rays.Base(), sizeof( ReSTIRGpuRay ), hits.Base(), sizeof( ReSTIRGpuHit ), rays.Count(), 1, triangleMask );
	return true;
}

bool CReSTIRVulkanDevice::GatherAmbient( const CUtlVector<ReSTIRGpuAmbientQuery> &queries, CUtlVector<ReSTIRGpuAmbientResult> &results )
{
	Impl &gpu = *m_pImpl;
	if ( !gpu.scene )
		gpu.Fail( "GatherAmbient requires UploadScene" );
	uint64_t count = (uint64_t)queries.Count() * gpu.scene->sceneStyles.Count();
	if ( count > INT_MAX )
		gpu.Fail( "ambient result indexing exceeds signed 32-bit range" );
	results.SetCount( (int)count );
	gpu.RunService( RESTIR_PIPE_AMBIENT, queries.Base(), sizeof( ReSTIRGpuAmbientQuery ), results.Base(), sizeof( ReSTIRGpuAmbientResult ), queries.Count(), gpu.scene->sceneStyles.Count(), RESTIR_RAY_MASK_WORLDFACE );
	return true;
}

bool CReSTIRVulkanDevice::ProjectProbes( const CUtlVector<ReSTIRGpuAmbientQuery> &queries, CUtlVector<ReSTIRGpuProbeResult> &results )
{
	Impl &gpu = *m_pImpl;
	if ( !gpu.scene )
		gpu.Fail( "ProjectProbes requires UploadScene" );
	results.SetCount( queries.Count() );
	gpu.RunService( RESTIR_PIPE_PROBE_SH, queries.Base(), sizeof( ReSTIRGpuAmbientQuery ), results.Base(), sizeof( ReSTIRGpuProbeResult ), queries.Count(), 1, RESTIR_RAY_MASK_WORLDFACE );
	return true;
}

bool CReSTIRVulkanDevice::LightPoints( const CUtlVector<ReSTIRGpuPointQuery> &queries, CUtlVector<ReSTIRGpuPointResult> &results )
{
	Impl &gpu = *m_pImpl;
	if ( !gpu.scene )
		gpu.Fail( "LightPoints requires UploadScene" );
	uint64_t count = (uint64_t)queries.Count() * gpu.scene->sceneStyles.Count();
	if ( count > INT_MAX )
		gpu.Fail( "point result indexing exceeds signed 32-bit range" );
	results.SetCount( (int)count );
	gpu.RunService( RESTIR_PIPE_POINTS, queries.Base(), sizeof( ReSTIRGpuPointQuery ), results.Base(), sizeof( ReSTIRGpuPointResult ), queries.Count(), gpu.scene->sceneStyles.Count(), RESTIR_RAY_MASK_SHADOW );
	return true;
}

bool CReSTIRVulkanDevice::ComputeLocalVisibility( const CUtlVector<ReSTIRGpuVisibilityQuery> &queries, CUtlVector<float> &visibility )
{
	Impl &gpu = *m_pImpl;
	if ( !gpu.scene )
		gpu.Fail( "ComputeLocalVisibility requires UploadScene" );
	for ( int i = 0; i < queries.Count(); ++i )
	{
		const ReSTIRGpuVisibilityQuery &query = queries[i];
		if ( query.selectedLightIndex >= (unsigned int)gpu.scene->shadowLights.Count() ||
			( gpu.scene->shadowLights[query.selectedLightIndex].light.type != emit_point &&
				gpu.scene->shadowLights[query.selectedLightIndex].light.type != emit_spotlight ) ||
			query.reserved != 0 || ( query.flags & ~RESTIR_VISIBILITY_NO_SELF_SHADOW ) != 0 ||
			( query.position[3] != 0.0f && query.position[3] != 1.0f ) ||
			!_finite( query.position[0] ) || !_finite( query.position[1] ) || !_finite( query.position[2] ) )
			gpu.Fail( "local visibility query has an invalid canonical local index, origin, flags or reserved field" );
	}
	visibility.SetCount( queries.Count() );
	gpu.RunService( RESTIR_PIPE_LOCAL_VISIBILITY, queries.Base(), sizeof( ReSTIRGpuVisibilityQuery ),
		visibility.Base(), sizeof( float ), queries.Count(), 1, RESTIR_RAY_MASK_STATIC_SUN );
	for ( int i = 0; i < visibility.Count(); ++i )
		if ( !_finite( visibility[i] ) || visibility[i] < 0.0f || visibility[i] > 1.0f )
			gpu.Fail( "local visibility readback contains a nonfinite or out-of-range scalar" );
	return true;
}

bool CReSTIRVulkanDevice::UploadFinalLightmap( const ReSTIRLightmapResult &result )
{
	Impl &gpu = *m_pImpl;
	const CUtlVector<Vector> &radiance = result.sourceRadiance.Count() ? result.sourceRadiance : result.radiance;
	if ( !gpu.scene || radiance.Count() != gpu.scene->numOutputValues || result.radiance.Count() != gpu.scene->numOutputValues || result.luxelValid.Count() != gpu.scene->luxels.Count() )
		gpu.Fail( "final lightmap dimensions do not match the resident scene" );
	// Convert only one ring-sized page at a time; no second full-size radiance copy.
	CUtlVector<float> page;
	unsigned int capacity = (unsigned int)( RESTIR_STAGING_BYTES / ( sizeof( float ) * 4 ) );
	page.EnsureCapacity( MIN( capacity, MAX( (unsigned int)radiance.Count(), (unsigned int)gpu.scene->faces.Count() * RESTIR_MAX_FACE_STYLES ) ) * 4 );
	// Ambient/prop rays share one valid-base-luxel mean per face/style. Scanning the
	// dense face at every ray hit scales quadratically with density and can time out.
	// Prefix the existing final-lightmap buffer; no additional descriptor is needed.
	const VkDeviceSize averageBytes = (VkDeviceSize)gpu.scene->faces.Count() * RESTIR_MAX_FACE_STYLES * sizeof( float ) * 4;
	for ( unsigned int first = 0; first < (unsigned int)gpu.scene->faces.Count(); )
	{
		const unsigned int count = MIN( capacity / RESTIR_MAX_FACE_STYLES, (unsigned int)gpu.scene->faces.Count() - first );
		page.SetCount( count * RESTIR_MAX_FACE_STYLES * 4 );
		memset( page.Base(), 0, page.Count() * sizeof( float ) );
		for ( unsigned int i = 0; i < count; ++i )
		{
			const ReSTIRGpuFace &face = gpu.scene->faces[first + i];
			const int luxels = face.luxelW * face.luxelH;
			for ( int slot = 0; slot < face.numStyles; ++slot )
			{
				float *average = page.Base() + ( i * RESTIR_MAX_FACE_STYLES + slot ) * 4;
				unsigned int valid = 0;
				const int output = face.firstOutput + slot * face.numChannels * luxels;
				for ( int luxel = 0; luxel < luxels; ++luxel )
				{
					if ( !result.luxelValid[face.firstLuxel + luxel] ) continue;
					for ( int c = 0; c < 3; ++c )
					{
						const float value = radiance[output + luxel][c];
						average[c] += _finite( value ) && value >= 0 ? value : 0;
					}
					++valid;
				}
				if ( valid ) for ( int c = 0; c < 3; ++c ) average[c] /= (float)valid;
			}
		}
		gpu.Upload( RESTIR_BIND_FINAL_LIGHTMAP, page.Base(), (VkDeviceSize)count * RESTIR_MAX_FACE_STYLES * sizeof( float ) * 4,
			(VkDeviceSize)first * RESTIR_MAX_FACE_STYLES * sizeof( float ) * 4 );
		first += count;
	}
	for ( unsigned int first = 0; first < (unsigned int)radiance.Count(); )
	{
		unsigned int count = MIN( capacity, (unsigned int)radiance.Count() - first );
		page.SetCount( count * 4 );
		for ( unsigned int i = 0; i < count; ++i )
		{
			for ( int c = 0; c < 3; ++c )
			{
				float value = radiance[first + i][c];
				page[i * 4 + c] = _finite( value ) && value >= 0 ? value : 0;
			}
			page[i * 4 + 3] = 0;
		}
		gpu.Upload( RESTIR_BIND_FINAL_LIGHTMAP, page.Base(), (VkDeviceSize)count * sizeof( float ) * 4, averageBytes + (VkDeviceSize)first * sizeof( float ) * 4 );
		first += count;
	}
	CUtlVector<unsigned int> validPage;
	capacity = (unsigned int)( RESTIR_STAGING_BYTES / sizeof( unsigned int ) );
	for ( unsigned int first = 0; first < (unsigned int)result.luxelValid.Count(); )
	{
		unsigned int count = MIN( capacity, (unsigned int)result.luxelValid.Count() - first );
		validPage.SetCount( count );
		for ( unsigned int i = 0; i < count; ++i )
			validPage[i] = result.luxelValid[first + i];
		gpu.Upload( RESTIR_BIND_LUXEL_VALID, validPage.Base(), (VkDeviceSize)count * sizeof( unsigned int ), (VkDeviceSize)first * sizeof( unsigned int ) );
		first += count;
	}
	gpu.Wait();
	gpu.finalUploaded = true;
	return true;
}
