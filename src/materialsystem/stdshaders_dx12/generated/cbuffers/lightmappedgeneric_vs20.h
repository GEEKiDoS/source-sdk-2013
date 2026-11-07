#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_vs20 {
    float cBlendMaskTexCoordTransform[2][4]{};
    float cModulationColor[4]{};
    float SeamlessScale[4]{};
    float cBaseTexCoordTransform[2][4]{};
    float cDetailOrBumpTexCoordTransform[2][4]{};
    float cEnvmapMaskTexCoordTransform[2][4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 224;
    static constexpr uint64_t kLayoutHash = 0x868fcd6bc250aa8dull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 14},
            {32, 16, 1, 0, 0, 0, 47},
            {48, 16, 1, 0, 0, 0, 48},
            {64, 16, 2, 16, 0, 0, 48},
            {96, 16, 2, 16, 0, 0, 50},
            {128, 16, 2, 16, 0, 0, 52},
            {160, 16, 4, 16, 0, 0, 54},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_vs20, cBlendMaskTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, cModulationColor) == 32, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, SeamlessScale) == 48, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, cBaseTexCoordTransform) == 64, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, cDetailOrBumpTexCoordTransform) == 96, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, cEnvmapMaskTexCoordTransform) == 128, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_vs20, g_FlashlightWorldToTexture) == 160, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_vs20) == 224, "HLSL cbuffer size");
} // namespace dx12cb
