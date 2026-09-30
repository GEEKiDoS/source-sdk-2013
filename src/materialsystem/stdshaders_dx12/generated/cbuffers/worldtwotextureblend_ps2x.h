#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) worldtwotextureblend_ps2x {
    float g_SelfIllumTint[4]{};
    float g_EyePos[3]{};
    uint8_t _pad0[4]{};
    float g_FlashlightPos[3]{};
    uint8_t _pad1[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0x6cdbb2937f89232eull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 7},
            {16, 12, 1, 0, 0, 0, 10},
            {32, 12, 1, 0, 0, 0, 15},
            {48, 16, 4, 16, 0, 0, 16},
            {112, 16, 1, 0, 0, 0, 20},
            {128, 16, 1, 0, 0, 0, 28},
            {144, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(worldtwotextureblend_ps2x, g_SelfIllumTint) == 0, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, g_EyePos) == 16, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, g_FlashlightPos) == 32, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, g_FlashlightWorldToTexture) == 48, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, g_FlashlightAttenuationFactors) == 112, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, cFlashlightColor) == 128, "HLSL member offset");
static_assert(offsetof(worldtwotextureblend_ps2x, cFlashlightScreenScale) == 144, "HLSL member offset");
static_assert(sizeof(worldtwotextureblend_ps2x) == 160, "HLSL cbuffer size");
} // namespace dx12cb
