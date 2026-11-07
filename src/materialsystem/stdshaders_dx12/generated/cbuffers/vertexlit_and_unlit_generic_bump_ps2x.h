#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vertexlit_and_unlit_generic_bump_ps2x {
    float g_EnvmapTint_TintReplaceFactor[4]{};
    float g_DiffuseModulation[4]{};
    float g_EnvmapContrast_ShadowTweaks[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad0[4]{};
    float g_SelfIllumTint_and_BlendFactor[4]{};
    float g_SelfIllumScaleBiasExpBrightness[4]{};
    float g_ShaderControls[4]{};
    float g_EyePos[3]{};
    uint8_t _pad1[4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float g_FlashlightPos[3]{};
    uint8_t _pad2[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 256;
    static constexpr uint64_t kLayoutHash = 0x4005a7e06e303857ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 16, 1, 0, 0, 0, 11},
            {96, 16, 1, 0, 0, 0, 12},
            {112, 12, 1, 0, 0, 0, 20},
            {128, 16, 1, 0, 0, 0, 22},
            {144, 12, 1, 0, 0, 0, 23},
            {160, 16, 4, 16, 0, 0, 24},
            {224, 16, 1, 0, 0, 0, 28},
            {240, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 13, entries };
        return map;
    }
};
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_EnvmapTint_TintReplaceFactor) == 0, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_DiffuseModulation) == 16, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_EnvmapContrast_ShadowTweaks) == 32, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_SelfIllumTint_and_BlendFactor) == 64, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_SelfIllumScaleBiasExpBrightness) == 80, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_ShaderControls) == 96, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_EyePos) == 112, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_FlashlightAttenuationFactors) == 128, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_FlashlightPos) == 144, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, g_FlashlightWorldToTexture) == 160, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, cFlashlightColor) == 224, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_ps2x, cFlashlightScreenScale) == 240, "HLSL member offset");
static_assert(sizeof(vertexlit_and_unlit_generic_bump_ps2x) == 256, "HLSL cbuffer size");
} // namespace dx12cb
