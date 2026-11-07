#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) WaterCheap_vs20 {
    float cNormalMapTransform[2][4]{};
    float TexOffsets[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x772a244c6e4b144cull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 16, 1, 0, 0, 0, 51},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(WaterCheap_vs20, cNormalMapTransform) == 0, "HLSL member offset");
static_assert(offsetof(WaterCheap_vs20, TexOffsets) == 32, "HLSL member offset");
static_assert(sizeof(WaterCheap_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
