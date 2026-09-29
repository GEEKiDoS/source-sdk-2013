#include "shaderapi_dx12.h"
#include "shaderdevice_dx12.h"
#include "hardwareconfig_dx12.h"
#include "shadershadow_dx12.h"
#include "vballoctracker_dx12.h"
#include "tier1/interface.h"

namespace shaderapidx12
{
static CShaderDeviceMgrDX12 s_deviceManager;
static CShaderDeviceDX12 &s_device = *s_deviceManager.Device();
static CHardwareConfigDX12 s_hardwareConfig;
static CShaderAPIDX12 s_shaderAPI;
static CShaderShadowDX12 s_shaderShadow;
static CVBAllocTrackerDX12 s_vbTracker;

} // namespace shaderapidx12

using namespace shaderapidx12;

EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderDeviceMgrDX12, IShaderDeviceMgr, SHADER_DEVICE_MGR_INTERFACE_VERSION, s_deviceManager);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderDeviceDX12, IShaderDevice, SHADER_DEVICE_INTERFACE_VERSION, s_device);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIDX12, IShaderAPI, SHADERAPI_INTERFACE_VERSION, s_shaderAPI);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIDX12, IShaderDynamicAPI, SHADERDYNAMIC_INTERFACE_VERSION, s_shaderAPI);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderShadowDX12, IShaderShadow, SHADERSHADOW_INTERFACE_VERSION, s_shaderShadow);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CHardwareConfigDX12, IMaterialSystemHardwareConfig, MATERIALSYSTEM_HARDWARECONFIG_INTERFACE_VERSION, s_hardwareConfig);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CVBAllocTrackerDX12, IVBAllocTracker, VB_ALLOC_TRACKER_INTERFACE_VERSION, s_vbTracker);
EXPOSE_SINGLE_INTERFACE_GLOBALVAR(CShaderAPIDX12, IDebugTextureInfo, DEBUG_TEXTURE_INFO_VERSION, s_shaderAPI);

namespace shaderapidx12
{

struct Initializer
{
    Initializer()
    {
        g_pShaderDeviceMgrDX12 = &s_deviceManager;
        g_pHardwareConfigDX12 = &s_hardwareConfig;
        g_pShaderAPIDX12 = &s_shaderAPI;
        g_pShaderShadowDX12 = &s_shaderShadow;
    }
};
static Initializer s_initializer;
} // namespace shaderapidx12
