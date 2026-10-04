//========= Copyright Valve Corporation, All rights reserved. ============//
#include "restir_vulkan_internal.h"
#include "mathlib/anorms.h"
#include <limits.h>

static VkAccelerationStructureGeometryKHR TriangleGeometry( VkDeviceAddress vertices, unsigned int triangles, bool opaque )
{
	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
	geometry.flags = opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0;
	geometry.geometry.triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
	geometry.geometry.triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
	geometry.geometry.triangles.vertexData.deviceAddress = vertices;
	geometry.geometry.triangles.vertexStride = sizeof( float ) * 3;
	geometry.geometry.triangles.maxVertex = triangles ? triangles * 3 - 1 : 0;
	geometry.geometry.triangles.indexType = VK_INDEX_TYPE_NONE_KHR;
	return geometry;
}

static VkAccelerationStructureBuildGeometryInfoKHR BuildInfo( VkAccelerationStructureTypeKHR type, const VkAccelerationStructureGeometryKHR *geometry )
{
	VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
	build.type = type;
	build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
	build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
	build.geometryCount = 1;
	build.pGeometries = geometry;
	return build;
}

void CReSTIRVulkanDevice::Impl::PlanAcceleration()
{
	VkDeviceSize scratchBytes = 0;
	const unsigned int classMasks[2] = { RESTIR_TRI_SHADOW, RESTIR_TRI_WORLDFACE };
	for ( int maskIndex = 0; maskIndex < 2; ++maskIndex )
	{
		for ( int nonopaque = 0; nonopaque < 2; ++nonopaque )
		{
			ReSTIRAcceleration acceleration;
			acceleration.mask = classMasks[maskIndex];
			acceleration.firstPrimitive = hardwarePrimitiveMap.Count();
			FOR_EACH_VEC( scene->triangles, i )
			{
				const ReSTIRGpuTriangle &triangle = scene->triangles[i];
				if ( !( triangle.flags & acceleration.mask ) || ( ( triangle.flags & RESTIR_TRI_NONOPAQUE ) != 0 ) != ( nonopaque != 0 ) )
					continue;
				hardwarePrimitiveMap.AddToTail( i );
				const float *vertices[3] = { triangle.v0, triangle.v1, triangle.v2 };
				for ( int v = 0; v < 3; ++v )
					for ( int axis = 0; axis < 3; ++axis )
						hardwareVertices.AddToTail( vertices[v][axis] );
				++acceleration.primitiveCount;
			}
			if ( !acceleration.primitiveCount )
				continue;
			if ( acceleration.firstPrimitive > 0xFFFFFFu )
				Fail( "hardware primitive map exceeds the 24-bit instanceCustomIndex range" );
			VkAccelerationStructureGeometryKHR geometry = TriangleGeometry( 0, acceleration.primitiveCount, !nonopaque );
			VkAccelerationStructureBuildGeometryInfoKHR build = BuildInfo( VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &geometry );
			getAccelerationSizes( device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &acceleration.primitiveCount, &acceleration.sizes );
			scratchBytes = MAX( scratchBytes, acceleration.sizes.buildScratchSize );
			blas.AddToTail( acceleration );
		}
	}
	// Binding 21 was planned before the class expansion; resize its unbound buffer now.
	vkDestroyBuffer( device, buffers[RESTIR_BIND_HW_PRIM_MAP].handle, NULL );
	buffers[RESTIR_BIND_HW_PRIM_MAP].handle = VK_NULL_HANDLE;
	int newMap = AddBuffer( (VkDeviceSize)hardwarePrimitiveMap.Count() * sizeof( unsigned int ), true );
	buffers[RESTIR_BIND_HW_PRIM_MAP] = buffers[newMap];
	buffers.Remove( newMap );
	FOR_EACH_VEC( blas, i )
	{
		blas[i].vertexBuffer = AddBuffer( (VkDeviceSize)blas[i].primitiveCount * 9 * sizeof( float ), true, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR );
		blas[i].storageBuffer = AddBuffer( blas[i].sizes.accelerationStructureSize, true, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR );
	}
	instanceBuffer = AddBuffer( (VkDeviceSize)MAX( 1, blas.Count() ) * sizeof( VkAccelerationStructureInstanceKHR ), true, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR );
	VkAccelerationStructureGeometryKHR instances = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	instances.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	instances.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	VkAccelerationStructureBuildGeometryInfoKHR build = BuildInfo( VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &instances );
	unsigned int count = blas.Count();
	getAccelerationSizes( device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &count, &tlas.sizes );
	tlas.primitiveCount = count;
	tlas.storageBuffer = AddBuffer( tlas.sizes.accelerationStructureSize, true, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR );
	scratchBytes = MAX( scratchBytes, tlas.sizes.buildScratchSize );
	scratchBuffer = AddBuffer( scratchBytes + scratchAlignment - 1, false );
}

void CReSTIRVulkanDevice::Impl::BuildAcceleration()
{
	FOR_EACH_VEC( blas, i )
	{
		ReSTIRAcceleration &acceleration = blas[i];
		Upload( acceleration.vertexBuffer, hardwareVertices.Base() + (VkDeviceSize)acceleration.firstPrimitive * 9, (VkDeviceSize)acceleration.primitiveCount * 9 * sizeof( float ) );
		VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
		create.buffer = buffers[acceleration.storageBuffer].handle;
		create.size = acceleration.sizes.accelerationStructureSize;
		create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
		Check( createAcceleration( device, &create, NULL, &acceleration.handle ), "vkCreateAccelerationStructureKHR(BLAS)" );
	}
	VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
	create.buffer = buffers[tlas.storageBuffer].handle;
	create.size = tlas.sizes.accelerationStructureSize;
	create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
	Check( createAcceleration( device, &create, NULL, &tlas.handle ), "vkCreateAccelerationStructureKHR(TLAS)" );
	Wait();
	ResetQueries();
	VkCommandBuffer command = BeginCommands();
	unsigned int start = Timestamp( command );
	VkDeviceAddress scratchAddress = ( BufferAddress( scratchBuffer ) + scratchAlignment - 1 ) & ~( scratchAlignment - 1 );
	FOR_EACH_VEC( blas, i )
	{
		ReSTIRAcceleration &acceleration = blas[i];
		unsigned int triangle = hardwarePrimitiveMap[acceleration.firstPrimitive];
		bool opaque = !( scene->triangles[triangle].flags & RESTIR_TRI_NONOPAQUE );
		VkAccelerationStructureGeometryKHR geometry = TriangleGeometry( BufferAddress( acceleration.vertexBuffer ), acceleration.primitiveCount, opaque );
		VkAccelerationStructureBuildGeometryInfoKHR build = BuildInfo( VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, &geometry );
		build.dstAccelerationStructure = acceleration.handle;
		build.scratchData.deviceAddress = scratchAddress;
		VkAccelerationStructureBuildRangeInfoKHR range = {};
		range.primitiveCount = acceleration.primitiveCount;
		const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
		cmdBuildAcceleration( command, 1, &build, &ranges );
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
		vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, 0, 1, &barrier, 0, NULL, 0, NULL );
	}
	unsigned int end = Timestamp( command );
	Submit( command );
	Wait();
	timings.sceneBuildMs += TimestampMs( start, end );
	CUtlVector<VkAccelerationStructureInstanceKHR> instances;
	instances.SetCount( blas.Count() );
	FOR_EACH_VEC( blas, i )
	{
		VkAccelerationStructureInstanceKHR &instance = instances[i];
		memset( &instance, 0, sizeof( instance ) );
		instance.transform.matrix[0][0] = instance.transform.matrix[1][1] = instance.transform.matrix[2][2] = 1;
		instance.instanceCustomIndex = blas[i].firstPrimitive;
		instance.mask = blas[i].mask;
		instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
		VkAccelerationStructureDeviceAddressInfoKHR address = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
		address.accelerationStructure = blas[i].handle;
		instance.accelerationStructureReference = getAccelerationAddress( device, &address );
	}
	Upload( instanceBuffer, instances.Base(), (VkDeviceSize)instances.Count() * sizeof( VkAccelerationStructureInstanceKHR ) );
	Wait();
	ResetQueries();
	command = BeginCommands();
	start = Timestamp( command );
	VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
	geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
	geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
	geometry.geometry.instances.data.deviceAddress = BufferAddress( instanceBuffer );
	VkAccelerationStructureBuildGeometryInfoKHR build = BuildInfo( VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, &geometry );
	build.dstAccelerationStructure = tlas.handle;
	build.scratchData.deviceAddress = scratchAddress;
	VkAccelerationStructureBuildRangeInfoKHR range = {};
	range.primitiveCount = instances.Count();
	const VkAccelerationStructureBuildRangeInfoKHR *ranges = &range;
	cmdBuildAcceleration( command, 1, &build, &ranges );
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
	barrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
	vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
	end = Timestamp( command );
	Submit( command );
	Wait();
	timings.sceneBuildMs += TimestampMs( start, end );
	hardwareVertices.Purge();
}

void CReSTIRVulkanDevice::Impl::UploadCoverage()
{
	VkSamplerCreateInfo sampler = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
	sampler.magFilter = sampler.minFilter = VK_FILTER_NEAREST;
	sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
	sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
	Check( vkCreateSampler( device, &sampler, NULL, &coverageSampler ), "vkCreateSampler" );
	FOR_EACH_VEC( images, i )
	{
		ReSTIRImage &image = images[i];
		VkImageViewCreateInfo view = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
		view.image = image.handle;
		view.viewType = VK_IMAGE_VIEW_TYPE_2D;
		view.format = VK_FORMAT_R8_UNORM;
		view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
		Check( vkCreateImageView( device, &view, NULL, &image.view ), "vkCreateImageView" );
		unsigned char opaqueTexel = 255;
		const unsigned char *texels = scene->coverageTextures.Count() ? scene->coverageTextures[i].texels.Base() : &opaqueTexel;
		unsigned int rowsPerChunk = (unsigned int)( RESTIR_STAGING_BYTES / image.width );
		for ( unsigned int firstRow = 0; firstRow < image.height; )
		{
			Wait();
			unsigned int rows = MIN( image.height - firstRow, rowsPerChunk );
			memcpy( mappedStaging, texels + (VkDeviceSize)firstRow * image.width, (size_t)rows * image.width );
			if ( !stagingCoherent )
			{
				VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
				range.memory = stagingMemory;
				range.size = VK_WHOLE_SIZE;
				Check( vkFlushMappedMemoryRanges( device, 1, &range ), "vkFlushMappedMemoryRanges(coverage)" );
			}
			VkCommandBuffer command = BeginCommands();
			VkImageMemoryBarrier barrier = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
			barrier.image = image.handle;
			barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.subresourceRange = view.subresourceRange;
			barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			if ( !firstRow )
				vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );
			VkBufferImageCopy copy = {};
			copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			copy.imageSubresource.layerCount = 1;
			copy.imageOffset.y = firstRow;
			copy.imageExtent.width = image.width;
			copy.imageExtent.height = rows;
			copy.imageExtent.depth = 1;
			vkCmdCopyBufferToImage( command, staging, image.handle, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy );
			firstRow += rows;
			if ( firstRow == image.height )
			{
				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &barrier );
			}
			Submit( command );
		}
	}
}

bool CReSTIRVulkanDevice::UploadScene( const ReSTIRScene &scene )
{
	Impl &gpu = *m_pImpl;
	if ( !gpu.device || gpu.scene )
		gpu.Fail( "UploadScene requires an initialized device and no previous scene" );
	gpu.scene = &scene;
	gpu.push.numLights = scene.lights.Count();
	gpu.push.numStyles = scene.sceneStyles.Count();
	gpu.push.numTriangles = scene.triangles.Count();
	gpu.push.numSamples = scene.samples.Count();
	gpu.push.numFaces = scene.faces.Count();
	gpu.push.numLuxels = scene.luxels.Count();
	gpu.push.skyLight = scene.skyLight;
	gpu.push.skyAmbientLight = scene.skyAmbientLight;
	gpu.push.flags = gpu.hardware ? RESTIR_PC_HARDWARE_RT : 0;
	for ( int axis = 0; axis < 3; ++axis )
	{
		gpu.push.worldMins[axis] = scene.worldMins[axis];
		gpu.push.worldMaxs[axis] = scene.worldMaxs[axis];
	}
	CUtlVector<ReSTIRGpuFace> faces;
	faces.CopyArray( scene.faces.Base(), scene.faces.Count() );
	uint64_t reservoirCount = 0;
	FOR_EACH_VEC( faces, i )
	{
		faces[i].firstReservoir = (int)reservoirCount;
		reservoirCount += (uint64_t)faces[i].numSamples * faces[i].numStyles;
		if ( reservoirCount > INT_MAX )
			gpu.Fail( "reservoir indexing exceeds signed 32-bit range" );
	}
	gpu.push.numReservoirs = (unsigned int)reservoirCount;
	while ( gpu.paddedTriangles < gpu.push.numTriangles )
	{
		if ( gpu.paddedTriangles > 0x40000000u )
			gpu.Fail( "triangle sort count exceeds 32-bit range" );
		gpu.paddedTriangles <<= 1;
	}
	VkDeviceSize sizes[RESTIR_BIND_COVERAGE] =
	{
		(VkDeviceSize)scene.triangles.Count() * sizeof( ReSTIRGpuTriangle ),
		(VkDeviceSize)scene.materials.Count() * sizeof( ReSTIRGpuMaterial ),
		(VkDeviceSize)scene.lights.Count() * sizeof( ReSTIRGpuLight ),
		(VkDeviceSize)faces.Count() * sizeof( ReSTIRGpuFace ),
		(VkDeviceSize)scene.samples.Count() * sizeof( ReSTIRGpuSample ),
		(VkDeviceSize)scene.luxels.Count() * sizeof( ReSTIRGpuLuxel ),
		(VkDeviceSize)( scene.faceNeighbors.Count() + scene.dfaceToFace.Count() ) * sizeof( int ),
		(VkDeviceSize)scene.luxels.Count() * sizeof( int ),
		reservoirCount * sizeof( ReSTIRReservoir ),
		reservoirCount * sizeof( ReSTIRReservoir ),
		reservoirCount * sizeof( ReSTIRReservoir ),
		reservoirCount * RESTIR_MAX_CHANNELS * sizeof( float ) * 4,
		(VkDeviceSize)scene.numOutputValues * sizeof( float ) * 4,
		(VkDeviceSize)scene.luxels.Count() * sizeof( unsigned int ),
		(VkDeviceSize)scene.numOutputValues * sizeof( float ) * 4,
		RESTIR_STAGING_BYTES, RESTIR_STAGING_BYTES,
		gpu.hardware || !scene.triangles.Count() ? 16 : ( (VkDeviceSize)scene.triangles.Count() * 2 - 1 ) * sizeof( ReSTIRBvhNode ),
		gpu.hardware ? 16 : (VkDeviceSize)scene.triangles.Count() * sizeof( unsigned int ),
		gpu.hardware ? 16 : (VkDeviceSize)gpu.paddedTriangles * 2 * sizeof( unsigned int ) * 2,
		NUMVERTEXNORMALS * sizeof( float ) * 4,
		16,
		(VkDeviceSize)scene.sceneStyles.Count() * sizeof( int )
	};
	for ( int binding = 0; binding < RESTIR_BIND_COVERAGE; ++binding )
	{
		if ( sizes[binding] > gpu.properties.limits.maxStorageBufferRange )
			gpu.Fail( "scene buffer exceeds maxStorageBufferRange" );
		bool sceneHeap = binding <= RESTIR_BIND_CELL_SAMPLES || binding == RESTIR_BIND_FINAL_LIGHTMAP || binding >= RESTIR_BIND_ANORMS;
		gpu.AddBuffer( sizes[binding], sceneHeap );
	}
	int imageCount = MAX( 1, scene.coverageTextures.Count() );
	if ( (unsigned int)imageCount > gpu.properties.limits.maxPerStageDescriptorSamplers || (unsigned int)imageCount > gpu.properties.limits.maxDescriptorSetSampledImages )
		gpu.Fail( "coverage texture count exceeds GPU descriptor limits" );
	for ( int i = 0; i < imageCount; ++i )
	{
		ReSTIRImage image;
		image.width = scene.coverageTextures.Count() ? scene.coverageTextures[i].width : 1;
		image.height = scene.coverageTextures.Count() ? scene.coverageTextures[i].height : 1;
		if ( !image.width || !image.height || image.width > gpu.properties.limits.maxImageDimension2D || image.height > gpu.properties.limits.maxImageDimension2D || ( scene.coverageTextures.Count() && (VkDeviceSize)scene.coverageTextures[i].texels.Count() != (VkDeviceSize)image.width * image.height ) )
			gpu.Fail( "invalid coverage texture dimensions or texel count" );
		VkImageCreateInfo create = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
		create.imageType = VK_IMAGE_TYPE_2D;
		create.format = VK_FORMAT_R8_UNORM;
		create.extent.width = image.width;
		create.extent.height = image.height;
		create.extent.depth = 1;
		create.mipLevels = create.arrayLayers = 1;
		create.samples = VK_SAMPLE_COUNT_1_BIT;
		create.tiling = VK_IMAGE_TILING_OPTIMAL;
		create.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		gpu.Check( vkCreateImage( gpu.device, &create, NULL, &image.handle ), "vkCreateImage(coverage)" );
		vkGetImageMemoryRequirements( gpu.device, image.handle, &image.requirements );
		gpu.images.AddToTail( image );
	}
	if ( gpu.hardware )
		gpu.PlanAcceleration();
	gpu.AllocateHeaps();
	gpu.Upload( RESTIR_BIND_TRIANGLES, scene.triangles.Base(), sizes[RESTIR_BIND_TRIANGLES] );
	gpu.Upload( RESTIR_BIND_MATERIALS, scene.materials.Base(), sizes[RESTIR_BIND_MATERIALS] );
	gpu.Upload( RESTIR_BIND_LIGHTS, scene.lights.Base(), sizes[RESTIR_BIND_LIGHTS] );
	gpu.Upload( RESTIR_BIND_FACES, faces.Base(), sizes[RESTIR_BIND_FACES] );
	gpu.Upload( RESTIR_BIND_SAMPLES, scene.samples.Base(), sizes[RESTIR_BIND_SAMPLES] );
	gpu.Upload( RESTIR_BIND_LUXELS, scene.luxels.Base(), sizes[RESTIR_BIND_LUXELS] );
	gpu.Upload( RESTIR_BIND_FACE_NEIGHBORS, scene.faceNeighbors.Base(), (VkDeviceSize)scene.faceNeighbors.Count() * sizeof( int ) );
	gpu.Upload( RESTIR_BIND_FACE_NEIGHBORS, scene.dfaceToFace.Base(), (VkDeviceSize)scene.dfaceToFace.Count() * sizeof( int ), (VkDeviceSize)scene.faceNeighbors.Count() * sizeof( int ) );
	CUtlVector<int> cellSamples;
	cellSamples.SetCount( scene.luxels.Count() );
	FOR_EACH_VEC( cellSamples, i )
		cellSamples[i] = -1;
	FOR_EACH_VEC( scene.samples, i )
	{
		const ReSTIRGpuSample &sample = scene.samples[i];
		const ReSTIRGpuFace &face = faces[sample.face];
		if ( sample.s >= 0 && sample.s < face.luxelW && sample.t >= 0 && sample.t < face.luxelH )
			cellSamples[face.firstLuxel + sample.s + sample.t * face.luxelW] = i;
	}
	gpu.Upload( RESTIR_BIND_CELL_SAMPLES, cellSamples.Base(), sizes[RESTIR_BIND_CELL_SAMPLES] );
	float normals[NUMVERTEXNORMALS][4];
	for ( int i = 0; i < NUMVERTEXNORMALS; ++i )
	{
		normals[i][0] = g_anorms[i].x;
		normals[i][1] = g_anorms[i].y;
		normals[i][2] = g_anorms[i].z;
		normals[i][3] = 0;
	}
	gpu.Upload( RESTIR_BIND_ANORMS, normals, sizeof( normals ) );
	gpu.Upload( RESTIR_BIND_SCENE_STYLES, scene.sceneStyles.Base(), sizes[RESTIR_BIND_SCENE_STYLES] );
	gpu.UploadCoverage();
	if ( gpu.hardware )
	{
		gpu.Upload( RESTIR_BIND_HW_PRIM_MAP, gpu.hardwarePrimitiveMap.Base(), (VkDeviceSize)gpu.hardwarePrimitiveMap.Count() * sizeof( unsigned int ) );
		gpu.BuildAcceleration();
	}
	gpu.CreateDescriptorsAndPipelines();
	gpu.Wait();
	if ( !gpu.hardware && scene.triangles.Count() )
	{
		gpu.ResetQueries();
		VkCommandBuffer command = gpu.BeginCommands();
		unsigned int start = gpu.Timestamp( command );
		gpu.Dispatch( command, RESTIR_PIPE_MORTON, gpu.paddedTriangles );
		gpu.Barrier( command );
		for ( unsigned int logK = 1; logK < 32 && ( 1u << logK ) <= gpu.paddedTriangles; ++logK )
		{
			unsigned int k = 1u << logK;
			gpu.push.iteration = logK;
			for ( unsigned int j = k / 2; j; j /= 2 )
			{
				gpu.Dispatch( command, RESTIR_PIPE_SORT, gpu.paddedTriangles, j );
				gpu.Barrier( command );
			}
		}
		gpu.push.iteration = 0;
		gpu.Dispatch( command, RESTIR_PIPE_HIERARCHY, scene.triangles.Count() );
		gpu.Barrier( command );
		gpu.Dispatch( command, RESTIR_PIPE_REFIT, scene.triangles.Count() );
		gpu.Barrier( command );
		unsigned int end = gpu.Timestamp( command );
		gpu.Submit( command );
		gpu.Wait();
		gpu.timings.sceneBuildMs = gpu.TimestampMs( start, end );
	}
	return true;
}
