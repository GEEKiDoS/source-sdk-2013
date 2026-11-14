//========= Copyright Valve Corporation, All rights reserved. ============//
#include "restir_vulkan_internal.h"
#include "cmdlib.h"
#include <windows.h>
#include <stdio.h>
#include <limits.h>

static const char *s_pPipelineNames[RESTIR_PIPE_COUNT] =
{
	"bvh_morton", "bvh_sort", "bvh_hierarchy", "bvh_refit", "restir_init",
	"restir_direct", "restir_transport",
	"restir_reconstruct", "restir_trace_rays", "restir_gather_ambient", "restir_light_points",
	"restir_sun_visibility", "restir_local_visibility", "restir_probe_sh"
};

static bool HasExtension( const CUtlVector<VkExtensionProperties> &extensions, const char *name )
{
	FOR_EACH_VEC( extensions, i )
	{
		if ( !strcmp( extensions[i].extensionName, name ) )
			return true;
	}
	return false;
}

static VkDeviceSize AlignSize( VkDeviceSize value, VkDeviceSize alignment )
{
	return ( value + alignment - 1 ) & ~( alignment - 1 );
}

CReSTIRVulkanDevice::Impl::Impl()
	: instance( VK_NULL_HANDLE ), physical( VK_NULL_HANDLE ), device( VK_NULL_HANDLE ), queue( VK_NULL_HANDLE ),
	queueFamily( 0 ), timestampBits( 0 ), sceneMemory( VK_NULL_HANDLE ), workMemory( VK_NULL_HANDLE ),
	stagingMemory( VK_NULL_HANDLE ), staging( VK_NULL_HANDLE ), mappedStaging( NULL ), stagingCoherent( false ),
	memoryBudget( false ), hardware( false ), finalUploaded( false ), receiverFullForReconstruct( false ),
	transportPaged( false ), receiverPageReservoirs( 0 ), fullReservoirs( 0 ), pageSamples( 0 ), pageLuxels( 0 ),
	pageReservoirs( 0 ), pageOutputs( 0 ), pageCount( 0 ), reconstructSampleCapacity( 0 ), reconstructReservoirCapacity( 0 ),
	commandPool( VK_NULL_HANDLE ),
	timeline( VK_NULL_HANDLE ), submittedValue( 0 ), queryPool( VK_NULL_HANDLE ), queryCursor( 0 ),
	lastBakeSeconds( 0.0 ), lastBakeSubmits( 0 ),
	progressCallback( NULL ), progressUser( NULL ),
	setLayout( VK_NULL_HANDLE ), descriptorPool( VK_NULL_HANDLE ), descriptorSet( VK_NULL_HANDLE ),
	pipelineLayout( VK_NULL_HANDLE ), textureSampler( VK_NULL_HANDLE ), instanceBuffer( -1 ), scratchBuffer( -1 ),
	scratchAlignment( 256 ), paddedTriangles( 1 ), scene( NULL ), createAcceleration( NULL ),
	destroyAcceleration( NULL ), getAccelerationSizes( NULL ), getAccelerationAddress( NULL ), cmdBuildAcceleration( NULL )
{
	memset( &properties, 0, sizeof( properties ) );
	memset( &memoryProperties, 0, sizeof( memoryProperties ) );
	memset( &timings, 0, sizeof( timings ) );
	memset( stageStats, 0, sizeof( stageStats ) );
	memset( &push, 0, sizeof( push ) );
	memset( pipelines, 0, sizeof( pipelines ) );
	memset( info.luid, 0, sizeof( info.luid ) );
	memset( info.uuid, 0, sizeof( info.uuid ) );
	info.vendorId = info.driverVersion = info.apiVersion = 0;
	info.luidValid = false;
	info.backend = RESTIR_BACKEND_COMPUTE_BVH;
}

void CReSTIRVulkanDevice::Impl::Fail( const char *message ) const
{
	Error( "VRAD ReSTIR: %s: %s (VkResult=%d)\n", options.mapPath.String(), message, VK_ERROR_INITIALIZATION_FAILED );
}

void CReSTIRVulkanDevice::Impl::Check( VkResult result, const char *operation ) const
{
	if ( result != VK_SUCCESS )
		Error( "VRAD ReSTIR: %s: %s failed (VkResult=%d)\n", options.mapPath.String(), operation, result );
}

CReSTIRVulkanDevice::CReSTIRVulkanDevice() : m_pImpl( new Impl )
{
}
CReSTIRVulkanDevice::~CReSTIRVulkanDevice()
{
	m_pImpl->Release();
	delete m_pImpl;
}
void CReSTIRVulkanDevice::SetProgressCallback( ReSTIRProgressCallback callback, void *user )
{
	m_pImpl->progressCallback = callback;
	m_pImpl->progressUser = user;
}

void CReSTIRVulkanDevice::Impl::ReportProgress( const char *phase, unsigned int done, unsigned int total ) const
{
	if ( progressCallback )
		progressCallback( phase, done, total, progressUser );
}

bool CReSTIRVulkanDevice::Init( const ReSTIROptions &options )
{
	Impl &gpu = *m_pImpl;
	if ( gpu.instance )
		gpu.Fail( "device already initialized" );
	gpu.options = options;
	uint32_t version = VK_API_VERSION_1_0;
	gpu.Check( vkEnumerateInstanceVersion( &version ), "vkEnumerateInstanceVersion" );
	if ( version < VK_API_VERSION_1_2 )
		gpu.Fail( "Vulkan 1.2 is required" );
	VkApplicationInfo application = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
	application.pApplicationName = "VRAD ReSTIR";
	application.apiVersion = VK_API_VERSION_1_2;
	VkInstanceCreateInfo instanceInfo = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
	instanceInfo.pApplicationInfo = &application;
	gpu.Check( vkCreateInstance( &instanceInfo, NULL, &gpu.instance ), "vkCreateInstance" );
	uint32_t deviceCount = 0;
	gpu.Check( vkEnumeratePhysicalDevices( gpu.instance, &deviceCount, NULL ), "vkEnumeratePhysicalDevices" );
	if ( !deviceCount )
		gpu.Fail( "no Vulkan GPU" );
	CUtlVector<VkPhysicalDevice> physicalDevices;
	physicalDevices.SetCount( deviceCount );
	gpu.Check( vkEnumeratePhysicalDevices( gpu.instance, &deviceCount, physicalDevices.Base() ), "vkEnumeratePhysicalDevices" );
	int selected = options.gpuIndex;
	if ( selected < 0 )
	{
		selected = 0;
		FOR_EACH_VEC( physicalDevices, i )
		{
			VkPhysicalDeviceProperties properties;
			vkGetPhysicalDeviceProperties( physicalDevices[i], &properties );
			if ( properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU )
			{
				selected = i;
				break;
			}
		}
	}
	if ( selected >= physicalDevices.Count() )
		gpu.Fail( "-restir_gpu index is out of range" );
	gpu.physical = physicalDevices[selected];
	VkPhysicalDeviceIDProperties identity = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
	VkPhysicalDeviceSubgroupProperties subgroup = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES };
	VkPhysicalDeviceAccelerationStructurePropertiesKHR accelerationProperties = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR };
	VkPhysicalDeviceProperties2 properties2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	properties2.pNext = &identity;
	identity.pNext = &subgroup;
	vkGetPhysicalDeviceProperties2( gpu.physical, &properties2 );
	gpu.properties = properties2.properties;
	if ( gpu.properties.limits.maxPushConstantsSize < sizeof( ReSTIRPushConstants ) )
		gpu.Fail( "GPU push-constant range is smaller than the paging layout" );
	vkGetPhysicalDeviceMemoryProperties( gpu.physical, &gpu.memoryProperties );
	uint32_t familyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( gpu.physical, &familyCount, NULL );
	CUtlVector<VkQueueFamilyProperties> families;
	families.SetCount( familyCount );
	vkGetPhysicalDeviceQueueFamilyProperties( gpu.physical, &familyCount, families.Base() );
	bool foundQueue = false;
	FOR_EACH_VEC( families, i )
	{
		if ( families[i].queueCount && ( families[i].queueFlags & VK_QUEUE_COMPUTE_BIT ) )
		{
			gpu.queueFamily = i;
			gpu.timestampBits = families[i].timestampValidBits;
			foundQueue = true;
			if ( !( families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ) )
				break;
		}
	}
	if ( !foundQueue || !gpu.timestampBits )
		gpu.Fail( "a compute queue with timestamps is required" );
	uint32_t extensionCount = 0;
	gpu.Check( vkEnumerateDeviceExtensionProperties( gpu.physical, NULL, &extensionCount, NULL ), "vkEnumerateDeviceExtensionProperties" );
	CUtlVector<VkExtensionProperties> extensions;
	extensions.SetCount( extensionCount );
	gpu.Check( vkEnumerateDeviceExtensionProperties( gpu.physical, NULL, &extensionCount, extensions.Base() ), "vkEnumerateDeviceExtensionProperties" );
	gpu.memoryBudget = HasExtension( extensions, VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
	bool rayExtensions = HasExtension( extensions, VK_KHR_RAY_QUERY_EXTENSION_NAME ) && HasExtension( extensions, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME ) && HasExtension( extensions, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
	VkPhysicalDeviceRayQueryFeaturesKHR rayFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR };
	VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR };
	VkPhysicalDeviceVulkan12Features features12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
	VkPhysicalDeviceFeatures2 features = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
	features.pNext = &features12;
	if ( rayExtensions )
	{
		features12.pNext = &rayFeatures;
		rayFeatures.pNext = &accelerationFeatures;
	}
	vkGetPhysicalDeviceFeatures2( gpu.physical, &features );
	if ( !features12.bufferDeviceAddress || !features12.timelineSemaphore || !features12.descriptorIndexing || !features12.runtimeDescriptorArray || !features12.shaderSampledImageArrayNonUniformIndexing || !features12.descriptorBindingPartiallyBound || !features12.hostQueryReset )
		gpu.Fail( "GPU is missing required Vulkan 1.2 features" );
	gpu.hardware = rayExtensions && rayFeatures.rayQuery && accelerationFeatures.accelerationStructure && !options.forceComputeBvh;
	CUtlVector<const char *> enabledExtensions;
	if ( gpu.memoryBudget )
		enabledExtensions.AddToTail( VK_EXT_MEMORY_BUDGET_EXTENSION_NAME );
	if ( gpu.hardware )
	{
		enabledExtensions.AddToTail( VK_KHR_RAY_QUERY_EXTENSION_NAME );
		enabledExtensions.AddToTail( VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME );
		enabledExtensions.AddToTail( VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME );
		properties2.pNext = &accelerationProperties;
		vkGetPhysicalDeviceProperties2( gpu.physical, &properties2 );
		gpu.scratchAlignment = accelerationProperties.minAccelerationStructureScratchOffsetAlignment;
	}
	memset( &features, 0, sizeof( features ) );
	features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features.pNext = &features12;
	memset( &features12, 0, sizeof( features12 ) );
	features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	features12.bufferDeviceAddress = features12.timelineSemaphore = features12.descriptorIndexing = VK_TRUE;
	features12.runtimeDescriptorArray = features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
	features12.descriptorBindingPartiallyBound = features12.hostQueryReset = VK_TRUE;
	if ( gpu.hardware )
	{
		memset( &rayFeatures, 0, sizeof( rayFeatures ) );
		rayFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
		rayFeatures.pNext = &accelerationFeatures;
		memset( &accelerationFeatures, 0, sizeof( accelerationFeatures ) );
		accelerationFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
		features12.pNext = &rayFeatures;
		rayFeatures.rayQuery = VK_TRUE;
		accelerationFeatures.accelerationStructure = VK_TRUE;
	}
	float priority = 1.0f;
	VkDeviceQueueCreateInfo queueInfo = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
	queueInfo.queueFamilyIndex = gpu.queueFamily;
	queueInfo.queueCount = 1;
	queueInfo.pQueuePriorities = &priority;
	VkDeviceCreateInfo deviceInfo = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
	deviceInfo.pNext = &features;
	deviceInfo.queueCreateInfoCount = 1;
	deviceInfo.pQueueCreateInfos = &queueInfo;
	deviceInfo.enabledExtensionCount = enabledExtensions.Count();
	deviceInfo.ppEnabledExtensionNames = enabledExtensions.Base();
	gpu.Check( vkCreateDevice( gpu.physical, &deviceInfo, NULL, &gpu.device ), "vkCreateDevice" );
	vkGetDeviceQueue( gpu.device, gpu.queueFamily, 0, &gpu.queue );
	if ( gpu.hardware )
	{
		gpu.createAcceleration = (PFN_vkCreateAccelerationStructureKHR)vkGetDeviceProcAddr( gpu.device, "vkCreateAccelerationStructureKHR" );
		gpu.destroyAcceleration = (PFN_vkDestroyAccelerationStructureKHR)vkGetDeviceProcAddr( gpu.device, "vkDestroyAccelerationStructureKHR" );
		gpu.getAccelerationSizes = (PFN_vkGetAccelerationStructureBuildSizesKHR)vkGetDeviceProcAddr( gpu.device, "vkGetAccelerationStructureBuildSizesKHR" );
		gpu.getAccelerationAddress = (PFN_vkGetAccelerationStructureDeviceAddressKHR)vkGetDeviceProcAddr( gpu.device, "vkGetAccelerationStructureDeviceAddressKHR" );
		gpu.cmdBuildAcceleration = (PFN_vkCmdBuildAccelerationStructuresKHR)vkGetDeviceProcAddr( gpu.device, "vkCmdBuildAccelerationStructuresKHR" );
		if ( !gpu.createAcceleration || !gpu.destroyAcceleration || !gpu.getAccelerationSizes || !gpu.getAccelerationAddress || !gpu.cmdBuildAcceleration )
			gpu.Fail( "missing Vulkan acceleration structure entry points" );
	}
	VkCommandPoolCreateInfo poolInfo = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
	poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
	poolInfo.queueFamilyIndex = gpu.queueFamily;
	gpu.Check( vkCreateCommandPool( gpu.device, &poolInfo, NULL, &gpu.commandPool ), "vkCreateCommandPool" );
	VkSemaphoreTypeCreateInfo timelineInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO };
	timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	VkSemaphoreCreateInfo semaphoreInfo = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	semaphoreInfo.pNext = &timelineInfo;
	gpu.Check( vkCreateSemaphore( gpu.device, &semaphoreInfo, NULL, &gpu.timeline ), "vkCreateSemaphore" );
	VkQueryPoolCreateInfo queryInfo = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryInfo.queryCount = 64;
	gpu.Check( vkCreateQueryPool( gpu.device, &queryInfo, NULL, &gpu.queryPool ), "vkCreateQueryPool" );
	gpu.info.deviceName = gpu.properties.deviceName;
	gpu.info.vendorId = gpu.properties.vendorID;
	gpu.info.driverVersion = gpu.properties.driverVersion;
	gpu.info.apiVersion = gpu.properties.apiVersion;
	gpu.info.backend = gpu.hardware ? RESTIR_BACKEND_HARDWARE_RT : RESTIR_BACKEND_COMPUTE_BVH;
	gpu.info.luidValid = identity.deviceLUIDValid != VK_FALSE;
	memcpy( gpu.info.luid, identity.deviceLUID, sizeof( gpu.info.luid ) );
	memcpy( gpu.info.uuid, identity.deviceUUID, sizeof( gpu.info.uuid ) );
	Msg( "VRAD ReSTIR GPU: %s vendor=0x%04x driver=%u backend=%s\n", gpu.info.deviceName.String(), gpu.info.vendorId, gpu.info.driverVersion, gpu.hardware ? "hardware-rt" : "compute-bvh" );
	return true;
}

unsigned int CReSTIRVulkanDevice::Impl::MemoryType( unsigned int bits, VkMemoryPropertyFlags required, VkMemoryPropertyFlags preferred ) const
{
	for ( unsigned int i = 0; i < memoryProperties.memoryTypeCount; ++i )
	{
		VkMemoryPropertyFlags flags = memoryProperties.memoryTypes[i].propertyFlags;
		if ( ( bits & ( 1u << i ) ) && ( flags & required ) == required && ( flags & preferred ) == preferred )
			return i;
	}
	if ( preferred )
		return MemoryType( bits, required );
	if ( required == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )
		for ( unsigned int i = 0; i < memoryProperties.memoryTypeCount; ++i )
			if ( bits & ( 1u << i ) )
				return i;
	Error( "VRAD ReSTIR: no compatible GPU memory type bits=0x%08x required=0x%08x\n", bits, (unsigned int)required );
}

int CReSTIRVulkanDevice::Impl::AddBuffer( VkDeviceSize bytes, bool useSceneHeap, VkBufferUsageFlags extraUsage )
{
	ReSTIRBuffer buffer;
	buffer.size = MAX( bytes, (VkDeviceSize)16 );
	buffer.sceneHeap = useSceneHeap;
	buffer.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | extraUsage;
	VkBufferCreateInfo create = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	create.size = buffer.size;
	create.usage = buffer.usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	Check( vkCreateBuffer( device, &create, NULL, &buffer.handle ), "vkCreateBuffer" );
	vkGetBufferMemoryRequirements( device, buffer.handle, &buffer.requirements );
	return buffers.AddToTail( buffer );
}

void CReSTIRVulkanDevice::Impl::RecreateBuffer( int index, VkDeviceSize bytes )
{
	ReSTIRBuffer &buffer = buffers[index];
	if ( buffer.handle )
		vkDestroyBuffer( device, buffer.handle, NULL );
	buffer.handle = VK_NULL_HANDLE;
	buffer.size = MAX( bytes, (VkDeviceSize)16 );
	buffer.offset = 0;
	VkBufferCreateInfo create = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	create.size = buffer.size;
	create.usage = buffer.usage;
	create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	Check( vkCreateBuffer( device, &create, NULL, &buffer.handle ), "vkCreateBuffer(recreate)" );
	vkGetBufferMemoryRequirements( device, buffer.handle, &buffer.requirements );
}

VkDeviceAddress CReSTIRVulkanDevice::Impl::BufferAddress( int index ) const
{
	VkBufferDeviceAddressInfo address = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
	address.buffer = buffers[index].handle;
	return vkGetBufferDeviceAddress( device, &address );
}

bool CReSTIRVulkanDevice::Impl::IsPagedBinding( int binding ) const
{
	if ( receiverFullForReconstruct )
		return false;
	return binding == RESTIR_BIND_SAMPLES || binding == RESTIR_BIND_LUXELS ||
		binding == RESTIR_BIND_ACCUMULATION ||
		binding == RESTIR_BIND_OUTPUT || binding == RESTIR_BIND_LUXEL_VALID ||
		binding == RESTIR_BIND_FINAL_LIGHTMAP || binding == RESTIR_BIND_RECEIVER_ACCUMULATION ||
		binding == RESTIR_BIND_RECEIVER_OUTPUT || binding == RESTIR_BIND_SUN_ORIGINS ||
		binding == RESTIR_BIND_SUN_VISIBILITY;
}

static uint64_t PageBytes( const ReSTIRPage &page, bool shadowSplit )
{
	const uint64_t samples = (uint64_t)page.sampleCount * sizeof( ReSTIRGpuSample );
	const uint64_t luxels = (uint64_t)page.luxelCount * ( sizeof( ReSTIRGpuLuxel ) + sizeof( Vector4D ) + sizeof( float ) + sizeof( unsigned int ) );
	const uint64_t reservoirs = (uint64_t)page.reservoirCount * ( sizeof( float ) * RESTIR_MAX_CHANNELS * 4 +
		( shadowSplit ? sizeof( float ) * RESTIR_MAX_CHANNELS * 4 : 0 ) );
	const uint64_t output = (uint64_t)page.outputCount * sizeof( float ) * 4 * ( shadowSplit ? 2 : 1 );
	const uint64_t finalLightmap = (uint64_t)page.outputCount * sizeof( float ) * 4;
	return samples + luxels + reservoirs + output + finalLightmap;
}

void CReSTIRVulkanDevice::Impl::BuildPages( VkDeviceSize room )
{
	pages.RemoveAll();
	const bool shadowSplit = options.shadowMaps && scene->shadowLights.Count() != 0;
	ReSTIRPage page;
	for ( int faceIndex = 0; faceIndex < uploadedFaces.Count(); ++faceIndex )
	{
		const ReSTIRGpuFace &face = uploadedFaces[faceIndex];
		ReSTIRPage candidate = page;
		if ( !candidate.faceCount )
		{
			candidate.firstFace = faceIndex;
			candidate.firstSample = face.firstSample;
			candidate.firstLuxel = face.firstLuxel;
			candidate.firstReservoir = face.firstReservoir;
			candidate.firstOutput = face.firstOutput;
		}
		candidate.faceCount++;
		candidate.sampleCount += face.numSamples;
		candidate.luxelCount += face.luxelW * face.luxelH;
		candidate.reservoirCount += face.numSamples * face.numStyles;
		candidate.outputCount += face.numStyles * face.numChannels * face.luxelW * face.luxelH;
		if ( page.faceCount && PageBytes( candidate, shadowSplit ) > room )
		{
			pages.AddToTail( page );
			page = ReSTIRPage();
			page.firstFace = faceIndex;
			page.firstSample = face.firstSample;
			page.firstLuxel = face.firstLuxel;
			page.firstReservoir = face.firstReservoir;
			page.firstOutput = face.firstOutput;
			page.faceCount = 1;
			page.sampleCount = face.numSamples;
			page.luxelCount = face.luxelW * face.luxelH;
			page.reservoirCount = face.numSamples * face.numStyles;
			page.outputCount = face.numStyles * face.numChannels * face.luxelW * face.luxelH;
		}
		else
			page = candidate;
		if ( page.faceCount == 1 && PageBytes( page, shadowSplit ) > room )
			Fail( "GPU heap budget cannot fit one complete face page" );
	}
	if ( page.faceCount )
		pages.AddToTail( page );
	pageCount = pages.Count();
	pageSamples = pageLuxels = pageReservoirs = pageOutputs = 0;
	FOR_EACH_VEC( pages, i )
	{
		pageSamples = MAX( pageSamples, pages[i].sampleCount );
		pageLuxels = MAX( pageLuxels, pages[i].luxelCount );
		pageReservoirs = MAX( pageReservoirs, pages[i].reservoirCount );
		pageOutputs = MAX( pageOutputs, pages[i].outputCount );
	}
	if ( !pageCount )
		Fail( "scene has no complete transport pages" );
	transportPaged = pageCount > 1;
	receiverPageReservoirs = pageReservoirs;
	if ( verbose )
		Msg( "VRAD ReSTIR paging decision: pages=%u pageBytes=%llu samples=%u luxels=%u reservoirs=%u outputs=%u\n",
			pageCount, (unsigned long long)PageBytes( pages[0], shadowSplit ), pageSamples, pageLuxels, pageReservoirs, pageOutputs );
}

void CReSTIRVulkanDevice::Impl::RebuildPageBufferSizes()
{
	if ( !pageCount )
		return;
	const VkDeviceSize samples = (VkDeviceSize)pageSamples * sizeof( ReSTIRGpuSample );
	const VkDeviceSize luxels = (VkDeviceSize)pageLuxels * sizeof( ReSTIRGpuLuxel );
	const VkDeviceSize accumulation = (VkDeviceSize)pageReservoirs * RESTIR_MAX_CHANNELS * sizeof( float ) * 4;
	const VkDeviceSize output = (VkDeviceSize)pageOutputs * sizeof( float ) * 4;
	const VkDeviceSize valid = (VkDeviceSize)pageLuxels * sizeof( unsigned int );
	RecreateBuffer( RESTIR_BIND_SAMPLES, samples );
	RecreateBuffer( RESTIR_BIND_LUXELS, luxels );
	RecreateBuffer( RESTIR_BIND_ACCUMULATION, accumulation );
	RecreateBuffer( RESTIR_BIND_OUTPUT, output );
	RecreateBuffer( RESTIR_BIND_LUXEL_VALID, valid );
	RecreateBuffer( RESTIR_BIND_FINAL_LIGHTMAP, output );
	if ( options.shadowMaps && scene->shadowLights.Count() )
	{
		RecreateBuffer( RESTIR_BIND_RECEIVER_ACCUMULATION, accumulation );
		RecreateBuffer( RESTIR_BIND_RECEIVER_OUTPUT, output );
	}
	RecreateBuffer( RESTIR_BIND_SUN_ORIGINS, (VkDeviceSize)pageLuxels * sizeof( Vector4D ) );
	RecreateBuffer( RESTIR_BIND_SUN_VISIBILITY, (VkDeviceSize)pageLuxels * sizeof( float ) );
}
void CReSTIRVulkanDevice::Impl::PrepareReconstructionPages( unsigned int sampleCapacity, unsigned int reservoirCapacity, unsigned int luxelCapacity )
{
	Wait();
	for ( int i = 0; i < buffers.Count(); ++i )
		if ( !buffers[i].sceneHeap )
		{
			vkDestroyBuffer( device, buffers[i].handle, NULL );
			buffers[i].handle = VK_NULL_HANDLE;
		}
	if ( workMemory )
	{
		vkFreeMemory( device, workMemory, NULL );
		workMemory = VK_NULL_HANDLE;
	}
	receiverFullForReconstruct = true;
	for ( int i = 0; i < buffers.Count(); ++i )
	{
		if ( buffers[i].sceneHeap )
			continue;
		if ( i == RESTIR_BIND_SAMPLES )
			buffers[i].size = MAX( (VkDeviceSize)16, (VkDeviceSize)sampleCapacity * sizeof( ReSTIRGpuSample ) );
		else if ( i == RESTIR_BIND_LUXELS )
			buffers[i].size = MAX( (VkDeviceSize)16, (VkDeviceSize)luxelCapacity * sizeof( ReSTIRGpuLuxel ) );
		else if ( i == RESTIR_BIND_ACCUMULATION || ( i == RESTIR_BIND_RECEIVER_ACCUMULATION && options.shadowMaps && scene->shadowLights.Count() ) )
			buffers[i].size = MAX( (VkDeviceSize)16, (VkDeviceSize)reservoirCapacity * RESTIR_MAX_CHANNELS * sizeof( float ) * 4 );
		else if ( i == RESTIR_BIND_RECEIVER_ACCUMULATION )
			buffers[i].size = 16;
		else if ( i == RESTIR_BIND_FINAL_LIGHTMAP || i == RESTIR_BIND_SUN_ORIGINS || i == RESTIR_BIND_SUN_VISIBILITY )
			buffers[i].size = 16;
		VkBufferCreateInfo create = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		create.size = buffers[i].size;
		create.usage = buffers[i].usage;
		create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		Check( vkCreateBuffer( device, &create, NULL, &buffers[i].handle ), "vkCreateBuffer(reconstruct-page)" );
		vkGetBufferMemoryRequirements( device, buffers[i].handle, &buffers[i].requirements );
	}
	pageSamples = sampleCapacity;
	pageLuxels = luxelCapacity;
	pageReservoirs = reservoirCapacity;
	AllocateHeaps();
	RefreshDescriptorSet();
}


void CReSTIRVulkanDevice::Impl::AllocateHeaps()
{
	const bool retainScene = sceneMemory != VK_NULL_HANDLE;
	const bool retainStaging = stagingMemory != VK_NULL_HANDLE;
	// Create the host-visible transfer ring once. Reallocation between the
	// transport and reconstruction phases reuses this allocation.
	VkMemoryRequirements stagingRequirements = {};
	if ( !staging )
	{
		VkBufferCreateInfo stagingInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		stagingInfo.size = RESTIR_STAGING_BYTES;
		stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		Check( vkCreateBuffer( device, &stagingInfo, NULL, &staging ), "vkCreateBuffer(staging)" );
	}
	vkGetBufferMemoryRequirements( device, staging, &stagingRequirements );

	int preferredWorkHeap = -1;
	// Every large per-luxel/sample transport allocation is a page. Geometry,
	// the nearest-sample table and previous-irradiance state remain resident.
	if ( !receiverFullForReconstruct && buffers.Count() > RESTIR_BIND_SUN_VISIBILITY )
	{
		VkDeviceSize fixedTotals[3] = { 0, 0, stagingRequirements.size };
		unsigned int fixedBits[3] = { ~0u, ~0u, stagingRequirements.memoryTypeBits };
		FOR_EACH_VEC( buffers, i )
		{
			if ( IsPagedBinding( i ) )
				continue;
			ReSTIRBuffer &buffer = buffers[i];
			int heap = buffer.sceneHeap ? 0 : 1;
			buffer.offset = AlignSize( fixedTotals[heap], MAX( buffer.requirements.alignment, properties.limits.bufferImageGranularity ) );
			fixedTotals[heap] = buffer.offset + buffer.requirements.size;
			fixedBits[heap] &= buffer.requirements.memoryTypeBits;
		}
		FOR_EACH_VEC( images, i )
		{
			ReSTIRImage &image = images[i];
			image.offset = AlignSize( fixedTotals[0], MAX( image.requirements.alignment, properties.limits.bufferImageGranularity ) );
			fixedTotals[0] = image.offset + image.requirements.size;
			fixedBits[0] &= image.requirements.memoryTypeBits;
		}
		unsigned int fixedTypes[3] =
		{
			MemoryType( fixedBits[0], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ),
			MemoryType( fixedBits[1], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ),
			MemoryType( fixedBits[2], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT )
		};
		VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
		VkPhysicalDeviceMemoryProperties2 memory2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
		memory2.pNext = memoryBudget ? &budget : NULL;
		vkGetPhysicalDeviceMemoryProperties2( physical, &memory2 );
		const unsigned int workHeap = memoryProperties.memoryTypes[fixedTypes[1]].heapIndex;
		preferredWorkHeap = workHeap;
		VkDeviceSize available = memoryProperties.memoryHeaps[workHeap].size;
		if ( memoryBudget )
			available = budget.heapBudget[workHeap] > budget.heapUsage[workHeap] ? budget.heapBudget[workHeap] - budget.heapUsage[workHeap] : 0;
		if ( retainScene && memoryProperties.memoryTypes[fixedTypes[0]].heapIndex == workHeap )
			available += fixedTotals[0];
		if ( retainStaging && memoryProperties.memoryTypes[fixedTypes[2]].heapIndex == workHeap )
			available += fixedTotals[2];
		if ( options.vramBudgetMB > 0 )
			available = MIN( available, (VkDeviceSize)options.vramBudgetMB * 1024ull * 1024ull );
		VkDeviceSize fixedRequested = 0;
		for ( int i = 0; i < 3; ++i )
			if ( memoryProperties.memoryTypes[fixedTypes[i]].heapIndex == workHeap )
				fixedRequested += fixedTotals[i];
		ReSTIRPage fullPage;
		fullPage.sampleCount = scene->samples.Count();
		fullPage.luxelCount = scene->luxels.Count();
		fullPage.reservoirCount = fullReservoirs;
		fullPage.outputCount = scene->numOutputValues;
		const VkDeviceSize unpagedHeadroom = 128ull * 1024ull * 1024ull;
		const bool fitsUnpaged = available > unpagedHeadroom &&
			fixedRequested + PageBytes( fullPage, options.shadowMaps && scene->shadowLights.Count() ) <= available - unpagedHeadroom;
		const VkDeviceSize headroom = fitsUnpaged ? unpagedHeadroom :
			MAX( (VkDeviceSize)512ull * 1024ull * 1024ull, available / 10 );
		const VkDeviceSize usable = available > headroom ? available - headroom : 0;
		const VkDeviceSize room = usable > fixedRequested ? usable - fixedRequested : 0;
		BuildPages( room );
		RebuildPageBufferSizes();
		if ( verbose )
			Msg( "VRAD ReSTIR paging budget: resident=%llu page-room=%llu staging=%llu heap=%u\n",
				(unsigned long long)( fixedTotals[0] + fixedTotals[1] ), (unsigned long long)room,
				(unsigned long long)fixedTotals[2], workHeap );
	}

	VkDeviceSize totals[3] = { 0, 0, stagingRequirements.size };
	unsigned int typeBits[3] = { ~0u, ~0u, stagingRequirements.memoryTypeBits };
	FOR_EACH_VEC( buffers, i )
	{
		ReSTIRBuffer &buffer = buffers[i];
		int heap = buffer.sceneHeap ? 0 : 1;
		buffer.offset = AlignSize( totals[heap], MAX( buffer.requirements.alignment, properties.limits.bufferImageGranularity ) );
		totals[heap] = buffer.offset + buffer.requirements.size;
		typeBits[heap] &= buffer.requirements.memoryTypeBits;
		if ( verbose )
			Msg( "VRAD ReSTIR allocation binding=%d bytes=%llu heap=%d%s\n", i,
				(unsigned long long)buffer.requirements.size, heap,
				i == RESTIR_BIND_RECEIVER_ACCUMULATION ? " receiver" : "" );
	}
	FOR_EACH_VEC( images, i )
	{
		ReSTIRImage &image = images[i];
		image.offset = AlignSize( totals[0], MAX( image.requirements.alignment, properties.limits.bufferImageGranularity ) );
		totals[0] = image.offset + image.requirements.size;
		typeBits[0] &= image.requirements.memoryTypeBits;
	}
	unsigned int types[3];
	types[0] = MemoryType( typeBits[0], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	types[1] = MemoryType( typeBits[1], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
	if ( preferredWorkHeap >= 0 && memoryProperties.memoryTypes[types[1]].heapIndex != (unsigned int)preferredWorkHeap )
		for ( unsigned int i = 0; i < memoryProperties.memoryTypeCount; ++i )
			if ( ( typeBits[1] & ( 1u << i ) ) && memoryProperties.memoryTypes[i].heapIndex == (unsigned int)preferredWorkHeap &&
				( memoryProperties.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ) )
			{
				types[1] = i;
				break;
			}
	types[2] = MemoryType( typeBits[2], VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
	VkPhysicalDeviceMemoryBudgetPropertiesEXT budget = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
	VkPhysicalDeviceMemoryProperties2 memory2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
	memory2.pNext = memoryBudget ? &budget : NULL;
	vkGetPhysicalDeviceMemoryProperties2( physical, &memory2 );
	VkDeviceSize requested[VK_MAX_MEMORY_HEAPS] = {};
	for ( int i = 0; i < 3; ++i )
		requested[memoryProperties.memoryTypes[types[i]].heapIndex] += totals[i];
	if ( verbose )
	{
		Msg( "VRAD ReSTIR memory: scene=%llu work=%llu staging=%llu bytes\n", totals[0], totals[1], totals[2] );
		for ( unsigned int h = 0; h < memoryProperties.memoryHeapCount; ++h )
		{
			if ( !requested[h] )
				continue;
			VkDeviceSize available = memoryProperties.memoryHeaps[h].size;
			if ( memoryBudget )
				available = budget.heapBudget[h] > budget.heapUsage[h] ? budget.heapBudget[h] - budget.heapUsage[h] : 0;
			if ( retainScene && memoryProperties.memoryTypes[types[0]].heapIndex == h )
				available += totals[0];
			if ( retainStaging && memoryProperties.memoryTypes[types[2]].heapIndex == h )
				available += totals[2];
			if ( options.vramBudgetMB > 0 )
				available = MIN( available, (VkDeviceSize)options.vramBudgetMB * 1024ull * 1024ull );
			Msg( "  heap %u requested=%llu available=%llu peak=%llu bytes\n", h,
				(unsigned long long)requested[h], (unsigned long long)available, (unsigned long long)requested[h] );
		}
	}
	for ( unsigned int h = 0; h < memoryProperties.memoryHeapCount; ++h )
	{
		if ( !requested[h] )
			continue;
		VkDeviceSize available = memoryProperties.memoryHeaps[h].size;
		if ( memoryBudget )
			available = budget.heapBudget[h] > budget.heapUsage[h] ? budget.heapBudget[h] - budget.heapUsage[h] : 0;
		if ( retainScene && memoryProperties.memoryTypes[types[0]].heapIndex == h )
			available += totals[0];
		if ( retainStaging && memoryProperties.memoryTypes[types[2]].heapIndex == h )
			available += totals[2];
		if ( options.vramBudgetMB > 0 )
			available = MIN( available, (VkDeviceSize)options.vramBudgetMB * 1024ull * 1024ull );
		if ( requested[h] > available )
			Fail( "scene exceeds GPU heap budget before dispatch" );
	}
	VkDeviceMemory *allocations[3] = { &sceneMemory, &workMemory, &stagingMemory };
	VkMemoryAllocateFlagsInfo flags = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO };
	flags.flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
	for ( int i = 0; i < 3; ++i )
	{
		if ( *allocations[i] )
			continue;
		VkMemoryAllocateInfo allocation = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
		allocation.pNext = i < 2 ? &flags : NULL;
		allocation.allocationSize = totals[i];
		allocation.memoryTypeIndex = types[i];
		Check( vkAllocateMemory( device, &allocation, NULL, allocations[i] ), "vkAllocateMemory" );
	}
	FOR_EACH_VEC( buffers, i )
	{
		if ( buffers[i].sceneHeap && retainScene )
			continue;
		if ( !buffers[i].handle )
			continue;
		Check( vkBindBufferMemory( device, buffers[i].handle, buffers[i].sceneHeap ? sceneMemory : workMemory, buffers[i].offset ), "vkBindBufferMemory" );
	}
	FOR_EACH_VEC( images, i )
	{
		if ( retainScene )
			continue;
		Check( vkBindImageMemory( device, images[i].handle, sceneMemory, images[i].offset ), "vkBindImageMemory" );
	}
	if ( !retainStaging )
	{
		Check( vkBindBufferMemory( device, staging, stagingMemory, 0 ), "vkBindBufferMemory(staging)" );
		void *mapped = NULL;
		Check( vkMapMemory( device, stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped ), "vkMapMemory" );
		mappedStaging = (unsigned char *)mapped;
	}
	stagingCoherent = ( memoryProperties.memoryTypes[types[2]].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT ) != 0;
}


// The module containing this function is authoritative even in a standalone smoke executable.
static void ShaderModuleAnchor() {}

void CReSTIRVulkanDevice::Impl::RefreshDescriptorSet()
{
	unsigned int textureCount = MAX( 1, images.Count() );
	CUtlVector<VkDescriptorBufferInfo> bufferInfo;
	bufferInfo.SetCount( RESTIR_BIND_COVERAGE );
	CUtlVector<VkWriteDescriptorSet> writes;
	writes.SetCount( RESTIR_BIND_COVERAGE );
	memset( bufferInfo.Base(), 0, bufferInfo.Count() * sizeof( VkDescriptorBufferInfo ) );
	memset( writes.Base(), 0, writes.Count() * sizeof( VkWriteDescriptorSet ) );
	for ( int i = 0; i < RESTIR_BIND_COVERAGE; ++i )
	{
		bufferInfo[i].buffer = buffers[i].handle;
		bufferInfo[i].range = buffers[i].size;
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = descriptorSet;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &bufferInfo[i];
	}
	CUtlVector<VkDescriptorImageInfo> imageInfo;
	imageInfo.SetCount( textureCount );
	FOR_EACH_VEC( imageInfo, i )
	{
		imageInfo[i].sampler = textureSampler;
		imageInfo[i].imageView = images[i].view;
		imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	VkWriteDescriptorSet imageWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	imageWrite.dstSet = descriptorSet;
	imageWrite.dstBinding = RESTIR_BIND_COVERAGE;
	imageWrite.descriptorCount = textureCount;
	imageWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	imageWrite.pImageInfo = imageInfo.Base();
	writes.AddToTail( imageWrite );
	VkWriteDescriptorSetAccelerationStructureKHR accelerationWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
	VkWriteDescriptorSet accelerationSet = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
	if ( hardware )
	{
		accelerationWrite.accelerationStructureCount = 1;
		accelerationWrite.pAccelerationStructures = &tlas.handle;
		accelerationSet.pNext = &accelerationWrite;
		accelerationSet.dstSet = descriptorSet;
		accelerationSet.dstBinding = RESTIR_BIND_TLAS;
		accelerationSet.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		accelerationSet.descriptorCount = 1;
		writes.AddToTail( accelerationSet );
	}
	vkUpdateDescriptorSets( device, writes.Count(), writes.Base(), 0, NULL );
}
void CReSTIRVulkanDevice::Impl::ReallocateForReconstruct()
{
	Wait();
	receiverFullForReconstruct = true;
	const VkDeviceSize finalBytes = ( (VkDeviceSize)scene->numOutputValues +
		(VkDeviceSize)scene->faces.Count() * RESTIR_MAX_FACE_STYLES ) * sizeof( float ) * 4;
	for ( int i = 0; i < buffers.Count(); ++i )
	{
		if ( buffers[i].sceneHeap )
			continue;
		vkDestroyBuffer( device, buffers[i].handle, NULL );
		buffers[i].handle = VK_NULL_HANDLE;
		if ( i == RESTIR_BIND_FINAL_LIGHTMAP )
			buffers[i].size = finalBytes;
		else if ( i == RESTIR_BIND_LUXEL_VALID )
			buffers[i].size = (VkDeviceSize)scene->luxels.Count() * sizeof( unsigned int );
		else if ( i == RESTIR_BIND_SERVICE_IN || i == RESTIR_BIND_SERVICE_OUT )
			buffers[i].size = RESTIR_STAGING_BYTES;
		else
			buffers[i].size = 16;
		VkBufferCreateInfo create = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
		create.size = buffers[i].size;
		create.usage = buffers[i].usage;
		create.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		Check( vkCreateBuffer( device, &create, NULL, &buffers[i].handle ), "vkCreateBuffer(reconstruct)" );
		vkGetBufferMemoryRequirements( device, buffers[i].handle, &buffers[i].requirements );
	}
	if ( workMemory )
	{
		vkFreeMemory( device, workMemory, NULL );
		workMemory = VK_NULL_HANDLE;
	}
	receiverPageReservoirs = fullReservoirs;
	AllocateHeaps();
	RefreshDescriptorSet();
	if ( verbose )
		Msg( "VRAD ReSTIR paging: final-lightmap heap rebound after transport pages were released\n" );
	push.flags = hardware ? RESTIR_PC_HARDWARE_RT : 0;
	memset( push.pageBase, 0, sizeof( push.pageBase ) );
}


void CReSTIRVulkanDevice::Impl::CreateDescriptorsAndPipelines()
{
	unsigned int textureCount = MAX( 1, images.Count() );
	COMPILE_TIME_ASSERT( RESTIR_BIND_TLAS == RESTIR_BIND_COVERAGE + 1 );
	COMPILE_TIME_ASSERT( RESTIR_BIND_COUNT == RESTIR_BIND_TLAS + 1 );
	VkDescriptorSetLayoutBinding bindings[RESTIR_BIND_COUNT] = {};
	VkDescriptorBindingFlags bindingFlags[RESTIR_BIND_COUNT] = {};
	unsigned int bindingCount = hardware ? RESTIR_BIND_COUNT : RESTIR_BIND_COUNT - 1;
	for ( unsigned int i = 0; i < bindingCount; ++i )
	{
		bindings[i].binding = i;
		bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[i].descriptorCount = 1;
		bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
	}
	bindings[RESTIR_BIND_COVERAGE].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[RESTIR_BIND_COVERAGE].descriptorCount = textureCount;
	// Fixed per-scene count: VARIABLE_DESCRIPTOR_COUNT cannot precede RESTIR_BIND_TLAS.
	bindingFlags[RESTIR_BIND_COVERAGE] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
	if ( hardware )
		bindings[RESTIR_BIND_TLAS].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
	VkDescriptorSetLayoutBindingFlagsCreateInfo flags = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	flags.bindingCount = bindingCount;
	flags.pBindingFlags = bindingFlags;
	VkDescriptorSetLayoutCreateInfo layoutInfo = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	layoutInfo.pNext = &flags;
	layoutInfo.bindingCount = bindingCount;
	layoutInfo.pBindings = bindings;
	Check( vkCreateDescriptorSetLayout( device, &layoutInfo, NULL, &setLayout ), "vkCreateDescriptorSetLayout" );
	VkDescriptorPoolSize sizes[3] =
	{
		{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, RESTIR_BIND_COVERAGE },
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, textureCount },
		{ VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1 }
	};
	VkDescriptorPoolCreateInfo pool = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
	pool.maxSets = 1;
	pool.poolSizeCount = hardware ? 3 : 2;
	pool.pPoolSizes = sizes;
	Check( vkCreateDescriptorPool( device, &pool, NULL, &descriptorPool ), "vkCreateDescriptorPool" );
	VkDescriptorSetAllocateInfo allocation = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocation.descriptorPool = descriptorPool;
	allocation.descriptorSetCount = 1;
	allocation.pSetLayouts = &setLayout;
	Check( vkAllocateDescriptorSets( device, &allocation, &descriptorSet ), "vkAllocateDescriptorSets" );
	VkDescriptorBufferInfo bufferInfo[RESTIR_BIND_COVERAGE] = {};
	VkWriteDescriptorSet writes[RESTIR_BIND_COUNT] = {};
	for ( unsigned int i = 0; i < RESTIR_BIND_COVERAGE; ++i )
	{
		bufferInfo[i].buffer = buffers[i].handle;
		bufferInfo[i].range = buffers[i].size;
		writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[i].dstSet = descriptorSet;
		writes[i].dstBinding = i;
		writes[i].descriptorCount = 1;
		writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[i].pBufferInfo = &bufferInfo[i];
	}
	CUtlVector<VkDescriptorImageInfo> imageInfo;
	imageInfo.SetCount( textureCount );
	FOR_EACH_VEC( imageInfo, i )
	{
		imageInfo[i].sampler = textureSampler;
		imageInfo[i].imageView = images[i].view;
		imageInfo[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	}
	writes[RESTIR_BIND_COVERAGE].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	writes[RESTIR_BIND_COVERAGE].dstSet = descriptorSet;
	writes[RESTIR_BIND_COVERAGE].dstBinding = RESTIR_BIND_COVERAGE;
	writes[RESTIR_BIND_COVERAGE].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	writes[RESTIR_BIND_COVERAGE].descriptorCount = textureCount;
	writes[RESTIR_BIND_COVERAGE].pImageInfo = imageInfo.Base();
	VkWriteDescriptorSetAccelerationStructureKHR accelerationWrite = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
	if ( hardware )
	{
		accelerationWrite.accelerationStructureCount = 1;
		accelerationWrite.pAccelerationStructures = &tlas.handle;
		writes[RESTIR_BIND_TLAS].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[RESTIR_BIND_TLAS].pNext = &accelerationWrite;
		writes[RESTIR_BIND_TLAS].dstSet = descriptorSet;
		writes[RESTIR_BIND_TLAS].dstBinding = RESTIR_BIND_TLAS;
		writes[RESTIR_BIND_TLAS].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
		writes[RESTIR_BIND_TLAS].descriptorCount = 1;
	}
	vkUpdateDescriptorSets( device, bindingCount, writes, 0, NULL );
	VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( ReSTIRPushConstants ) };
	COMPILE_TIME_ASSERT( sizeof( ReSTIRPushConstants ) == 144 );
	VkPipelineLayoutCreateInfo pipelineInfo = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	pipelineInfo.setLayoutCount = 1;
	pipelineInfo.pSetLayouts = &setLayout;
	pipelineInfo.pushConstantRangeCount = 1;
	pipelineInfo.pPushConstantRanges = &range;
	Check( vkCreatePipelineLayout( device, &pipelineInfo, NULL, &pipelineLayout ), "vkCreatePipelineLayout" );
	HMODULE module = NULL;
	if ( !GetModuleHandleExA( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&ShaderModuleAnchor, &module ) )
		Fail( "cannot locate shader module directory" );
	char modulePath[32768];
	DWORD pathLength = GetModuleFileNameA( module, modulePath, sizeof( modulePath ) );
	if ( !pathLength || pathLength >= sizeof( modulePath ) )
		Fail( "shader module path is unavailable or too long" );
	char *separator = strrchr( modulePath, '\\' );
	if ( !separator )
		Fail( "shader module directory is unavailable" );
	*separator = '\0';
	for ( int i = hardware ? RESTIR_PIPE_INIT : RESTIR_PIPE_MORTON; i < RESTIR_PIPE_COUNT; ++i )
	{
		char shaderPath[32768];
		V_snprintf( shaderPath, sizeof( shaderPath ), "%s\\vrad_restir_shaders\\%s_%s.spv", modulePath, s_pPipelineNames[i], hardware ? "hw" : "sw" );
		FILE *file = fopen( shaderPath, "rb" );
		if ( !file )
			Error( "VRAD ReSTIR: %s: missing shader %s (VkResult=%d)\n", options.mapPath.String(), shaderPath, VK_ERROR_INITIALIZATION_FAILED );
		fseek( file, 0, SEEK_END );
		long bytes = ftell( file );
		if ( bytes < 20 || ( bytes & 3 ) )
			Fail( "invalid SPIR-V module size" );
		rewind( file );
		CUtlVector<unsigned int> code;
		code.SetCount( bytes / 4 );
		if ( fread( code.Base(), 1, bytes, file ) != (size_t)bytes )
			Fail( "cannot read SPIR-V module" );
		fclose( file );
		VkShaderModuleCreateInfo shaderInfo = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		shaderInfo.codeSize = bytes;
		shaderInfo.pCode = code.Base();
		VkShaderModule shader;
		Check( vkCreateShaderModule( device, &shaderInfo, NULL, &shader ), "vkCreateShaderModule" );
		VkComputePipelineCreateInfo compute = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
		compute.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		compute.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		compute.stage.module = shader;
		compute.stage.pName = "main";
		compute.layout = pipelineLayout;
		Check( vkCreateComputePipelines( device, VK_NULL_HANDLE, 1, &compute, NULL, &pipelines[i] ), "vkCreateComputePipelines" );
		vkDestroyShaderModule( device, shader, NULL );
	}
}

VkCommandBuffer CReSTIRVulkanDevice::Impl::BeginCommands()
{
	VkCommandBufferAllocateInfo allocation = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
	allocation.commandPool = commandPool;
	allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocation.commandBufferCount = 1;
	VkCommandBuffer command;
	Check( vkAllocateCommandBuffers( device, &allocation, &command ), "vkAllocateCommandBuffers" );
	VkCommandBufferBeginInfo begin = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	Check( vkBeginCommandBuffer( command, &begin ), "vkBeginCommandBuffer" );
	return command;
}

void CReSTIRVulkanDevice::Impl::Submit( VkCommandBuffer command )
{
	Check( vkEndCommandBuffer( command ), "vkEndCommandBuffer" );
	++submittedValue;
	VkTimelineSemaphoreSubmitInfo timelineInfo = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO };
	timelineInfo.signalSemaphoreValueCount = 1;
	timelineInfo.pSignalSemaphoreValues = &submittedValue;
	VkSubmitInfo submit = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submit.pNext = &timelineInfo;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &command;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &timeline;
	Check( vkQueueSubmit( queue, 1, &submit, VK_NULL_HANDLE ), "vkQueueSubmit" );
	pendingCommands.AddToTail( command );
}

void CReSTIRVulkanDevice::Impl::Wait()
{
	if ( !pendingCommands.Count() )
		return;
	VkSemaphoreWaitInfo wait = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO };
	wait.semaphoreCount = 1;
	wait.pSemaphores = &timeline;
	wait.pValues = &submittedValue;
	Check( vkWaitSemaphores( device, &wait, UINT64_MAX ), "vkWaitSemaphores" );
	vkFreeCommandBuffers( device, commandPool, pendingCommands.Count(), pendingCommands.Base() );
	pendingCommands.RemoveAll();
}

void CReSTIRVulkanDevice::Impl::Barrier( VkCommandBuffer command ) const
{
	VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
	barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
	vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
}

unsigned int CReSTIRVulkanDevice::Impl::DispatchItemLimit() const
{
	uint64_t limit = (uint64_t)properties.limits.maxComputeWorkGroupCount[0] * RESTIR_WORKGROUP_SIZE;
	return (unsigned int)MIN( limit, (uint64_t)UINT_MAX - ( RESTIR_WORKGROUP_SIZE - 1 ) );
}

void CReSTIRVulkanDevice::Impl::DispatchRange( VkCommandBuffer command, ReSTIRPipeline pipeline,
	unsigned int first, unsigned int count, unsigned int pass )
{
	if ( !count )
		return;
	vkCmdBindPipeline( command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelines[pipeline] );
	vkCmdBindDescriptorSets( command, VK_PIPELINE_BIND_POINT_COMPUTE, pipelineLayout, 0, 1, &descriptorSet, 0, NULL );
	const uint64_t limit = DispatchItemLimit();
	while ( count )
	{
		push.first = first;
		push.count = (unsigned int)MIN( (uint64_t)count, limit );
		push.pass = pass;
		vkCmdPushConstants( command, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof( push ), &push );
		vkCmdDispatch( command, ( push.count + RESTIR_WORKGROUP_SIZE - 1 ) / RESTIR_WORKGROUP_SIZE, 1, 1 );
		first += push.count;
		count -= push.count;
	}
}

void CReSTIRVulkanDevice::Impl::Dispatch( VkCommandBuffer command, ReSTIRPipeline pipeline, unsigned int count, unsigned int pass )
{
	DispatchRange( command, pipeline, 0, count, pass );
}

void CReSTIRVulkanDevice::Impl::Upload( int buffer, const void *data, VkDeviceSize bytes, VkDeviceSize offset )
{
	if ( bytes + offset > buffers[buffer].size )
		Fail( "buffer upload exceeds allocation" );
	for ( VkDeviceSize done = 0; done < bytes; )
	{
		Wait();
		VkDeviceSize chunk = MIN( bytes - done, RESTIR_STAGING_BYTES );
		memcpy( mappedStaging, (const unsigned char *)data + done, (size_t)chunk );
		if ( !stagingCoherent )
		{
			VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
			range.memory = stagingMemory;
			range.size = VK_WHOLE_SIZE;
			Check( vkFlushMappedMemoryRanges( device, 1, &range ), "vkFlushMappedMemoryRanges" );
		}
		VkCommandBuffer command = BeginCommands();
		VkBufferCopy copy = { 0, offset + done, chunk };
		vkCmdCopyBuffer( command, staging, buffers[buffer].handle, 1, &copy );
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
		VkPipelineStageFlags destination = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
		if ( hardware )
		{
			barrier.dstAccessMask |= VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
			destination |= VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
		}
		vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TRANSFER_BIT, destination, 0, 1, &barrier, 0, NULL, 0, NULL );
		Submit( command );
		done += chunk;
	}
}

void CReSTIRVulkanDevice::Impl::Download( int buffer, void *data, VkDeviceSize bytes, VkDeviceSize offset, double *pTransferMs )
{
	if ( bytes + offset > buffers[buffer].size )
		Fail( "buffer readback exceeds allocation" );
	for ( VkDeviceSize done = 0; done < bytes; )
	{
		if ( pTransferMs )
			ResetQueries();
		VkDeviceSize chunk = MIN( bytes - done, RESTIR_STAGING_BYTES );
		VkCommandBuffer command = BeginCommands();
		VkMemoryBarrier barrier = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
		barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
		unsigned int transferStart = pTransferMs ? Timestamp( command ) : 0;
		VkBufferCopy copy = { offset + done, 0, chunk };
		vkCmdCopyBuffer( command, buffers[buffer].handle, staging, 1, &copy );
		barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
		vkCmdPipelineBarrier( command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, NULL, 0, NULL );
		unsigned int transferEnd = pTransferMs ? Timestamp( command ) : 0;
		Submit( command );
		Wait();
		if ( pTransferMs )
			*pTransferMs += TimestampMs( transferStart, transferEnd );
		if ( !stagingCoherent )
		{
			VkMappedMemoryRange range = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE };
			range.memory = stagingMemory;
			range.size = VK_WHOLE_SIZE;
			Check( vkInvalidateMappedMemoryRanges( device, 1, &range ), "vkInvalidateMappedMemoryRanges" );
		}
		memcpy( (unsigned char *)data + done, mappedStaging, (size_t)chunk );
		done += chunk;
	}
}

void CReSTIRVulkanDevice::Impl::ResetQueries()
{
	Wait();
	vkResetQueryPool( device, queryPool, 0, 64 );
	queryCursor = 0;
}

unsigned int CReSTIRVulkanDevice::Impl::Timestamp( VkCommandBuffer command )
{
	if ( queryCursor >= 64 )
		Fail( "timestamp query capacity exceeded" );
	unsigned int index = queryCursor++;
	vkCmdWriteTimestamp( command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, index );
	return index;
}

double CReSTIRVulkanDevice::Impl::TimestampMs( unsigned int first, unsigned int last )
{
	uint64_t values[2];
	Check( vkGetQueryPoolResults( device, queryPool, first, 1, sizeof( uint64_t ), &values[0], sizeof( uint64_t ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT ), "vkGetQueryPoolResults" );
	Check( vkGetQueryPoolResults( device, queryPool, last, 1, sizeof( uint64_t ), &values[1], sizeof( uint64_t ), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT ), "vkGetQueryPoolResults" );
	uint64_t mask = timestampBits == 64 ? UINT64_MAX : ( 1ull << timestampBits ) - 1;
	return ( ( values[1] - values[0] ) & mask ) * (double)properties.limits.timestampPeriod / 1000000.0;
}

const ReSTIRDeviceInfo &CReSTIRVulkanDevice::GetDeviceInfo() const
{
	return m_pImpl->info;
}

const ReSTIRGpuTimings &CReSTIRVulkanDevice::GetTimings() const
{
	return m_pImpl->timings;
}

void CReSTIRVulkanDevice::Impl::Release()
{
	Impl &gpu = *this;
	if ( gpu.device )
	{
		gpu.Wait();
		FOR_EACH_VEC( gpu.blas, i )
			if ( gpu.blas[i].handle )
				gpu.destroyAcceleration( gpu.device, gpu.blas[i].handle, NULL );
		if ( gpu.tlas.handle )
			gpu.destroyAcceleration( gpu.device, gpu.tlas.handle, NULL );
		for ( int i = 0; i < RESTIR_PIPE_COUNT; ++i )
			if ( gpu.pipelines[i] )
				vkDestroyPipeline( gpu.device, gpu.pipelines[i], NULL );
		if ( gpu.pipelineLayout )
			vkDestroyPipelineLayout( gpu.device, gpu.pipelineLayout, NULL );
		if ( gpu.descriptorPool )
			vkDestroyDescriptorPool( gpu.device, gpu.descriptorPool, NULL );
		if ( gpu.setLayout )
			vkDestroyDescriptorSetLayout( gpu.device, gpu.setLayout, NULL );
		FOR_EACH_VEC( gpu.images, i )
		{
			if ( gpu.images[i].view )
				vkDestroyImageView( gpu.device, gpu.images[i].view, NULL );
			vkDestroyImage( gpu.device, gpu.images[i].handle, NULL );
		}
		if ( gpu.textureSampler )
			vkDestroySampler( gpu.device, gpu.textureSampler, NULL );
		FOR_EACH_VEC( gpu.buffers, i )
			vkDestroyBuffer( gpu.device, gpu.buffers[i].handle, NULL );
		if ( gpu.mappedStaging )
			vkUnmapMemory( gpu.device, gpu.stagingMemory );
		if ( gpu.staging )
			vkDestroyBuffer( gpu.device, gpu.staging, NULL );
		if ( gpu.sceneMemory )
			vkFreeMemory( gpu.device, gpu.sceneMemory, NULL );
		if ( gpu.workMemory )
			vkFreeMemory( gpu.device, gpu.workMemory, NULL );
		if ( gpu.stagingMemory )
			vkFreeMemory( gpu.device, gpu.stagingMemory, NULL );
		if ( gpu.queryPool )
			vkDestroyQueryPool( gpu.device, gpu.queryPool, NULL );
		if ( gpu.timeline )
			vkDestroySemaphore( gpu.device, gpu.timeline, NULL );
		if ( gpu.commandPool )
			vkDestroyCommandPool( gpu.device, gpu.commandPool, NULL );
		vkDestroyDevice( gpu.device, NULL );
	}
	if ( gpu.instance )
		vkDestroyInstance( gpu.instance, NULL );
	gpu.device = VK_NULL_HANDLE;
	gpu.instance = VK_NULL_HANDLE;
}

void CReSTIRVulkanDevice::Shutdown()
{
	m_pImpl->Release();
	delete m_pImpl;
	m_pImpl = new Impl;
}
