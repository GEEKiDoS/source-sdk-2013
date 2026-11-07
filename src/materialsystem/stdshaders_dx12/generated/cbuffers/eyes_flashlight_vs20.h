#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) eyes_flashlight_vs20 {
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    float cLightPosition[4]{};
    float cSpotlightProj1[4]{};
    float cSpotlightProj2[4]{};
    float cSpotlightProj3[4]{};
    float cSpotlightProj4[4]{};
    float cFlashlighAtten[4]{};
    float cIrisProjectionU[4]{};
    float cIrisProjectionV[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0x57d48b4a17bbd58aull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 14},
            {16, 16, 1, 0, 0, 0, 15},
            {32, 16, 1, 0, 0, 0, 48},
            {48, 16, 1, 0, 0, 0, 49},
            {64, 16, 1, 0, 0, 0, 50},
            {80, 16, 1, 0, 0, 0, 51},
            {96, 16, 1, 0, 0, 0, 52},
            {112, 16, 1, 0, 0, 0, 53},
            {128, 16, 1, 0, 0, 0, 56},
            {144, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 10, entries };
        return map;
    }
};
static_assert(offsetof(eyes_flashlight_vs20, cMorphTargetTextureDim) == 0, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cMorphSubrect) == 16, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cLightPosition) == 32, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cSpotlightProj1) == 48, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cSpotlightProj2) == 64, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cSpotlightProj3) == 80, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cSpotlightProj4) == 96, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cFlashlighAtten) == 112, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cIrisProjectionU) == 128, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_vs20, cIrisProjectionV) == 144, "HLSL member offset");
static_assert(sizeof(eyes_flashlight_vs20) == 160, "HLSL cbuffer size");
} // namespace dx12cb
