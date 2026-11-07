#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) BlurFilter_ps2x {
    float psTapOffs[10]{};
    uint8_t _pad0[8]{};
    float scale_factor[3]{};
    uint8_t _pad1[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x3dfad46f3b87f1eaull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 3, 16, 0, 0, 0},
            {48, 12, 1, 0, 0, 0, 3},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(BlurFilter_ps2x, psTapOffs) == 0, "HLSL member offset");
static_assert(offsetof(BlurFilter_ps2x, scale_factor) == 48, "HLSL member offset");
static_assert(sizeof(BlurFilter_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
