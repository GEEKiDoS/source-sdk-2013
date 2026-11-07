#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vortwarp_ps2x {
    float g_EnvmapTint[4]{};
    float g_DiffuseModulation[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad0[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad1[4]{};
    float g_SelfIllumTint[4]{};
    float g_EyePos[3]{};
    uint8_t _pad2[4]{};
    float g_FlashlightAttenuationFactors[4]{};
    float g_Time[1]{};
    float g_FlashlightPos[3]{};
    float g_FlashlightWorldToTexture[4][4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 224;
    static constexpr uint64_t kLayoutHash = 0x42805cf75bff8585ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 12, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 12, 1, 0, 0, 0, 20},
            {96, 16, 1, 0, 0, 0, 22},
            {112, 4, 1, 0, 0, 0, 22},
            {116, 12, 1, 0, 0, 0, 23},
            {128, 16, 4, 16, 0, 0, 24},
            {192, 16, 1, 0, 0, 0, 28},
            {208, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 12, entries };
        return map;
    }
};
static_assert(offsetof(vortwarp_ps2x, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_DiffuseModulation) == 16, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_EnvmapContrast) == 32, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_SelfIllumTint) == 64, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_EyePos) == 80, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_FlashlightAttenuationFactors) == 96, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_Time) == 112, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_FlashlightPos) == 116, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, g_FlashlightWorldToTexture) == 128, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, cFlashlightColor) == 192, "HLSL member offset");
static_assert(offsetof(vortwarp_ps2x, cFlashlightScreenScale) == 208, "HLSL member offset");
static_assert(sizeof(vortwarp_ps2x) == 224, "HLSL cbuffer size");
} // namespace dx12cb
