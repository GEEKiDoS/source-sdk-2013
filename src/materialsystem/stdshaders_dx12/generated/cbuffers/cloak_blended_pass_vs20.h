#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) cloak_blended_pass_vs20 {
    float cBaseTexCoordTransform[2][4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x40fbe06aba599a8aull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 12, 1, 0, 0, 0, 54},
            {48, 16, 1, 0, 0, 0, 55},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(cloak_blended_pass_vs20, cBaseTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_vs20, cMorphTargetTextureDim) == 32, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_vs20, cMorphSubrect) == 48, "HLSL member offset");
static_assert(sizeof(cloak_blended_pass_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
