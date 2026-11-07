#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) shadowmodel_vs20 {
    float cModulationColor[4]{};
    float cShadowTextureMatrix[3][4]{};
    float cTexOrigin[4]{};
    float cTexScale[4]{};
    float cShadowConstants[3]{};
    uint8_t _pad0[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 112;
    static constexpr uint64_t kLayoutHash = 0x8099154d8125ac8cull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 47},
            {16, 16, 3, 16, 0, 0, 48},
            {64, 16, 1, 0, 0, 0, 51},
            {80, 16, 1, 0, 0, 0, 52},
            {96, 12, 1, 0, 0, 0, 53},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(shadowmodel_vs20, cModulationColor) == 0, "HLSL member offset");
static_assert(offsetof(shadowmodel_vs20, cShadowTextureMatrix) == 16, "HLSL member offset");
static_assert(offsetof(shadowmodel_vs20, cTexOrigin) == 64, "HLSL member offset");
static_assert(offsetof(shadowmodel_vs20, cTexScale) == 80, "HLSL member offset");
static_assert(offsetof(shadowmodel_vs20, cShadowConstants) == 96, "HLSL member offset");
static_assert(sizeof(shadowmodel_vs20) == 112, "HLSL cbuffer size");
} // namespace dx12cb
