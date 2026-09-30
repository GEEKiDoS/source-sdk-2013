#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) Water_vs20 {
    float cBumpTexCoordTransform[2][4]{};
    float TexOffsets[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x5c3a30800ab47194ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 49},
            {32, 16, 1, 0, 0, 0, 51},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(Water_vs20, cBumpTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(Water_vs20, TexOffsets) == 32, "HLSL member offset");
static_assert(sizeof(Water_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
