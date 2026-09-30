#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) depthwrite_vs20 {
    float g_vTreeSwayParams0[4]{};
    float g_vTreeSwayParams1[4]{};
    float g_vTreeSwayParams2[4]{};
    float g_vTreeSwayParams3[4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    float g_vTreeSwayParams4[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 112;
    static constexpr uint64_t kLayoutHash = 0x41794b1930659651ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 50},
            {16, 16, 1, 0, 0, 0, 51},
            {32, 16, 1, 0, 0, 0, 52},
            {48, 16, 1, 0, 0, 0, 53},
            {64, 12, 1, 0, 0, 0, 54},
            {80, 16, 1, 0, 0, 0, 55},
            {96, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(depthwrite_vs20, g_vTreeSwayParams0) == 0, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, g_vTreeSwayParams1) == 16, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, g_vTreeSwayParams2) == 32, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, g_vTreeSwayParams3) == 48, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, cMorphTargetTextureDim) == 64, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, cMorphSubrect) == 80, "HLSL member offset");
static_assert(offsetof(depthwrite_vs20, g_vTreeSwayParams4) == 96, "HLSL member offset");
static_assert(sizeof(depthwrite_vs20) == 112, "HLSL cbuffer size");
} // namespace dx12cb
