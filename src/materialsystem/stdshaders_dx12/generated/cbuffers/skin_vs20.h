#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) skin_vs20 {
    float cBaseTexCoordTransform[2][4]{};
    float cDetailTexCoordTransform[2][4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0xec44da26791a81beull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 16, 2, 16, 0, 0, 52},
            {64, 12, 1, 0, 0, 0, 54},
            {80, 16, 1, 0, 0, 0, 55},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(skin_vs20, cBaseTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(skin_vs20, cDetailTexCoordTransform) == 32, "HLSL member offset");
static_assert(offsetof(skin_vs20, cMorphTargetTextureDim) == 64, "HLSL member offset");
static_assert(offsetof(skin_vs20, cMorphSubrect) == 80, "HLSL member offset");
static_assert(sizeof(skin_vs20) == 96, "HLSL cbuffer size");
} // namespace dx12cb
