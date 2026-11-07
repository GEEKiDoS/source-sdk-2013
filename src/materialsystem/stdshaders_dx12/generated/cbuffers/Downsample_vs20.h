#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) Downsample_vs20 {
    float vsTapOffs[14]{};
    uint8_t _pad0[8]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x9b51bf3ecd17dd41ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 4, 16, 0, 0, 48},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(Downsample_vs20, vsTapOffs) == 0, "HLSL member offset");
static_assert(sizeof(Downsample_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
