#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vertexlit_and_unlit_generic_bump_vs20 {
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    float cBaseTexCoordTransform[2][4]{};
    float cDetailTexCoordTransform[2][4]{};
    float g_FlashlightWorldToTexture[4][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0x23d2aee968e64867ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 14},
            {16, 16, 1, 0, 0, 0, 15},
            {32, 16, 2, 16, 0, 0, 48},
            {64, 16, 2, 16, 0, 0, 52},
            {96, 16, 4, 16, 0, 0, 54},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(vertexlit_and_unlit_generic_bump_vs20, cMorphTargetTextureDim) == 0, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_vs20, cMorphSubrect) == 16, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_vs20, cBaseTexCoordTransform) == 32, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_vs20, cDetailTexCoordTransform) == 64, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_bump_vs20, g_FlashlightWorldToTexture) == 96, "HLSL member offset");
static_assert(sizeof(vertexlit_and_unlit_generic_bump_vs20) == 160, "HLSL cbuffer size");
} // namespace dx12cb
