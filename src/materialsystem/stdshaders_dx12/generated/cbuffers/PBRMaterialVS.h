#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) PBRMaterialVS {
    float g_BaseTransform[2][4]{};
    float g_BumpTransform[2][4]{};
    float g_DetailTransform[2][4]{};
    float g_BlendTransform[2][4]{};
    float g_MorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float g_MorphSubrect[4]{};
    float g_PbrVertexColor[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0x43b230be442689f1ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 16, 2, 16, 0, 0, 50},
            {64, 16, 2, 16, 0, 0, 52},
            {96, 16, 2, 16, 0, 0, 54},
            {128, 12, 1, 0, 0, 0, 14},
            {144, 16, 1, 0, 0, 0, 15},
            {160, 16, 1, 0, 0, 0, 56},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(PBRMaterialVS, g_BaseTransform) == 0, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_BumpTransform) == 32, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_DetailTransform) == 64, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_BlendTransform) == 96, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_MorphTargetTextureDim) == 128, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_MorphSubrect) == 144, "HLSL member offset");
static_assert(offsetof(PBRMaterialVS, g_PbrVertexColor) == 160, "HLSL member offset");
static_assert(sizeof(PBRMaterialVS) == 176, "HLSL cbuffer size");
} // namespace dx12cb
