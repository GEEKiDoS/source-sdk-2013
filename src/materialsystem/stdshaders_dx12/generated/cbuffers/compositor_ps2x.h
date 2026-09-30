#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) compositor_ps2x {
    float cAdjustInLevel[4][4]{};
    float cNumTextures[1]{};
    uint8_t _pad0[12]{};
    float cSelectValues[4][4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 144;
    static constexpr uint64_t kLayoutHash = 0xb60471ea29d2653bull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 4, 16, 0, 0, 2},
            {64, 4, 1, 0, 0, 0, 6},
            {80, 16, 4, 16, 0, 0, 7},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(compositor_ps2x, cAdjustInLevel) == 0, "HLSL member offset");
static_assert(offsetof(compositor_ps2x, cNumTextures) == 64, "HLSL member offset");
static_assert(offsetof(compositor_ps2x, cSelectValues) == 80, "HLSL member offset");
static_assert(sizeof(compositor_ps2x) == 144, "HLSL cbuffer size");
} // namespace dx12cb
