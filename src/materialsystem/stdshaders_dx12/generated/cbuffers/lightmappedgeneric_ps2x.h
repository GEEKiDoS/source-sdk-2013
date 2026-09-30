#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_ps2x {
    float g_EnvmapTint[4]{};
    float g_OutlineParams[4]{};
    float g_EnvmapContrast_c2[3]{};
    uint8_t _pad0[4]{};
    float g_OutlineColor[4]{};
    float g_EnvmapSaturation_c3[3]{};
    uint8_t _pad1[4]{};
    float g_EdgeSoftnessParms[4]{};
    float g_FresnelReflectionReg[4]{};
    float g_SelfIllumTint_c7[4]{};
    float g_DetailTint_and_BlendFactor[4]{};
    float g_EyePos[3]{};
    uint8_t _pad2[4]{};
    float g_TintValuesAndLightmapScale[4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float g_FlashlightPos[3]{};
    uint8_t _pad3[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float g_ShadowTweaks[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 320;
    static constexpr uint64_t kLayoutHash = 0x25ed3cf66ad20db8ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 2},
            {32, 12, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 12, 1, 0, 0, 0, 3},
            {80, 16, 1, 0, 0, 0, 4},
            {96, 16, 1, 0, 0, 0, 4},
            {112, 16, 1, 0, 0, 0, 7},
            {128, 16, 1, 0, 0, 0, 8},
            {144, 12, 1, 0, 0, 0, 10},
            {160, 16, 1, 0, 0, 0, 12},
            {176, 16, 1, 0, 0, 0, 13},
            {192, 12, 1, 0, 0, 0, 14},
            {208, 16, 4, 16, 0, 0, 15},
            {272, 16, 1, 0, 0, 0, 19},
            {288, 16, 1, 0, 0, 0, 28},
            {304, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 17, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_ps2x, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_OutlineParams) == 16, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_EnvmapContrast_c2) == 32, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_OutlineColor) == 48, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_EnvmapSaturation_c3) == 64, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_EdgeSoftnessParms) == 80, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_FresnelReflectionReg) == 96, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_SelfIllumTint_c7) == 112, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_DetailTint_and_BlendFactor) == 128, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_EyePos) == 144, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_TintValuesAndLightmapScale) == 160, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_FlashlightAttenuationFactors) == 176, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_FlashlightPos) == 192, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_FlashlightWorldToTexture) == 208, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, g_ShadowTweaks) == 272, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, cFlashlightColor) == 288, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x, cFlashlightScreenScale) == 304, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_ps2x) == 320, "HLSL cbuffer size");
} // namespace dx12cb
