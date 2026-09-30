#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) eyes_flashlight_ps2x {
    float g_vShadowTweaks[4]{};
    float g_EyePos_SpecExponent[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0xaefa89c1cabd4f07ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 2},
            {16, 16, 1, 0, 0, 0, 11},
            {32, 16, 1, 0, 0, 0, 28},
            {48, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(eyes_flashlight_ps2x, g_vShadowTweaks) == 0, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_ps2x, g_EyePos_SpecExponent) == 16, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_ps2x, cFlashlightColor) == 32, "HLSL member offset");
static_assert(offsetof(eyes_flashlight_ps2x, cFlashlightScreenScale) == 48, "HLSL member offset");
static_assert(sizeof(eyes_flashlight_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
