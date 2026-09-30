#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) ShatteredGlass_ps2x {
    float g_EnvmapTint[4]{};
    float g_DiffuseModulation[3]{};
    uint8_t _pad0[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad1[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad2[4]{};
    float g_FresnelReflection[4]{};
    float g_EyePos[3]{};
    uint8_t _pad3[4]{};
    float g_OverbrightFactor[3]{};
    uint8_t _pad4[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 112;
    static constexpr uint64_t kLayoutHash = 0x3934fe7066b5827bull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 12, 1, 0, 0, 0, 1},
            {32, 12, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 12, 1, 0, 0, 0, 5},
            {96, 12, 1, 0, 0, 0, 6},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(ShatteredGlass_ps2x, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_DiffuseModulation) == 16, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_EnvmapContrast) == 32, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_FresnelReflection) == 64, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_EyePos) == 80, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_ps2x, g_OverbrightFactor) == 96, "HLSL member offset");
static_assert(sizeof(ShatteredGlass_ps2x) == 112, "HLSL cbuffer size");
} // namespace dx12cb
