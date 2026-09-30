#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedreflective_vs20 {
    float cBumpTexCoordTransform[2][4]{};
    float cBaseTextureTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x6adaabb59d88a0a5ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 49},
            {32, 16, 2, 16, 0, 0, 51},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedreflective_vs20, cBumpTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedreflective_vs20, cBaseTextureTransform) == 32, "HLSL member offset");
static_assert(sizeof(lightmappedreflective_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
