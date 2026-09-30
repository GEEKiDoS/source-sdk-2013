#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vertexlit_and_unlit_generic_ps2x {
    float g_EnvmapTint_TintReplaceFactor[4]{};
    float g_DiffuseModulation[4]{};
    float g_EnvmapContrast_ShadowTweaks[4]{};
    float g_EnvmapSaturation_SelfIllumMask[4]{};
    float g_SelfIllumTint_and_BlendFactor[4]{};
    float g_GlowParameters[4]{};
    float g_GlowColor[4]{};
    float g_DistanceAlphaParams[4]{};
    float g_OutlineColor[4]{};
    float g_OutlineParams[4]{};
    float g_DetailTint[3]{};
    uint8_t _pad0[4]{};
    float g_LuxelScale[4]{};
    float g_ShaderControls[4]{};
    float g_DepthFeatheringConstants[4]{};
    float g_EyePos[4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float g_FlashlightPos[3]{};
    uint8_t _pad1[4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 368;
    static constexpr uint64_t kLayoutHash = 0x72a66b8d05057c6aull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 16, 1, 0, 0, 0, 5},
            {96, 16, 1, 0, 0, 0, 6},
            {112, 16, 1, 0, 0, 0, 7},
            {128, 16, 1, 0, 0, 0, 8},
            {144, 16, 1, 0, 0, 0, 9},
            {160, 12, 1, 0, 0, 0, 10},
            {176, 16, 1, 0, 0, 0, 11},
            {192, 16, 1, 0, 0, 0, 12},
            {208, 16, 1, 0, 0, 0, 13},
            {224, 16, 1, 0, 0, 0, 20},
            {240, 16, 1, 0, 0, 0, 22},
            {256, 12, 1, 0, 0, 0, 23},
            {272, 16, 4, 16, 0, 0, 24},
            {336, 16, 1, 0, 0, 0, 28},
            {352, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 20, entries };
        return map;
    }
};
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_EnvmapTint_TintReplaceFactor) == 0, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_DiffuseModulation) == 16, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_EnvmapContrast_ShadowTweaks) == 32, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_EnvmapSaturation_SelfIllumMask) == 48, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_SelfIllumTint_and_BlendFactor) == 64, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_GlowParameters) == 80, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_GlowColor) == 96, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_DistanceAlphaParams) == 112, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_OutlineColor) == 128, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_OutlineParams) == 144, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_DetailTint) == 160, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_LuxelScale) == 176, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_ShaderControls) == 192, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_DepthFeatheringConstants) == 208, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_EyePos) == 224, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_FlashlightAttenuationFactors) == 240, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_FlashlightPos) == 256, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, g_FlashlightWorldToTexture) == 272, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, cFlashlightColor) == 336, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_ps2x, cFlashlightScreenScale) == 352, "HLSL member offset");
static_assert(sizeof(vertexlit_and_unlit_generic_ps2x) == 368, "HLSL cbuffer size");
} // namespace dx12cb
