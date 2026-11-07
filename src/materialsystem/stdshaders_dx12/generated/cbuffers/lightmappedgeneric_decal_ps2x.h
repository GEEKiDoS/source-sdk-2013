#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_decal_ps2x {
    float g_LightMap0Color[4]{};
    float g_LightMap1Color[4]{};
    float g_LightMap2Color[4]{};
    float g_ModulationColor[4]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0x84a336ffd3ae2172ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_decal_ps2x, g_LightMap0Color) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_decal_ps2x, g_LightMap1Color) == 16, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_decal_ps2x, g_LightMap2Color) == 32, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_decal_ps2x, g_ModulationColor) == 48, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_decal_ps2x, g_EyePos_SpecExponent) == 64, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_decal_ps2x) == 80, "HLSL cbuffer size");
} // namespace dx12cb
