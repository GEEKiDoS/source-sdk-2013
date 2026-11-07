#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) teeth_flashlight_vs20 {
    float cFlashlightPosition[4]{};
    float cSpotlightProj1[4]{};
    float cSpotlightProj2[4]{};
    float cSpotlightProj3[4]{};
    float cSpotlightProj4[4]{};
    float cFlashlighAtten[4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    float cTeethLighting[4]{};
    float const4[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0xdf06d40030c0fb57ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 1, 0, 0, 0, 49},
            {32, 16, 1, 0, 0, 0, 50},
            {48, 16, 1, 0, 0, 0, 51},
            {64, 16, 1, 0, 0, 0, 52},
            {80, 16, 1, 0, 0, 0, 53},
            {96, 12, 1, 0, 0, 0, 54},
            {112, 16, 1, 0, 0, 0, 55},
            {128, 16, 1, 0, 0, 0, 56},
            {144, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 10, entries };
        return map;
    }
};
static_assert(offsetof(teeth_flashlight_vs20, cFlashlightPosition) == 0, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cSpotlightProj1) == 16, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cSpotlightProj2) == 32, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cSpotlightProj3) == 48, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cSpotlightProj4) == 64, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cFlashlighAtten) == 80, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cMorphTargetTextureDim) == 96, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cMorphSubrect) == 112, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, cTeethLighting) == 128, "HLSL member offset");
static_assert(offsetof(teeth_flashlight_vs20, const4) == 144, "HLSL member offset");
static_assert(sizeof(teeth_flashlight_vs20) == 160, "HLSL cbuffer size");
} // namespace dx12cb
