#include "hardwareconfig_dx12.h"
#include "tier1/convar.h"
#include <algorithm>
#include <climits>
#include <cstring>

namespace shaderapidx12
{

CHardwareConfigDX12 *g_pHardwareConfigDX12 = nullptr;

CHardwareConfigDX12::CHardwareConfigDX12()
{
    std::memset(&adapter_, 0, sizeof(adapter_));
    std::strncpy(adapter_.m_pDriverName, "Native DirectX 12", sizeof(adapter_.m_pDriverName) - 1);
    adapter_.m_nDXSupportLevel = 95;
    adapter_.m_nMaxDXSupportLevel = 95;
}

void CHardwareConfigDX12::SetAdapter(const MaterialAdapterInfo_t &adapter, uint64_t dedicatedVideoMemory, bool aaEnabled, int samples)
{
    adapter_ = adapter;
    dedicatedVideoMemory_ = dedicatedVideoMemory;
    aaEnabled_ = aaEnabled;
    samples_ = std::max(1, samples);
    adapter_.m_nDXSupportLevel = std::min(adapter_.m_nDXSupportLevel ? adapter_.m_nDXSupportLevel : 95, 95);
    adapter_.m_nMaxDXSupportLevel = 95;
    // Source selects float HDR only for the explicit mat_hdr_level 3 mode.
    ConVarRef hdrLevel("mat_hdr_level",true);
    hdrType_ = hdrLevel.IsValid() && hdrLevel.GetInt() == 3 ? HDR_TYPE_FLOAT : HDR_TYPE_INTEGER;
}


void CHardwareConfigDX12::SetSupportCaps(bool fastClipping, bool centroidHack, bool disableShaderOptimizations)
{
    fastClipping_ = fastClipping;
    centroidHack_ = centroidHack;
    disableShaderOptimizations_ = disableShaderOptimizations;
}

void CHardwareConfigDX12::SetDXSupportLevels(int recommended, int maximum)
{
    maxDXLevel_ = std::max(90, std::min(maximum, 95));
    dxLevel_ = std::max(90, std::min(recommended, maxDXLevel_));
}
void CHardwareConfigDX12::SetDXLevel(int level)
{
    dxLevel_ = std::max(90, std::min(level ? level : maxDXLevel_, maxDXLevel_));
}

int CHardwareConfigDX12::TextureMemorySize() const
{
    return dedicatedVideoMemory_ > static_cast<uint64_t>(INT_MAX) ? INT_MAX : static_cast<int>(dedicatedVideoMemory_);
}

} // namespace shaderapidx12
