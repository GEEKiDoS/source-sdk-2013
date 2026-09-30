#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lpreview1_ps2x {
    float Light_origin[4]{};
    float Light_dir[4]{};
    float Light_attn[4]{};
    float Light_color[3]{};
    uint8_t _pad0[4]{};
    float EyePosition[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0x62adeac365766f2aull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 10},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(lpreview1_ps2x, Light_origin) == 0, "HLSL member offset");
static_assert(offsetof(lpreview1_ps2x, Light_dir) == 16, "HLSL member offset");
static_assert(offsetof(lpreview1_ps2x, Light_attn) == 32, "HLSL member offset");
static_assert(offsetof(lpreview1_ps2x, Light_color) == 48, "HLSL member offset");
static_assert(offsetof(lpreview1_ps2x, EyePosition) == 64, "HLSL member offset");
static_assert(sizeof(lpreview1_ps2x) == 80, "HLSL cbuffer size");
} // namespace dx12cb
