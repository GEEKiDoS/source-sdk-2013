//========= Copyright Valve Corporation, All rights reserved. ============//
#include "restir_vulkan_internal.h"
#include "cmdlib.h"
#include <float.h>
#include <limits.h>


static const char *ReSTIRStageName( ReSTIRPipeline pipeline )
{
	return pipeline == RESTIR_PIPE_INIT ? "init" :
		pipeline == RESTIR_PIPE_DIRECT ? "direct" :
		pipeline == RESTIR_PIPE_TRANSPORT ? "transport" :
		pipeline == RESTIR_PIPE_RECONSTRUCT ? "reconstruct" :
		pipeline == RESTIR_PIPE_SUN_VISIBILITY ? "sun-visibility" : "gpu";
}


static double SubmitDispatchChunks( CReSTIRVulkanDevice::Impl &gpu, ReSTIRPipeline pipeline,
	unsigned int count, unsigned int pass, double *pMilliseconds = NULL, unsigned int firstBase = 0 )
{
	if ( !count )
	{
		if ( pMilliseconds )
			*pMilliseconds = 0;
		return 0;
	}
	unsigned int first = firstBase;
	unsigned int start = 0, end = 0;
	double verboseGpuMilliseconds = 0.0;
	double slowestItemSeconds = 0.0;
	const unsigned int itemLimit = gpu.DispatchItemLimit();
	const unsigned int safetyLimit = pipeline == RESTIR_PIPE_DIRECT ? RESTIR_DIRECT_SAFE_ITEMS :
		pipeline == RESTIR_PIPE_TRANSPORT ? RESTIR_TRANSPORT_SAFE_ITEMS : RESTIR_SUBMIT_MAX_ITEMS;
	unsigned int chunkItems = MIN( MIN( RESTIR_SUBMIT_INITIAL_ITEMS, safetyLimit ), itemLimit );
	// The start/end pair spans every chunk of this call; it owns the pool from here until it is read below. Per-face
	// reconstruction issues thousands of calls, and nothing else resets the 64-entry pool between them.
	if ( pMilliseconds && !verbose )
		gpu.ResetQueries();
	while ( first < firstBase + count )
	{
		const unsigned int chunk = MIN( chunkItems, firstBase + count - first );
		unsigned int submitStartQuery = 0, submitEndQuery = 0;
		if ( verbose )
			gpu.ResetQueries();
		VkCommandBuffer command = gpu.BeginCommands();
		if ( verbose )
			submitStartQuery = gpu.Timestamp( command );
		else if ( pMilliseconds && first == firstBase )
			start = gpu.Timestamp( command );
		gpu.DispatchRange( command, pipeline, first, chunk, pass );
		gpu.Barrier( command );
		if ( verbose )
			submitEndQuery = gpu.Timestamp( command );
		else if ( pMilliseconds && first + chunk == firstBase + count )
			end = gpu.Timestamp( command );
		const double submitStart = Plat_FloatTime();
		gpu.Submit( command );
		gpu.Wait();
		const double submitSeconds = Plat_FloatTime() - submitStart;
		if ( verbose )
		{
			const double gpuMilliseconds = gpu.TimestampMs( submitStartQuery, submitEndQuery );
			verboseGpuMilliseconds += gpuMilliseconds;
			Msg( "VRAD ReSTIR: submit stage=%s first=%u items=%u gpu=%.3f ms queue=%.3f ms\n",
				ReSTIRStageName( pipeline ), first, chunk, gpuMilliseconds, submitSeconds * 1000.0 );
		}
		if ( submitSeconds > 0.0 )
		{
			slowestItemSeconds = MAX( slowestItemSeconds, submitSeconds / (double)chunk );
			const double desired = slowestItemSeconds > 0.0 ?
				0.5 / slowestItemSeconds : (double)chunkItems * 2.0;
			const double growth = submitSeconds < RESTIR_SUBMIT_TARGET_SECONDS * 0.25 ?
				(double)chunkItems * 2.0 : (double)chunkItems;
			unsigned int next;
			if ( submitSeconds >= RESTIR_SUBMIT_TARGET_SECONDS )
				next = (unsigned int)MAX( (double)RESTIR_SUBMIT_MIN_ITEMS, (double)chunkItems * 0.25 );
			else
				next = (unsigned int)MIN( (double)safetyLimit,
					MIN( (double)itemLimit, MIN( growth, desired ) ) );
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
	*pMilliseconds = verbose ? verboseGpuMilliseconds : gpu.TimestampMs( start, end );
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

static void SetPagePush( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page, unsigned int flags )
{
	gpu.push.flags = flags;
	gpu.push.pageBase[0] = page.firstReservoir;
	gpu.push.pageBase[1] = page.firstSample;
	gpu.push.pageBase[2] = page.firstLuxel;
	gpu.push.pageBase[3] = page.firstOutput;
}

// Paged bakes stream a page's running sums through the host; receiverAccumulation is empty unless split.
static void UploadPageAccumulation( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page,
	const CUtlVector<float> &accumulation, const CUtlVector<float> &receiverAccumulation )
{
	const size_t first = (size_t)page.firstReservoir * RESTIR_MAX_CHANNELS * 4;
	const VkDeviceSize bytes = (VkDeviceSize)page.reservoirCount * RESTIR_MAX_CHANNELS * sizeof( float ) * 4;
	gpu.Upload( RESTIR_BIND_ACCUMULATION, accumulation.Base() + first, bytes );
	if ( receiverAccumulation.Count() )
		gpu.Upload( RESTIR_BIND_RECEIVER_ACCUMULATION, receiverAccumulation.Base() + first, bytes );
}

static void DownloadPageAccumulation( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page,
	CUtlVector<float> &accumulation, CUtlVector<float> &receiverAccumulation )
{
	const size_t first = (size_t)page.firstReservoir * RESTIR_MAX_CHANNELS * 4;
	const VkDeviceSize bytes = (VkDeviceSize)page.reservoirCount * RESTIR_MAX_CHANNELS * sizeof( float ) * 4;
	gpu.Download( RESTIR_BIND_ACCUMULATION, accumulation.Base() + first, bytes );
	if ( receiverAccumulation.Count() )
		gpu.Download( RESTIR_BIND_RECEIVER_ACCUMULATION, receiverAccumulation.Base() + first, bytes );
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

static void UploadTransportPage( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page );
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
	CUtlVector<float> pageOutput;
	for ( int light = 0; light < gpu.scene->shadowLights.Count(); ++light )
	{
		if ( gpu.scene->shadowLights[light].light.type == emit_skylight )
			continue;
		for ( int p = 0; p < gpu.pages.Count(); ++p )
		{
			const ReSTIRPage &page = gpu.pages[p];
			if ( verbose )
				Msg( "VRAD ReSTIR: visibility page=%d/%d light=%d luxels=%u\n",
					p + 1, gpu.pages.Count(), light, page.luxelCount );
			if ( gpu.pages.Count() > 1 )
				UploadTransportPage( gpu, page );
			pageOutput.SetCount( page.luxelCount );
			gpu.push.flags = ( gpu.hardware ? RESTIR_PC_HARDWARE_RT : 0 ) | RESTIR_PC_RESERVOIR_PAGE;
			gpu.push.pageBase[0] = page.firstReservoir;
			gpu.push.pageBase[1] = page.firstSample;
			gpu.push.pageBase[2] = page.firstLuxel;
			gpu.push.pageBase[3] = page.firstOutput;
			SubmitDispatchChunks( gpu, RESTIR_PIPE_LOCAL_VISIBILITY, page.luxelCount, (unsigned int)light,
				NULL, page.firstLuxel );
			gpu.Download( RESTIR_BIND_SERVICE_OUT, pageOutput.Base(), (VkDeviceSize)page.luxelCount * sizeof( float ),
				0, &gpu.timings.compactionMs );
			for ( unsigned int i = 0; i < page.luxelCount; ++i )
			{
				if ( !_finite( pageOutput[i] ) || pageOutput[i] < 0.0f || pageOutput[i] > 1.0f )
					gpu.Fail( "world local visibility readback contains a nonfinite or out-of-range scalar" );
				visibility[light * gpu.scene->luxels.Count() + page.firstLuxel + i] =
					(unsigned char)floorf( pageOutput[i] * 255.0f + 0.5f );
			}
		}
	}
	if ( verbose )
		Msg( "Hybrid visibility: world queries=%llu elapsed=%.3f s\n",
			(unsigned long long)localCount * gpu.scene->luxels.Count(), Plat_FloatTime() - visibilityStart );
}

static void UploadTransportPage( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page )
{
	const ReSTIRScene &scene = *gpu.scene;
	gpu.Upload( RESTIR_BIND_SAMPLES, scene.samples.Base() + page.firstSample,
		(VkDeviceSize)page.sampleCount * sizeof( ReSTIRGpuSample ) );
	gpu.Upload( RESTIR_BIND_LUXELS, scene.luxels.Base() + page.firstLuxel,
		(VkDeviceSize)page.luxelCount * sizeof( ReSTIRGpuLuxel ) );
	if ( scene.shadowLights.Count() )
		gpu.Upload( RESTIR_BIND_SUN_ORIGINS, scene.sunVisibilityOrigins.Base() + page.firstLuxel,
			(VkDeviceSize)page.luxelCount * sizeof( Vector4D ) );
}

static void CopyRadiancePage( CReSTIRVulkanDevice::Impl &gpu, int binding, const ReSTIRPage &page,
	CUtlVector<Vector> &result )
{
	CUtlVector<float> values;
	values.SetCount( page.outputCount * 4 );
	gpu.Download( binding, values.Base(), (VkDeviceSize)values.Count() * sizeof( float ) );
	for ( unsigned int i = 0; i < page.outputCount; ++i )
		result[page.firstOutput + i] = Vector( values[i * 4], values[i * 4 + 1], values[i * 4 + 2] );
}

static unsigned int UploadReconstructPage( CReSTIRVulkanDevice::Impl &gpu, const ReSTIRPage &page,
	const CUtlVector<float> &accumulation, const CUtlVector<float> &receiverAccumulation )
{
	CUtlVector<unsigned char> used;
	used.SetCount( gpu.scene->faces.Count() );
	memset( used.Base(), 0, used.Count() );
	for ( unsigned int f = 0; f < page.faceCount; ++f )
	{
		int faceIndex = page.firstFace + f;
		used[faceIndex] = 1;
		const ReSTIRGpuFace &face = gpu.scene->faces[faceIndex];
		for ( int n = 0; n < face.numNeighbors; ++n )
		{
			int neighbor = gpu.scene->faceNeighbors[face.firstNeighbor + n];
			if ( neighbor >= 0 && neighbor < used.Count() )
				used[neighbor] = 1;
		}
	}
	CUtlVector<ReSTIRGpuFace> faces;
	faces.CopyArray( gpu.uploadedFaces.Base(), gpu.uploadedFaces.Count() );
	CUtlVector<ReSTIRGpuSample> samples;
	CUtlVector<ReSTIRGpuLuxel> luxels;
	CUtlVector<float> pageAccum, pageReceiver;
	int localSample = 0, localLuxel = 0, localReservoir = 0;
	for ( int f = 0; f < faces.Count(); ++f )
	{
		if ( !used[f] )
			continue;
		const ReSTIRGpuFace &source = gpu.uploadedFaces[f];
		ReSTIRGpuFace &patched = faces[f];
		patched.firstSample = localSample;
		patched.firstLuxel = localLuxel;
		patched.firstReservoir = localReservoir;
		const int luxelCount = source.luxelW * source.luxelH;
		for ( int s = 0; s < source.numSamples; ++s )
			samples.AddToTail( gpu.scene->samples[source.firstSample + s] );
		for ( int l = 0; l < luxelCount; ++l )
			luxels.AddToTail( gpu.scene->luxels[source.firstLuxel + l] );
		const int reservoirCount = source.numSamples * source.numStyles;
		pageAccum.AddMultipleToTail( reservoirCount * RESTIR_MAX_CHANNELS * 4,
			accumulation.Base() + (size_t)source.firstReservoir * RESTIR_MAX_CHANNELS * 4 );
		if ( receiverAccumulation.Count() )
			pageReceiver.AddMultipleToTail( reservoirCount * RESTIR_MAX_CHANNELS * 4,
				receiverAccumulation.Base() + (size_t)source.firstReservoir * RESTIR_MAX_CHANNELS * 4 );
		localSample += source.numSamples;
		localLuxel += luxelCount;
		localReservoir += reservoirCount;
	}
	if ( samples.Count() > gpu.pageSamples || luxels.Count() > gpu.pageLuxels || localReservoir > gpu.pageReservoirs )
		gpu.Fail( "reconstruction face-neighbor closure exceeds the allocated page" );
	gpu.Upload( RESTIR_BIND_FACES, faces.Base(), (VkDeviceSize)faces.Count() * sizeof( ReSTIRGpuFace ) );
	gpu.Upload( RESTIR_BIND_SAMPLES, samples.Base(), (VkDeviceSize)samples.Count() * sizeof( ReSTIRGpuSample ) );
	gpu.Upload( RESTIR_BIND_LUXELS, luxels.Base(), (VkDeviceSize)luxels.Count() * sizeof( ReSTIRGpuLuxel ) );
	gpu.Upload( RESTIR_BIND_ACCUMULATION, pageAccum.Base(), (VkDeviceSize)pageAccum.Count() * sizeof( float ) );
	if ( receiverAccumulation.Count() )
		gpu.Upload( RESTIR_BIND_RECEIVER_ACCUMULATION, pageReceiver.Base(),
			(VkDeviceSize)pageReceiver.Count() * sizeof( float ) );
	return (unsigned int)faces[page.firstFace].firstLuxel;
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
	result.radiance.SetCount( gpu.scene->numOutputValues );
	if ( shadowSplit )
		result.sourceRadiance.SetCount( gpu.scene->numOutputValues );
	result.luxelValid.SetCount( gpu.scene->luxels.Count() );
	memset( result.radiance.Base(), 0, result.radiance.Count() * sizeof( Vector ) );
	if ( result.sourceRadiance.Count() )
		memset( result.sourceRadiance.Base(), 0, result.sourceRadiance.Count() * sizeof( Vector ) );
	memset( result.luxelValid.Base(), 0, result.luxelValid.Count() );
	gpu.push.seed = options.seed;
	gpu.push.candidates = options.candidates;
	gpu.push.maxBounces = options.maxBounces;
	gpu.push.totalIterations = options.iterations;
	const uint64_t submitsAtStart = gpu.submittedValue;
	const double bakeStart = Plat_FloatTime();
	gpu.timings.directMs = gpu.timings.transportMs = gpu.timings.reconstructionMs = gpu.timings.compactionMs = 0;
	memset( gpu.stageStats, 0, sizeof( gpu.stageStats ) );
	// One page already holds every transport buffer, so its iterations run on resident GPU state.
	// Only a paged bake keeps host copies and streams them through the staging ring.
	const bool resident = gpu.pages.Count() == 1;
	const unsigned int initFlags = ( gpu.hardware ? RESTIR_PC_HARDWARE_RT : 0 ) | RESTIR_PC_RESERVOIR_PAGE;
	const unsigned int transportFlags = initFlags | ( shadowSplit ? RESTIR_PC_RECEIVER_PAGE : 0 );
	CUtlVector<float> accumulationHost, receiverAccumHost;
	if ( !resident )
	{
		accumulationHost.SetCount( (int)gpu.fullReservoirs * RESTIR_MAX_CHANNELS * 4 );
		memset( accumulationHost.Base(), 0, accumulationHost.Count() * sizeof( float ) );
		if ( shadowSplit )
		{
			receiverAccumHost.SetCount( accumulationHost.Count() );
			memset( receiverAccumHost.Base(), 0, receiverAccumHost.Count() * sizeof( float ) );
		}
	}
	unsigned int progressDone = 0;
	const unsigned int progressTotal = (unsigned int)options.iterations + 2;
	if ( selectedSun )
		result.sunVisibility.SetCount( gpu.scene->luxels.Count() );
	double stageMs = 0;
	for ( int p = 0; p < gpu.pages.Count(); ++p )
	{
		const ReSTIRPage &page = gpu.pages[p];
		if ( verbose )
			Msg( "VRAD ReSTIR: transport page=%d/%d faces=%u samples=%u luxels=%u reservoirs=%u outputs=%u\n",
				p + 1, gpu.pages.Count(), page.faceCount, page.sampleCount, page.luxelCount,
				page.reservoirCount, page.outputCount );
		UploadTransportPage( gpu, page );
		SetPagePush( gpu, page, initFlags );
		SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, page.reservoirCount, 0, NULL, page.firstReservoir );
		SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, page.luxelCount, 1, NULL, page.firstLuxel );
		SubmitDispatchChunks( gpu, RESTIR_PIPE_INIT, page.outputCount, 2, NULL, page.firstOutput );
		if ( selectedSun )
		{
			SubmitDispatchChunks( gpu, RESTIR_PIPE_SUN_VISIBILITY, page.luxelCount, 0, NULL, page.firstLuxel );
			gpu.Download( RESTIR_BIND_SUN_VISIBILITY, result.sunVisibility.Base() + page.firstLuxel,
				(VkDeviceSize)page.luxelCount * sizeof( float ), 0, &gpu.timings.compactionMs );
		}
		// Exact point/spot direct light seeds the bounce feedback before the first iteration.
		SubmitDispatchChunks( gpu, RESTIR_PIPE_DIRECT, page.reservoirCount, 0, &stageMs, page.firstReservoir );
		gpu.timings.directMs += stageMs;
	}
	BakeLocalVisibility( gpu, result.localVisibility );
	gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	for ( int iteration = 0; iteration < options.iterations; ++iteration )
	{
		gpu.push.iteration = iteration;
		for ( int p = 0; p < gpu.pages.Count(); ++p )
		{
			const ReSTIRPage &page = gpu.pages[p];
			if ( verbose )
				Msg( "VRAD ReSTIR: transport page=%d/%d faces=%u samples=%u luxels=%u reservoirs=%u outputs=%u iteration=%d\n",
					p + 1, gpu.pages.Count(), page.faceCount, page.sampleCount, page.luxelCount,
					page.reservoirCount, page.outputCount, iteration );
			SetPagePush( gpu, page, transportFlags );
			if ( !resident )
			{
				UploadTransportPage( gpu, page );
				UploadPageAccumulation( gpu, page, accumulationHost, receiverAccumHost );
			}
			SubmitDispatchChunks( gpu, RESTIR_PIPE_TRANSPORT, page.reservoirCount, gpu.scene->faceNeighbors.Count(), &stageMs, page.firstReservoir );
			gpu.timings.transportMs += stageMs;
			if ( !resident )
				DownloadPageAccumulation( gpu, page, accumulationHost, receiverAccumHost );
		}
		// Publish the bounce feedback only after the whole iteration: every page, and every sample
		// within a page, must read the same previous-iteration state.
		if ( resident )
		{
			SetPagePush( gpu, gpu.pages[0], transportFlags | RESTIR_PC_PUBLISH_IRRADIANCE );
			SubmitDispatchChunks( gpu, RESTIR_PIPE_TRANSPORT, gpu.pages[0].reservoirCount, 0, NULL, gpu.pages[0].firstReservoir );
		}
		else
		{
			CUtlVector<float> previousIrradiance;
			previousIrradiance.SetCount( gpu.scene->samples.Count() * 4 );
			memset( previousIrradiance.Base(), 0, previousIrradiance.Count() * sizeof( float ) );
			FOR_EACH_VEC( gpu.uploadedFaces, f )
			{
				const ReSTIRGpuFace &face = gpu.uploadedFaces[f];
				for ( int s = 0; s < face.numSamples; ++s )
				{
					// The same copy restir_transport.comp publishes on a resident page.
					memcpy( previousIrradiance.Base() + (size_t)( face.firstSample + s ) * 4,
						accumulationHost.Base() + (size_t)( face.firstReservoir + s * face.numStyles ) * RESTIR_MAX_CHANNELS * 4, sizeof( float ) * 4 );
				}
			}
			gpu.Upload( RESTIR_BIND_PREVIOUS_IRRADIANCE, previousIrradiance.Base(),
				(VkDeviceSize)previousIrradiance.Count() * sizeof( float ) );
		}
		gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	}
	// Add the exact direct light to the finished means.
	for ( int p = 0; p < gpu.pages.Count(); ++p )
	{
		const ReSTIRPage &page = gpu.pages[p];
		SetPagePush( gpu, page, transportFlags );
		if ( !resident )
		{
			UploadTransportPage( gpu, page );
			UploadPageAccumulation( gpu, page, accumulationHost, receiverAccumHost );
		}
		SubmitDispatchChunks( gpu, RESTIR_PIPE_DIRECT, page.reservoirCount, 1, &stageMs, page.firstReservoir );
		gpu.timings.directMs += stageMs;
		if ( !resident )
			DownloadPageAccumulation( gpu, page, accumulationHost, receiverAccumHost );
	}
	gpu.push.flags = transportFlags;
	if ( resident )
	{
		// Reconstruct straight from the resident page. Each face stays its own dispatch call so the
		// adaptive chunk sizer restarts small: the per-output cost grows with the face's closure.
		const ReSTIRPage &page = gpu.pages[0];
		gpu.push.pageBase[0] = page.firstReservoir;
		gpu.push.pageBase[1] = page.firstSample;
		gpu.push.pageBase[2] = page.firstLuxel;
		gpu.push.pageBase[3] = page.firstOutput;
		FOR_EACH_VEC( gpu.uploadedFaces, faceIndex )
		{
			const ReSTIRGpuFace &face = gpu.uploadedFaces[faceIndex];
			double faceMs = 0;
			SubmitDispatchChunks( gpu, RESTIR_PIPE_RECONSTRUCT,
				face.numStyles * face.numChannels * face.luxelW * face.luxelH, 0, &faceMs, face.firstOutput );
			gpu.timings.reconstructionMs += faceMs;
		}
		CopyRadiancePage( gpu, shadowSplit ? RESTIR_BIND_RECEIVER_OUTPUT : RESTIR_BIND_OUTPUT, page, result.radiance );
		if ( shadowSplit )
			CopyRadiancePage( gpu, RESTIR_BIND_OUTPUT, page, result.sourceRadiance );
		CUtlVector<unsigned int> valid;
		valid.SetCount( page.luxelCount );
		gpu.Download( RESTIR_BIND_LUXEL_VALID, valid.Base(), (VkDeviceSize)valid.Count() * sizeof( unsigned int ) );
		for ( unsigned int i = 0; i < page.luxelCount; ++i )
			result.luxelValid[page.firstLuxel + i] = (unsigned char)MIN( valid[i], 255u );
	}
	else
	{
		CUtlVector<ReSTIRPage> reconstructionPages;
		unsigned int reconstructSamples = 0, reconstructLuxels = 0, reconstructReservoirs = 0;
		for ( int faceIndex = 0; faceIndex < gpu.uploadedFaces.Count(); ++faceIndex )
		{
			ReSTIRPage page;
			const ReSTIRGpuFace &face = gpu.uploadedFaces[faceIndex];
			page.firstFace = faceIndex;
			page.faceCount = 1;
			page.firstSample = face.firstSample;
			page.sampleCount = face.numSamples;
			page.firstLuxel = face.firstLuxel;
			page.luxelCount = face.luxelW * face.luxelH;
			page.firstReservoir = face.firstReservoir;
			page.reservoirCount = face.numSamples * face.numStyles;
			page.firstOutput = face.firstOutput;
			page.outputCount = face.numStyles * face.numChannels * face.luxelW * face.luxelH;
			CUtlVector<unsigned char> used;
			used.SetCount( gpu.uploadedFaces.Count() );
			memset( used.Base(), 0, used.Count() );
			used[faceIndex] = 1;
			for ( int n = 0; n < face.numNeighbors; ++n )
			{
				int neighbor = gpu.scene->faceNeighbors[face.firstNeighbor + n];
				if ( neighbor >= 0 && neighbor < used.Count() )
					used[neighbor] = 1;
			}
			unsigned int closureSamples = 0, closureLuxels = 0, closureReservoirs = 0;
			for ( int f = 0; f < used.Count(); ++f )
				if ( used[f] )
				{
					closureSamples += gpu.uploadedFaces[f].numSamples;
					closureLuxels += gpu.uploadedFaces[f].luxelW * gpu.uploadedFaces[f].luxelH;
					closureReservoirs += gpu.uploadedFaces[f].numSamples * gpu.uploadedFaces[f].numStyles;
				}
			reconstructSamples = MAX( reconstructSamples, closureSamples );
			reconstructLuxels = MAX( reconstructLuxels, closureLuxels );
			reconstructReservoirs = MAX( reconstructReservoirs, closureReservoirs );
			reconstructionPages.AddToTail( page );
		}
		gpu.PrepareReconstructionPages( reconstructSamples, reconstructReservoirs, reconstructLuxels );
		for ( int p = 0; p < reconstructionPages.Count(); ++p )
		{
			const ReSTIRPage &page = reconstructionPages[p];
			const unsigned int localLuxel = UploadReconstructPage( gpu, page, accumulationHost, receiverAccumHost );
			gpu.push.pageBase[0] = 0;
			gpu.push.pageBase[1] = 0;
			gpu.push.pageBase[2] = 0;
			gpu.push.pageBase[3] = page.firstOutput;
			SubmitDispatchChunks( gpu, RESTIR_PIPE_RECONSTRUCT, page.outputCount, 0, &gpu.timings.reconstructionMs, page.firstOutput );
			CopyRadiancePage( gpu, shadowSplit ? RESTIR_BIND_RECEIVER_OUTPUT : RESTIR_BIND_OUTPUT, page, result.radiance );
			if ( shadowSplit )
				CopyRadiancePage( gpu, RESTIR_BIND_OUTPUT, page, result.sourceRadiance );
			CUtlVector<unsigned int> valid;
			valid.SetCount( page.luxelCount );
			gpu.Download( RESTIR_BIND_LUXEL_VALID, valid.Base(), (VkDeviceSize)valid.Count() * sizeof( unsigned int ),
				(VkDeviceSize)localLuxel * sizeof( unsigned int ) );
			for ( unsigned int i = 0; i < page.luxelCount; ++i )
				result.luxelValid[page.firstLuxel + i] = (unsigned char)MIN( valid[i], 255u );
		}
	}
	gpu.ReallocateForReconstruct();
	gpu.Upload( RESTIR_BIND_FACES, gpu.uploadedFaces.Base(),
		(VkDeviceSize)gpu.uploadedFaces.Count() * sizeof( ReSTIRGpuFace ) );
	gpu.Wait();
	gpu.ReportProgress( "bake", ++progressDone, progressTotal );
	gpu.lastBakeSeconds = Plat_FloatTime() - bakeStart;
	gpu.lastBakeSubmits = gpu.submittedValue - submitsAtStart;
	return true;
}
