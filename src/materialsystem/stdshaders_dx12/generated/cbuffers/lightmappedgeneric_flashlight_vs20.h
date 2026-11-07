#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_flashlight_vs20 {
    float g_FlashlightPos[3]{};
    uint8_t _pad0[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float SeamlessScale[4]{};
    float cBaseTexCoordTransform[2][4]{};
    float cNormalMapOrDetailTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0xa1c25b657dc724c2ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 48},
            {16, 16, 4, 16, 0, 0, 49},
            {80, 16, 1, 0, 0, 0, 53},
            {96, 16, 1, 0, 0, 0, 54},
            {112, 16, 2, 16, 0, 0, 54},
            {144, 16, 2, 16, 0, 0, 56},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, g_FlashlightPos) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, g_FlashlightWorldToTexture) == 16, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, g_FlashlightAttenuationFactors) == 80, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, SeamlessScale) == 96, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, cBaseTexCoordTransform) == 112, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_flashlight_vs20, cNormalMapOrDetailTexCoordTransform) == 144, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_flashlight_vs20) == 176, "HLSL cbuffer size");
} // namespace dx12cb
