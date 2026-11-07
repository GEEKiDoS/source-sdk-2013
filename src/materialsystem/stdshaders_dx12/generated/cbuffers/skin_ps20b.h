#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) skin_ps20b {
    float g_SelfIllumTint_and_DetailBlendFactor[4]{};
    float g_DiffuseModulation[4]{};
    float g_EnvmapTint_ShadowTweaks[4]{};
    float g_SelfIllumScaleBiasExpBrightness[4]{};
    float g_EnvMapFresnel[4]{};
    float g_EyePos_SpecExponent[4]{};
    float g_FlashlightAttenuationFactors_RimMask[4]{};
    float g_FlashlightPos_RimBoost[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float g_FresnelSpecParams[4]{};
    float g_SpecularRimParams[4]{};
    float g_ShaderControls[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 272;
    static constexpr uint64_t kLayoutHash = 0xff43f7269a0b071dull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 10},
            {80, 16, 1, 0, 0, 0, 11},
            {96, 16, 1, 0, 0, 0, 13},
            {112, 16, 1, 0, 0, 0, 14},
            {128, 16, 4, 16, 0, 0, 15},
            {192, 16, 1, 0, 0, 0, 19},
            {208, 16, 1, 0, 0, 0, 26},
            {224, 16, 1, 0, 0, 0, 27},
            {240, 16, 1, 0, 0, 0, 28},
            {256, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 14, entries };
        return map;
    }
};
static_assert(offsetof(skin_ps20b, g_SelfIllumTint_and_DetailBlendFactor) == 0, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_DiffuseModulation) == 16, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_EnvmapTint_ShadowTweaks) == 32, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_SelfIllumScaleBiasExpBrightness) == 48, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_EnvMapFresnel) == 64, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_EyePos_SpecExponent) == 80, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_FlashlightAttenuationFactors_RimMask) == 96, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_FlashlightPos_RimBoost) == 112, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_FlashlightWorldToTexture) == 128, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_FresnelSpecParams) == 192, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_SpecularRimParams) == 208, "HLSL member offset");
static_assert(offsetof(skin_ps20b, g_ShaderControls) == 224, "HLSL member offset");
static_assert(offsetof(skin_ps20b, cFlashlightColor) == 240, "HLSL member offset");
static_assert(offsetof(skin_ps20b, cFlashlightScreenScale) == 256, "HLSL member offset");
static_assert(sizeof(skin_ps20b) == 272, "HLSL cbuffer size");
} // namespace dx12cb
