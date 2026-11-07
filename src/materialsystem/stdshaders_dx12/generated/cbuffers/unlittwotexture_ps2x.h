#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) unlittwotexture_ps2x {
    float g_DiffuseModulation[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad0[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad1[4]{};
    float g_EyePos_SpecExponent[4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float g_FlashlightPos[3]{};
    uint8_t _pad2[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0x5319df76eb35e0c9ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 1},
            {16, 12, 1, 0, 0, 0, 2},
            {32, 12, 1, 0, 0, 0, 3},
            {48, 16, 1, 0, 0, 0, 11},
            {64, 16, 1, 0, 0, 0, 22},
            {80, 12, 1, 0, 0, 0, 23},
            {96, 16, 4, 16, 0, 0, 24},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(unlittwotexture_ps2x, g_DiffuseModulation) == 0, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_EnvmapContrast) == 16, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_EnvmapSaturation) == 32, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_EyePos_SpecExponent) == 48, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_FlashlightAttenuationFactors) == 64, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_FlashlightPos) == 80, "HLSL member offset");
static_assert(offsetof(unlittwotexture_ps2x, g_FlashlightWorldToTexture) == 96, "HLSL member offset");
static_assert(sizeof(unlittwotexture_ps2x) == 160, "HLSL cbuffer size");
} // namespace dx12cb
