#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) teeth_bump_ps2x {
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0xf52b7cebdf17e06bull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(teeth_bump_ps2x, g_EyePos_SpecExponent) == 0, "HLSL member offset");
static_assert(sizeof(teeth_bump_ps2x) == 16, "HLSL cbuffer size");
} // namespace dx12cb
