#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) AccumBuff4Sample_ps2x {
    float weights[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0xafcd662361502499ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(AccumBuff4Sample_ps2x, weights) == 0, "HLSL member offset");
static_assert(sizeof(AccumBuff4Sample_ps2x) == 16, "HLSL cbuffer size");
} // namespace dx12cb
