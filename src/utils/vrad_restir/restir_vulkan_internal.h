//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef RESTIR_VULKAN_INTERNAL_H
#define RESTIR_VULKAN_INTERNAL_H
#pragma once

#include "restir_vulkan.h"
#include "restir_gpu_layout.h"
#include "tier0/dbg.h"
#include <vulkan/vulkan.h>

static const VkDeviceSize RESTIR_STAGING_BYTES = 64ull * 1024 * 1024;
static const double RESTIR_SUBMIT_TARGET_SECONDS = 0.25;
static const unsigned int RESTIR_SUBMIT_INITIAL_ITEMS = 8192;
static const unsigned int RESTIR_SUBMIT_MIN_ITEMS = 1024;
// Ray-query cost varies sharply with page-local face/light distributions. The
// adaptive sizer uses this as a watchdog safety ceiling, not as its target.
static const unsigned int RESTIR_SUBMIT_MAX_ITEMS = 65536;
static const unsigned int RESTIR_DIRECT_SAFE_ITEMS = 8192;		// one shadow ray per point/spot light of the style
static const unsigned int RESTIR_TRANSPORT_SAFE_ITEMS = 16384;

enum ReSTIRPipeline
{
	RESTIR_PIPE_MORTON,
	RESTIR_PIPE_SORT,
	RESTIR_PIPE_HIERARCHY,
	RESTIR_PIPE_REFIT,
	RESTIR_PIPE_INIT,
	RESTIR_PIPE_DIRECT,
	RESTIR_PIPE_TRANSPORT,
	RESTIR_PIPE_RECONSTRUCT,
	RESTIR_PIPE_TRACE,
	RESTIR_PIPE_AMBIENT,
	RESTIR_PIPE_POINTS,
	RESTIR_PIPE_SUN_VISIBILITY,
	RESTIR_PIPE_LOCAL_VISIBILITY,
	RESTIR_PIPE_PROBE_SH,
	RESTIR_PIPE_COUNT
};

struct ReSTIRBuffer
{
	VkBuffer handle;
	VkDeviceSize size;
	VkDeviceSize offset;
	VkMemoryRequirements requirements;
	VkBufferUsageFlags usage;
	bool sceneHeap;
	ReSTIRBuffer() : handle( VK_NULL_HANDLE ), size( 0 ), offset( 0 ), usage( 0 ), sceneHeap( false )
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
	unsigned int channels;		// 1 = R8 coverage, 4 = RGBA8 albedo (ReSTIRSceneTexture::channels)
	VkFormat Format() const { return channels == 4 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8_UNORM; }
	ReSTIRImage() : handle( VK_NULL_HANDLE ), view( VK_NULL_HANDLE ), offset( 0 ), width( 0 ), height( 0 ), channels( 1 )
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
struct ReSTIRPage
{
	unsigned int firstFace, faceCount;
	unsigned int firstSample, sampleCount;
	unsigned int firstLuxel, luxelCount;
	unsigned int firstReservoir, reservoirCount;
	unsigned int firstOutput, outputCount;
	ReSTIRPage() : firstFace( 0 ), faceCount( 0 ), firstSample( 0 ), sampleCount( 0 ),
		luxelCount( 0 ), firstLuxel( 0 ), firstReservoir( 0 ), reservoirCount( 0 ),
		firstOutput( 0 ), outputCount( 0 ) {}
};

struct ReSTIRGpuStageStats
{
	uint64_t submits;
	unsigned int minChunk;
	unsigned int maxChunk;
	unsigned int finalChunk;
	ReSTIRGpuStageStats() : submits( 0 ), minChunk( 0 ), maxChunk( 0 ), finalChunk( 0 ) {}
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
	bool receiverFullForReconstruct;
	bool transportPaged;
	unsigned int receiverPageReservoirs;
	unsigned int fullReservoirs;
	unsigned int pageSamples;
	unsigned int pageLuxels;
	unsigned int pageReservoirs;
	unsigned int pageOutputs;
	unsigned int pageCount;
	unsigned int reconstructSampleCapacity;
	unsigned int reconstructReservoirCapacity;
	CUtlVector<ReSTIRPage> pages;
	CUtlVector<ReSTIRGpuFace> uploadedFaces;
	VkCommandPool commandPool;
	VkSemaphore timeline;
	uint64_t submittedValue;
	CUtlVector<VkCommandBuffer> pendingCommands;
	VkQueryPool queryPool;
	unsigned int queryCursor;
	ReSTIRGpuStageStats stageStats[RESTIR_PIPE_COUNT];
	double lastBakeSeconds;
	uint64_t lastBakeSubmits;
	ReSTIRProgressCallback progressCallback;
	void *progressUser;
	VkDescriptorSetLayout setLayout;
	VkDescriptorPool descriptorPool;
	VkDescriptorSet descriptorSet;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipelines[RESTIR_PIPE_COUNT];
	VkSampler textureSampler;
	CUtlVector<ReSTIRBuffer> buffers;
	CUtlVector<ReSTIRImage> images;
	CUtlVector<ReSTIRAcceleration> blas;
	ReSTIRAcceleration tlas;
	int instanceBuffer;
	int scratchBuffer;
	VkDeviceSize scratchAlignment;
	CUtlVector<unsigned int> hardwarePrimitiveMap;
	CUtlVector<float> hardwareVertices;
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
	void RecreateBuffer( int index, VkDeviceSize bytes );
	VkDeviceAddress BufferAddress( int index ) const;
	void AllocateHeaps();
	bool IsPagedBinding( int binding ) const;
	void BuildPages( VkDeviceSize room );
	void RebuildPageBufferSizes();
	void PrepareReconstructionPages( unsigned int sampleCapacity, unsigned int reservoirCapacity, unsigned int luxelCapacity );
	void RefreshDescriptorSet();
	void ReallocateForReconstruct();
	void CreateDescriptorsAndPipelines();
	VkCommandBuffer BeginCommands();
	void Submit( VkCommandBuffer command );
	void Wait();
	void Barrier( VkCommandBuffer command ) const;
	void ReportProgress( const char *phase, unsigned int done, unsigned int total ) const;
	unsigned int DispatchItemLimit() const;
	void DispatchRange( VkCommandBuffer command, ReSTIRPipeline pipeline, unsigned int first,
		unsigned int count, unsigned int pass = 0 );
	void Dispatch( VkCommandBuffer command, ReSTIRPipeline pipeline, unsigned int count, unsigned int pass = 0 );
	void Upload( int buffer, const void *data, VkDeviceSize bytes, VkDeviceSize offset = 0 );
	void Download( int buffer, void *data, VkDeviceSize bytes, VkDeviceSize offset = 0, double *pTransferMs = NULL );
	void UploadTextures();
	void PlanAcceleration();
	void BuildAcceleration();
	bool HasSelectedSun() const;
	void RunService( ReSTIRPipeline pipeline, const void *input, unsigned int inputStride, void *output, unsigned int outputStride, unsigned int count, unsigned int styles, unsigned int mask );
	unsigned int Timestamp( VkCommandBuffer command );
	double TimestampMs( unsigned int first, unsigned int last );
	void ResetQueries();
};

#endif // RESTIR_VULKAN_INTERNAL_H
