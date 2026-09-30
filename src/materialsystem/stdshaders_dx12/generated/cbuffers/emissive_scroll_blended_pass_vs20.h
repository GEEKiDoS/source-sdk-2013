#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) emissive_scroll_blended_pass_vs20 {
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x9630e32fe2fd71dfull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 54},
            {16, 16, 1, 0, 0, 0, 55},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(emissive_scroll_blended_pass_vs20, cMorphTargetTextureDim) == 0, "HLSL member offset");
static_assert(offsetof(emissive_scroll_blended_pass_vs20, cMorphSubrect) == 16, "HLSL member offset");
static_assert(sizeof(emissive_scroll_blended_pass_vs20) == 32, "HLSL cbuffer size");
} // namespace dx12cb
