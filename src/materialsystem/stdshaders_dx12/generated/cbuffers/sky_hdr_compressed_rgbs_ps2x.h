#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) sky_hdr_compressed_rgbs_ps2x {
    float InputScale[4]{};
    float texWidthHeight[2]{};
    uint8_t _pad0[8]{};
    float texOffsets[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0xd503b6699e13679eull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 8, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(sky_hdr_compressed_rgbs_ps2x, InputScale) == 0, "HLSL member offset");
static_assert(offsetof(sky_hdr_compressed_rgbs_ps2x, texWidthHeight) == 16, "HLSL member offset");
static_assert(offsetof(sky_hdr_compressed_rgbs_ps2x, texOffsets) == 32, "HLSL member offset");
static_assert(sizeof(sky_hdr_compressed_rgbs_ps2x) == 48, "HLSL cbuffer size");
} // namespace dx12cb
