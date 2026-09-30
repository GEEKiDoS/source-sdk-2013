#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) flashlight_ps2x {
    float g_DetailConstants[4]{};
    float g_vShadowTweaks[4]{};
    float g_EyePos[4]{};
    float g_FlashlightAttenuation[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x141067ef4522859eull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 2},
            {32, 16, 1, 0, 0, 0, 11},
            {48, 16, 1, 0, 0, 0, 13},
            {64, 16, 1, 0, 0, 0, 28},
            {80, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(flashlight_ps2x, g_DetailConstants) == 0, "HLSL member offset");
static_assert(offsetof(flashlight_ps2x, g_vShadowTweaks) == 16, "HLSL member offset");
static_assert(offsetof(flashlight_ps2x, g_EyePos) == 32, "HLSL member offset");
static_assert(offsetof(flashlight_ps2x, g_FlashlightAttenuation) == 48, "HLSL member offset");
static_assert(offsetof(flashlight_ps2x, cFlashlightColor) == 64, "HLSL member offset");
static_assert(offsetof(flashlight_ps2x, cFlashlightScreenScale) == 80, "HLSL member offset");
static_assert(sizeof(flashlight_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
