//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_VULKAN_INTERNAL_H
#define RESTIR_VULKAN_INTERNAL_H
#pragma once

#include "restir_vulkan.h"
#include "restir_gpu_layout.h"
#include "tier0/dbg.h"
#include <vulkan/vulkan.h>

static const VkDeviceSize RESTIR_STAGING_BYTES = 64ull * 1024 * 1024;

enum ReSTIRPipeline
{
	RESTIR_PIPE_MORTON,
	RESTIR_PIPE_SORT,
	RESTIR_PIPE_HIERARCHY,
	RESTIR_PIPE_REFIT,
	RESTIR_PIPE_INIT,
	RESTIR_PIPE_CANDIDATES,
	RESTIR_PIPE_TEMPORAL,
	RESTIR_PIPE_SPATIAL,
	RESTIR_PIPE_ACCUMULATE,
	RESTIR_PIPE_RECONSTRUCT,
	RESTIR_PIPE_TRACE,
	RESTIR_PIPE_AMBIENT,
	RESTIR_PIPE_POINTS,
	RESTIR_PIPE_COUNT
};

struct ReSTIRBuffer
{
	VkBuffer handle;
	VkDeviceSize size;
	VkDeviceSize offset;
	VkMemoryRequirements requirements;
	bool sceneHeap;
	ReSTIRBuffer() : handle( VK_NULL_HANDLE ), size( 0 ), offset( 0 ), sceneHeap( false )
	{
		memset( &requirements, 0, sizeof( requirements ) );
	}
};

struct ReSTIRImage
{
	VkImage handle;
	VkImageView view;
	VkMemoryRequirements requirements;
	VkDeviceSize offset;
	unsigned int width;
	unsigned int height;
	ReSTIRImage() : handle( VK_NULL_HANDLE ), view( VK_NULL_HANDLE ), offset( 0 ), width( 0 ), height( 0 )
	{
		memset( &requirements, 0, sizeof( requirements ) );
	}
};

struct ReSTIRAcceleration
{
	VkAccelerationStructureKHR handle;
	int vertexBuffer;
	int storageBuffer;
	unsigned int mask;
	unsigned int firstPrimitive;
	unsigned int primitiveCount;
	VkAccelerationStructureBuildSizesInfoKHR sizes;
	ReSTIRAcceleration() : handle( VK_NULL_HANDLE ), vertexBuffer( -1 ), storageBuffer( -1 ), mask( 0 ), firstPrimitive( 0 ), primitiveCount( 0 )
	{
		memset( &sizes, 0, sizeof( sizes ) );
		sizes.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
	}
};

struct CReSTIRVulkanDevice::Impl
{
	VkInstance instance;
	VkPhysicalDevice physical;
	VkDevice device;
	VkQueue queue;
	unsigned int queueFamily;
	unsigned int timestampBits;
	VkPhysicalDeviceProperties properties;
	VkPhysicalDeviceMemoryProperties memoryProperties;
	VkDeviceMemory sceneMemory;
	VkDeviceMemory workMemory;
	VkDeviceMemory stagingMemory;
	VkBuffer staging;
	unsigned char *mappedStaging;
	bool stagingCoherent;
	bool memoryBudget;
	bool hardware;
	bool finalUploaded;
	VkCommandPool commandPool;
	VkSemaphore timeline;
	uint64_t submittedValue;
	CUtlVector<VkCommandBuffer> pendingCommands;
	VkQueryPool queryPool;
	unsigned int queryCursor;
	VkDescriptorSetLayout setLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipelines[RESTIR_PIPE_COUNT];
	VkSampler coverageSampler;
	CUtlVector<ReSTIRBuffer> buffers;
	CUtlVector<ReSTIRImage> images;
	CUtlVector<ReSTIRAcceleration> blas;
	ReSTIRAcceleration tlas;
	int instanceBuffer;
	int scratchBuffer;
	VkDeviceSize scratchAlignment;
	CUtlVector<unsigned int> hardwarePrimitiveMap;
	CUtlVector<float> hardwareVertices;
	unsigned int reservoirBindings[3];
	unsigned int paddedTriangles;
	const ReSTIRScene *scene;
	ReSTIROptions options;
	ReSTIRDeviceInfo info;
	ReSTIRGpuTimings timings;
	ReSTIRPushConstants push;
	PFN_vkCreateAccelerationStructureKHR createAcceleration;
	PFN_vkDestroyAccelerationStructureKHR destroyAcceleration;
	PFN_vkGetAccelerationStructureBuildSizesKHR getAccelerationSizes;
	PFN_vkGetAccelerationStructureDeviceAddressKHR getAccelerationAddress;
	PFN_vkCmdBuildAccelerationStructuresKHR cmdBuildAcceleration;

	Impl();
	void Release();
	void Check( VkResult result, const char *operation ) const;
	void Fail( const char *message ) const;
	unsigned int MemoryType( unsigned int bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred = 0 ) const;
	int AddBuffer( VkDeviceSize bytes, bool useSceneHeap, VkBufferUsageFlags extraUsage = 0 );
	VkDeviceAddress BufferAddress( int index ) const;
	void AllocateHeaps();
	void CreateDescriptorsAndPipelines();
	void WriteReservoirDescriptors();
	VkCommandBuffer BeginCommands();
	void Submit( VkCommandBuffer command );
	void Wait();
	void Barrier( VkCommandBuffer command ) const;
	void Dispatch( VkCommandBuffer command, ReSTIRPipeline pipeline, unsigned int count, unsigned int pass = 0 );
	void Upload( int buffer, const void *data, VkDeviceSize bytes, VkDeviceSize offset = 0 );
	void Download( int buffer, void *data, VkDeviceSize bytes, VkDeviceSize offset = 0, double *pTransferMs = NULL );
	void UploadCoverage();
	void PlanAcceleration();
	void BuildAcceleration();
	void RunService( ReSTIRPipeline pipeline, const void *input, unsigned int inputStride, void *output, unsigned int outputStride, unsigned int count, unsigned int styles, unsigned int mask );
	unsigned int Timestamp( VkCommandBuffer command );
	double TimestampMs( unsigned int first, unsigned int last );
	void ResetQueries();
};

#endif // RESTIR_VULKAN_INTERNAL_H
