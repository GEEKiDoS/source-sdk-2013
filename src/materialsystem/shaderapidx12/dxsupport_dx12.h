//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once
#include <cstdint>
class IFileSystem;
class KeyValues;
namespace shaderapidx12
{
struct DXSupportCapsDX12
{
    int recommended = 95, max = 95;
    unsigned vendor = 0, device = 0;
    uint64_t memory = 0; // Dedicated video memory in bytes.
    bool fastClipping = false, centroidHack = false, disableShaderOptimizations = false;
};
class CDXSupportDX12
{
public:
    CDXSupportDX12() = default;
    ~CDXSupportDX12();
    CDXSupportDX12(const CDXSupportDX12 &) = delete;
    CDXSupportDX12 &operator=(const CDXSupportDX12 &) = delete;
    // Missing files are valid. Malformed input discards all overrides and logs.
    bool Load(IFileSystem *filesystem);
    void Clear();
    void ReadDXSupportLevels(DXSupportCapsDX12 &caps) const;
    void ReadHardwareCaps(DXSupportCapsDX12 &caps, int dxLevel) const;
    bool GetRecommendedConfigurationInfo(const DXSupportCapsDX12 &caps, int dxLevel, KeyValues *configuration) const;
    bool GetRecommendedConfigurationInfo(const DXSupportCapsDX12 &caps, int dxLevel, unsigned vendor, unsigned device, KeyValues *configuration) const;
private:
    KeyValues *config_ = nullptr;
};
} // namespace shaderapidx12
