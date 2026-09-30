#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) morphaccumulate_vs30 {
    float vMorphWeightSubrect[4]{};
    float vMorphWeightDimensions[4]{};
    float cFlexWeights[512][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 8224;
    static constexpr uint64_t kLayoutHash = 0x7937811f4f801813ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 1, 0, 0, 0, 49},
            {32, 16, 512, 16, 0, 0, 1024},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(morphaccumulate_vs30, vMorphWeightSubrect) == 0, "HLSL member offset");
static_assert(offsetof(morphaccumulate_vs30, vMorphWeightDimensions) == 16, "HLSL member offset");
static_assert(offsetof(morphaccumulate_vs30, cFlexWeights) == 32, "HLSL member offset");
static_assert(sizeof(morphaccumulate_vs30) == 8224, "HLSL cbuffer size");
} // namespace dx12cb
