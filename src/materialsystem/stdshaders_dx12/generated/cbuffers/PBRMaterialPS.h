#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) PBRMaterialPS {
    float g_BaseTint[4]{};
    float g_Surface[4]{};
    float g_SpecularColor[4]{};
    float g_Flags0[4]{};
    float g_Flags1[4]{};
    float g_ReflectionTint[4]{};
    float g_Parallax0[4]{};
    float g_Parallax1[4]{};
    float g_Parallax2[4]{};
    float g_EnvmapOrigin[4]{};
    float g_EmissionTint[4]{};
    float g_Options[4]{};
    float g_SubsurfaceTint[4]{};
    float g_BacklightTint[4]{};
    float g_Misc[4]{};
    float g_PbrDetail[4]{};
    float g_PbrFlags2[4]{};
    float g_PbrBlend[4]{};
    float g_PbrMaskTransform[2][4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 320;
    static constexpr uint64_t kLayoutHash = 0xe13458959e25d606ull;
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
            {160, 16, 1, 0, 0, 0, 10},
            {176, 16, 1, 0, 0, 0, 11},
            {192, 16, 1, 0, 0, 0, 12},
            {208, 16, 1, 0, 0, 0, 13},
            {224, 16, 1, 0, 0, 0, 14},
            {240, 16, 1, 0, 0, 0, 30},
            {256, 16, 1, 0, 0, 0, 31},
            {272, 16, 1, 0, 0, 0, 32},
            {288, 16, 2, 16, 0, 0, 33},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 19, entries };
        return map;
    }
};
static_assert(offsetof(PBRMaterialPS, g_BaseTint) == 0, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Surface) == 16, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_SpecularColor) == 32, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Flags0) == 48, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Flags1) == 64, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_ReflectionTint) == 80, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Parallax0) == 96, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Parallax1) == 112, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Parallax2) == 128, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_EnvmapOrigin) == 144, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_EmissionTint) == 160, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Options) == 176, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_SubsurfaceTint) == 192, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_BacklightTint) == 208, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_Misc) == 224, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_PbrDetail) == 240, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_PbrFlags2) == 256, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_PbrBlend) == 272, "HLSL member offset");
static_assert(offsetof(PBRMaterialPS, g_PbrMaskTransform) == 288, "HLSL member offset");
static_assert(sizeof(PBRMaterialPS) == 320, "HLSL cbuffer size");
} // namespace dx12cb
