#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_ps2x_shadowmap {
    float g_EnvmapTint[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad0[4]{};
    float g_OutlineParams[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad1[4]{};
    float g_OutlineColor[4]{};
    float g_FresnelReflectionReg[4]{};
    float g_EdgeSoftnessParms[4]{};
    float g_SelfIllumTint[4]{};
    float g_DetailTint_and_BlendFactor[4]{};
    float g_EyePos[3]{};
    uint8_t _pad2[4]{};
    float g_TintValuesAndLightmapScale[4]{};
    float g_ShadowDirectTint[3]{};
    uint8_t _pad3[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 192;
    static constexpr uint64_t kLayoutHash = 0x4665556fa3ee3d73ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 12, 1, 0, 0, 0, 2},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 3},
            {80, 16, 1, 0, 0, 0, 4},
            {96, 16, 1, 0, 0, 0, 4},
            {112, 16, 1, 0, 0, 0, 7},
            {128, 16, 1, 0, 0, 0, 8},
            {144, 12, 1, 0, 0, 0, 10},
            {160, 16, 1, 0, 0, 0, 12},
            {176, 12, 1, 0, 0, 0, 223},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 12, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_EnvmapContrast) == 16, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_OutlineParams) == 32, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_OutlineColor) == 64, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_FresnelReflectionReg) == 80, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_EdgeSoftnessParms) == 96, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_SelfIllumTint) == 112, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_DetailTint_and_BlendFactor) == 128, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_EyePos) == 144, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_TintValuesAndLightmapScale) == 160, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_ps2x_shadowmap, g_ShadowDirectTint) == 176, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_ps2x_shadowmap) == 192, "HLSL cbuffer size");
} // namespace dx12cb
