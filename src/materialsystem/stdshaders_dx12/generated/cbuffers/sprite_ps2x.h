#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) sprite_ps2x {
    float g_Color[4]{};
    float g_HDRColorScale[1]{};
    uint8_t _pad0[12]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x3388bb38e50dd7f9ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 4, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(sprite_ps2x, g_Color) == 0, "HLSL member offset");
static_assert(offsetof(sprite_ps2x, g_HDRColorScale) == 16, "HLSL member offset");
static_assert(offsetof(sprite_ps2x, g_EyePos_SpecExponent) == 32, "HLSL member offset");
static_assert(sizeof(sprite_ps2x) == 48, "HLSL cbuffer size");
} // namespace dx12cb
