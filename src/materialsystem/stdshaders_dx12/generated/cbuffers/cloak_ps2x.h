#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) cloak_ps2x {
    float g_ViewProj[11]{};
    uint8_t _pad0[4]{};
    float g_CloakControl[2]{};
    uint8_t _pad1[8]{};
    float g_EyePos_SpecExponent[4]{};
    float g_FlashlightAttenuationFactors_RimMask[4]{};
    float g_RimBoost[4]{};
    float g_FresnelSpecParams[4]{};
    float g_SpecularRimParams[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 144;
    static constexpr uint64_t kLayoutHash = 0xe7a051e573afb64full;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 3, 16, 0, 0, 0},
            {48, 8, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 11},
            {80, 16, 1, 0, 0, 0, 13},
            {96, 16, 1, 0, 0, 0, 14},
            {112, 16, 1, 0, 0, 0, 19},
            {128, 16, 1, 0, 0, 0, 26},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(cloak_ps2x, g_ViewProj) == 0, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_CloakControl) == 48, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_EyePos_SpecExponent) == 64, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_FlashlightAttenuationFactors_RimMask) == 80, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_RimBoost) == 96, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_FresnelSpecParams) == 112, "HLSL member offset");
static_assert(offsetof(cloak_ps2x, g_SpecularRimParams) == 128, "HLSL member offset");
static_assert(sizeof(cloak_ps2x) == 144, "HLSL cbuffer size");
} // namespace dx12cb
