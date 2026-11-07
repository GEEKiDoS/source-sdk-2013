//========= Copyright Valve Corporation, All rights reserved. ============//
#include "restir_vulkan_internal.h"
#include "cmdlib.h"
#include <float.h>
#include <limits.h>


static const char *ReSTIRStageName( ReSTIRPipeline pipeline )
{
	return pipeline == RESTIR_PIPE_INIT ? "init" :
		pipeline == RESTIR_PIPE_CANDIDATES ? "candidates" :
		pipeline == RESTIR_PIPE_TEMPORAL ? "temporal" :
		pipeline == RESTIR_PIPE_SPATIAL ? "spatial" :
		pipeline == RESTIR_PIPE_ACCUMULATE ? "accumulate" :
		pipeline == RESTIR_PIPE_RECONSTRUCT ? "reconstruct" :
		pipeline == RESTIR_PIPE_SUN_VISIBILITY ? "sun-visibility" : "gpu";
}

static double SubmitDispatchChunks( CReSTIRVulkanDevice::Impl &gpu, ReSTIRPipeline pipeline,
	unsigned int count, unsigned int pass, double *pMilliseconds = NULL )
{
	if ( !count )
	{
		if ( pMilliseconds )
			*pMilliseconds = 0;
		return 0;
	}
	unsigned int first = 0;
	unsigned int start = 0, end = 0;
	const unsigned int itemLimit = gpu.DispatchItemLimit();
	unsigned int chunkItems = MIN( RESTIR_SUBMIT_INITIAL_ITEMS, itemLimit );
	while ( first < count )
	{
		const unsigned int chunk = MIN( chunkItems, count - first );
		VkCommandBuffer command = gpu.BeginCommands();
		if ( pMilliseconds && first == 0 )
			start = gpu.Timestamp( command );
		gpu.DispatchRange( command, pipeline, first, chunk, pass );
		gpu.Barrier( command );
		if ( pMilliseconds && first + chunk == count )
			end = gpu.Timestamp( command );
		const double submitStart = Plat_FloatTime();
		gpu.Submit( command );
		gpu.Wait();
		const double submitSeconds = Plat_FloatTime() - submitStart;
		if ( submitSeconds > 0.0 )
		{
			const double desired = chunkItems * RESTIR_SUBMIT_TARGET_SECONDS / submitSeconds;
			const unsigned int next = (unsigned int)MAX( (double)RESTIR_SUBMIT_MIN_ITEMS,
				MIN( (double)itemLimit, MIN( (double)chunkItems * 2.0, desired ) ) );
			chunkItems = MAX( RESTIR_SUBMIT_MIN_ITEMS, next );
		}
		ReSTIRGpuStageStats &stats = gpu.stageStats[pipeline];
		if ( !stats.submits )
			stats.minChunk = stats.maxChunk = chunk;
		else
		{
			stats.minChunk = MIN( stats.minChunk, chunk );
			stats.maxChunk = MAX( stats.maxChunk, chunk );
		}
		stats.finalChunk = chunk;
		++stats.submits;
		first += chunk;
	}
	if ( !pMilliseconds )
		return 0;
	*pMilliseconds = gpu.TimestampMs( start, end );
	return *pMilliseconds;
}

static void PrintStageSummaries( const CReSTIRVulkanDevice::Impl &gpu )
{
	for ( int pipeline = 0; pipeline < RESTIR_PIPE_COUNT; ++pipeline )
	{
		const ReSTIRGpuStageStats &stats = gpu.stageStats[pipeline];
		if ( !stats.submits )
			continue;
		Msg( "VRAD ReSTIR: GPU stage %s submits=%llu chunk-range=%u..%u final-chunk=%u\n",
			ReSTIRStageName( (ReSTIRPipeline)pipeline ), (unsigned long long)stats.submits,
			stats.minChunk, stats.maxChunk, stats.finalChunk );
	}
}


void CReSTIRVulkanDevice::PrintBakeStageSummaries() const
{
	PrintStageSummaries( *m_pImpl );
}
void CReSTIRVulkanDevice::PrintBakeSummary() const
{
	Msg( "VRAD ReSTIR: GPU bake elapsed %.3f s, submits=%llu\n",
		m_pImpl->lastBakeSeconds, (unsigned long long)m_pImpl->lastBakeSubmits );
}

void CReSTIRVulkanDevice::Impl::WriteReservoirDescriptors()
{
	VkDescriptorBufferInfo info[3] = {};
	VkWriteDescriptorSet writes[3] = {};
	for ( unsigned int i = 0; i < 3; ++i )
	{
		info[i].buffer = buffers[reservoirBindings[i]].handle;
		info[i].range = buffers[reservoirBindings[i]].size;
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = descriptorSet;
		writes[i].dstBinding = RESTIR_BIND_RESERVOIRS_PREV + i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &info[i];
	}
	vkUpdateDescriptorSets( device, 3, writes, 0, NULL );
}

static void DownloadRadiance( CReSTIRVulkanDevice::Impl &gpu, int binding, CUtlVector<Vector> &radiance )
{
	radiance.SetCount( gpu.scene->numOutputValues );
	CUtlVector<float> values;
	const unsigned int capacity = (unsigned int)( RESTIR_STAGING_BYTES / ( sizeof( float ) * 4 ) );
	for ( unsigned int first = 0; first < (unsigned int)radiance.Count(); )
	{
		unsigned int count = MIN( capacity, (unsigned int)radiance.Count() - first );
		values.SetCount( count * 4 );
		gpu.Download( binding, values.Base(), (VkDeviceSize)count * sizeof( float ) * 4,
			(VkDeviceSize)first * sizeof( float ) * 4, &gpu.timings.compactionMs );
		for ( unsigned int i = 0; i < count; ++i )
		{
			for ( int channel = 0; channel < 3; ++channel )
			{
				float value = values[i * 4 + channel];
				radiance[first + i][channel] = _finite( value ) && value >= 0 ? value : 0;
			}
		}
		first += count;
	}
}

static bool DownloadSunVisibility( CReSTIRVulkanDevice::Impl &gpu, CUtlVector<float> &visibility )
{
	visibility.SetCount( gpu.scene->luxels.Count() );
	const unsigned int capacity = (unsigned int)( RESTIR_STAGING_BYTES / sizeof( float ) );
	for ( unsigned int first = 0; first < (unsigned int)visibility.Count(); )
	{
		const unsigned int count = MIN( capacity, (unsigned int)visibility.Count() - first );
		gpu.Download( RESTIR_BIND_SUN_VISIBILITY, visibility.Base() + first, (VkDeviceSize)count * sizeof( float ),
			(VkDeviceSize)first * sizeof( float ), &gpu.timings.compactionMs );
		for ( unsigned int i = 0; i < count; ++i )
		{
			const float value = visibility[first + i];
			if ( !_finite( value ) || value < 0.0f || value > 1.0f )
			{
				visibility.RemoveAll();
				gpu.Fail( "sun visibility readback contains a nonfinite or out-of-range scalar" );
				return false;
			}
		}
		first += count;
	}
	return true;
}

static void BakeLocalVisibility( CReSTIRVulkanDevice::Impl &gpu, CUtlVector<unsigned char> &visibility )
{
	int localCount = 0;
	for ( int light = 0; light < gpu.scene->shadowLights.Count(); ++light )
		if ( gpu.scene->shadowLights[light].light.type != emit_skylight )
			++localCount;
	if ( !gpu.options.shadowMaps || !localCount )
		return;
	const uint64_t valueCount = (uint64_t)gpu.scene->shadowLights.Count() * gpu.scene->luxels.Count();
	if ( valueCount > INT_MAX )
		gpu.Fail( "canonical local visibility result exceeds signed 32-bit indexing" );
	visibility.SetCount( (int)valueCount );
	const double visibilityStart = Plat_FloatTime();
	uint64_t raysPerLuxel = 0;
	const unsigned int capacity = (unsigned int)( MIN( gpu.buffers[RESTIR_BIND_SERVICE_OUT].size,
		RESTIR_STAGING_BYTES ) / sizeof( float ) );
	if ( !capacity )
		gpu.Fail( "local visibility result exceeds service ring capacity" );
	CUtlVector<float> page; page.SetCount( MIN( capacity, (unsigned int)gpu.scene->luxels.Count() ) );
	for ( int light = 0; light < gpu.scene->shadowLights.Count(); ++light )
	{
		unsigned char *plane = visibility.Base() ? visibility.Base() + (size_t)light * gpu.scene->luxels.Count() : NULL;
		if ( gpu.scene->shadowLights[light].light.type == emit_skylight )
		{
			// Preserve the full canonical selected domain: sun is not a local entry.
			if ( gpu.scene->luxels.Count() )
				memset( plane, 0, (size_t)gpu.scene->luxels.Count() );
			continue;
		}
		raysPerLuxel += gpu.scene->shadowLights[light].shadowSourceRadius == 0 ? 1 : 32;
		unsigned int chunkItems = MIN( RESTIR_SUBMIT_INITIAL_ITEMS, gpu.DispatchItemLimit() );
		for ( unsigned int first = 0; first < (unsigned int)gpu.scene->luxels.Count(); )
		{
			const unsigned int count = MIN( MIN( capacity, chunkItems ),
				(unsigned int)gpu.scene->luxels.Count() - first );
			// Origins stay resident; only one scalar per query occupies the GPU ring.
			// Dispatch batching owns push.first; iteration is the outer luxel base.
			gpu.push.iteration = first;
			VkCommandBuffer command = gpu.BeginCommands();
			gpu.Dispatch( command, RESTIR_PIPE_LOCAL_VISIBILITY, count, (unsigned int)light );
			gpu.Barrier( command );
			const double submitStart = Plat_FloatTime();
			gpu.Submit( command );
			gpu.Download( RESTIR_BIND_SERVICE_OUT, page.Base(), (VkDeviceSize)count * sizeof( float ),
				0, &gpu.timings.compactionMs );
			const double submitSeconds = Plat_FloatTime() - submitStart;
			if ( submitSeconds > 0.0 )
			{
				const double desired = chunkItems * RESTIR_SUBMIT_TARGET_SECONDS / submitSeconds;
				chunkItems = (unsigned int)MAX( (double)RESTIR_SUBMIT_MIN_ITEMS,
					MIN( (double)gpu.DispatchItemLimit(), MIN( (double)chunkItems * 2.0, desired ) ) );
			}
			for ( unsigned int i = 0; i < count; ++i )
			{
				if ( !_finite( page[i] ) || page[i] < 0.0f || page[i] > 1.0f )
					gpu.Fail( "world local visibility readback contains a nonfinite or out-of-range scalar" );
				plane[first + i] = (unsigned char)floorf( page[i] * 255.0f + 0.5f );
			}
			first += count;
		}
	}
	if ( verbose )
		Msg( "Hybrid visibility: world queries=%llu rays<=%llu R8-bytes=%d elapsed=%.3f s\n",
			(unsigned long long)localCount * gpu.scene->luxels.Count(),
			(unsigned long long)raysPerLuxel * gpu.scene->luxels.Count(), visibility.Count(), Plat_FloatTime() - visibilityStart );
}

bool CReSTIRVulkanDevice::BakeLightmaps( const ReSTIROptions &options, ReSTIRLightmapResult &result )
{
	Impl &gpu = *m_pImpl;
	result.sunVisibility.RemoveAll();
	result.localVisibility.RemoveAll();
	if ( !gpu.scene || !gpu.pipelineLayout )
		gpu.Fail( "BakeLightmaps requires UploadScene" );
	gpu.finalUploaded = false;
	const bool shadowSplit = gpu.options.shadowMaps && gpu.scene->shadowLights.Count() != 0;
	const bool selectedSun = gpu.HasSelectedSun();
	result.sourceRadiance.RemoveAll();
	gpu.push.seed = options.seed;
	gpu.push.spatialRadius = options.spatialRadius;
	gpu.push.maxBounces = options.maxBounces;
	gpu.push.totalIterations = options.iterations;
	const uint64_t submitsAtStart = gpu.submittedValue;
	const double bakeStart = Plat_FloatTime();
	gpu.timings.candidateMs = gpu.timings.reuseMs = gpu.timings.reconstructionMs = gpu.timings.compactionMs = 0;
	memset( gpu.stageStats, 0, sizeof( gpu.stageStats ) );
	const unsigned int progressTotal = (unsigned int)options.iterations + 2;
	unsigned int progressDone = 0;
	gpu.ResetQueries();
	VkCommandBuffer command = gpu.BeginCommands();
	// Reset even the ordinary-bake dummy so reused devices cannot retain a previous scalar result.
	vkCmdFillBuffer( command, gpu.buffers[RESTIR_BIND_SUN_VISIBILITY].handle, 0, VK_WHOLE_SIZE, 0 );
	if ( shadowSplit )
	{
		for ( int binding = RESTIR_BIND_RECEIVER_ACCUMULATION; binding <= RESTIR_BIND_RECEIVER_OUTPUT; ++binding )
			vkCmdFillBuffer( command, gpu.buffers[binding].handle, 0, VK_WHOLE_SIZE, 0 );
	}
	VkMemoryBarrier clearBarrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	clearBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	clearBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		0, 1, &clearBarrier, 0, NULL, 0, NULL );
	gpu.Submit( command );
	gpu.Wait();
	SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, gpu.push.numReservoirs, 0 );
	SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, gpu.push.numLuxels, 1 );
	SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, gpu.scene->numOutputValues, 2 );
	// Independent static-world scalar: one selected-sun pass, never weighted by transport iterations.
	if ( selectedSun )
		SubmitDispatchChunks( gpu, RESTIR_PIPE_SUN_VISIBILITY, gpu.push.numLuxels, 0 );
	// Exactly one local scalar query per canonical local/geometric luxel, outside
	// transport iterations and independent of selected sun/intensity/style.
	BakeLocalVisibility( gpu, result.localVisibility );
	gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	for ( int iteration = 0; iteration < options.iterations; ++iteration )
	{
		gpu.push.iteration = iteration;
		gpu.push.flags = ( gpu.hardware ? RESTIR_PC_HARDWARE_RT : 0 ) | ( iteration + 1 == options.iterations ? RESTIR_PC_FINAL_ITERATION : 0 );
		double stageMs;
		// pc.pass carries the dfaceToFace offset inside RESTIR_BIND_FACE_NEIGHBORS (restir_lightmap.glsl SceneFaceForHit).
		SubmitDispatchChunks( gpu, RESTIR_PIPE_CANDIDATES, gpu.push.numReservoirs,
			gpu.scene->faceNeighbors.Count(), &stageMs );
		gpu.timings.candidateMs += stageMs;
		if ( iteration )
		{
			SubmitDispatchChunks( gpu, RESTIR_PIPE_TEMPORAL, gpu.push.numReservoirs, 0, &stageMs );
			gpu.timings.reuseMs += stageMs;
		}
		SubmitDispatchChunks( gpu, RESTIR_PIPE_SPATIAL, gpu.push.numReservoirs, 0, &stageMs );
		gpu.timings.reuseMs += stageMs;
		SubmitDispatchChunks( gpu, RESTIR_PIPE_ACCUMULATE, gpu.push.numReservoirs, 0 );
		if ( iteration + 1 < options.iterations )
		{
			unsigned int previous = gpu.reservoirBindings[0];
			gpu.reservoirBindings[0] = gpu.reservoirBindings[2];
			gpu.reservoirBindings[2] = previous;
			gpu.WriteReservoirDescriptors();
			gpu.ResetQueries();
		}
		gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	}
	gpu.ResetQueries();
	SubmitDispatchChunks( gpu, RESTIR_PIPE_RECONSTRUCT, gpu.scene->numOutputValues, 0,
		&gpu.timings.reconstructionMs );
	// The fixed pass table compacts directly during reconstruction; this counter
	// measures the GPU transfer of its compact radiance and validity arrays.
	// Output boundary only: no clipping or receiver subtraction in full transport.
	DownloadRadiance( gpu, shadowSplit ? RESTIR_BIND_RECEIVER_OUTPUT : RESTIR_BIND_OUTPUT, result.radiance );
	if ( shadowSplit )
		DownloadRadiance( gpu, RESTIR_BIND_OUTPUT, result.sourceRadiance );
	if ( selectedSun && !DownloadSunVisibility( gpu, result.sunVisibility ) )
	{
		return false;
	}
	result.luxelValid.SetCount( gpu.scene->luxels.Count() );
	CUtlVector<unsigned int> valid;
	unsigned int capacity = (unsigned int)( RESTIR_STAGING_BYTES / sizeof( unsigned int ) );
	for ( unsigned int first = 0; first < (unsigned int)result.luxelValid.Count(); )
	{
		unsigned int count = MIN( capacity, (unsigned int)result.luxelValid.Count() - first );
		valid.SetCount( count );
		gpu.Download( RESTIR_BIND_LUXEL_VALID, valid.Base(), (VkDeviceSize)count * sizeof( unsigned int ), (VkDeviceSize)first * sizeof( unsigned int ), &gpu.timings.compactionMs );
		for ( unsigned int i = 0; i < count; ++i )
			result.luxelValid[first + i] = (unsigned char)MIN( valid[i], 255u );
		first += count;
	}
	gpu.Wait();
	gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	gpu.lastBakeSeconds = Plat_FloatTime() - bakeStart;
	gpu.lastBakeSubmits = gpu.submittedValue - submitsAtStart;
	return true;
}
