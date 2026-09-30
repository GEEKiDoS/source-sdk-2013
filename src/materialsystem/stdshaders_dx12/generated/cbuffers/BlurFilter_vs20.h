#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) BlurFilter_vs20 {
    float vsTapOffs[10]{};
    uint8_t _pad0[8]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x9a8e34c146ef3e92ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 3, 16, 0, 0, 48},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(BlurFilter_vs20, vsTapOffs) == 0, "HLSL member offset");
static_assert(sizeof(BlurFilter_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
