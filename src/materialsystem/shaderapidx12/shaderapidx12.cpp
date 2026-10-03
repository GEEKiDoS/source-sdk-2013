//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Module singletons and the interfaces shaderapidx12 exposes to the material system.
//
//=============================================================================//

#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "hardwareconfig_dx12.h"
#include "shadershadow_dx12.h"
#include "vballoctracker_dx12.h"
#include "tier1/interface.h"

namespace shaderapidx12
{
static CShaderDeviceMgrDX12 s_DeviceManager;
static CShaderDeviceDX12 &s_Device = *s_DeviceManager.Device();
static CHardwareConfigDX12 s_HardwareConfig;
static CShaderAPIDX12 s_ShaderAPI;
static CShaderShadowDX12 s_ShaderShadow;
static CVBAllocTrackerDX12 s_VBTracker;

} // namespace shaderapidx12

using namespace shaderapidx12;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceMgrDX12, IShaderDeviceMgr, SHADER_DEVICE_MGR_INTERFACE_VERSION, s_DeviceManager );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderDeviceDX12, IShaderDevice, SHADER_DEVICE_INTERFACE_VERSION, s_Device );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDX12, IShaderAPI, SHADERAPI_INTERFACE_VERSION, s_ShaderAPI );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDX12, IShaderDynamicAPI, SHADERDYNAMIC_INTERFACE_VERSION, s_ShaderAPI );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderShadowDX12, IShaderShadow, SHADERSHADOW_INTERFACE_VERSION, s_ShaderShadow );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHardwareConfigDX12, IMaterialSystemHardwareConfig, MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION, s_HardwareConfig );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CVBAllocTrackerDX12, IVBAllocTracker, VB_ALLOC_TRACKER_INTERFACE_VERSION, s_VBTracker );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDX12, IDebugTextureInfo, DEBUG_TEXTURE_INFO_VERSION, s_ShaderAPI );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDX12, IShaderAPIDX12, SHADERAPIDX12_INTERFACE_VERSION, s_ShaderAPI );
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CShaderAPIDX12, IShaderAPIDX12Compute, SHADERAPIDX12_COMPUTE_INTERFACE_VERSION, s_ShaderAPI );

namespace shaderapidx12
{

//-----------------------------------------------------------------------------
// Purpose: Publishes the module singletons through the g_p* globals at static-init time
//-----------------------------------------------------------------------------
struct Initializer
{
	Initializer()
	{
		g_pShaderDeviceMgrDX12 = &s_DeviceManager;
		g_pHardwareConfigDX12 = &s_HardwareConfig;
		g_pShaderAPIDX12 = &s_ShaderAPI;
		g_pShaderShadowDX12 = &s_ShaderShadow;
	}
};

static Initializer s_Initializer;
} // namespace shaderapidx12
