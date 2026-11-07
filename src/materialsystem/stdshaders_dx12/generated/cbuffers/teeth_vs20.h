#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) teeth_vs20 {
    float cTeethLighting[4]{};
    float const4[4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x3de34a245dd9a4fbull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 1, 0, 0, 0, 49},
            {32, 12, 1, 0, 0, 0, 54},
            {48, 16, 1, 0, 0, 0, 55},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(teeth_vs20, cTeethLighting) == 0, "HLSL member offset");
static_assert(offsetof(teeth_vs20, const4) == 16, "HLSL member offset");
static_assert(offsetof(teeth_vs20, cMorphTargetTextureDim) == 32, "HLSL member offset");
static_assert(offsetof(teeth_vs20, cMorphSubrect) == 48, "HLSL member offset");
static_assert(sizeof(teeth_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
