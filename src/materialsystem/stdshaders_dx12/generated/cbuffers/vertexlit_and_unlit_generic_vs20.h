#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vertexlit_and_unlit_generic_vs20 {
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float g_vMiscParams3[4]{};
    float cMorphSubrect[4]{};
    float g_vMiscParams4[4]{};
    float cBaseTexCoordTransform[2][4]{};
    float cSeamlessScale[1]{};
    uint8_t _pad1[12]{};
    float g_vMiscParams2[4]{};
    float g_vMiscParams1[4]{};
    float cDetailTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0xead1971f3798c19cull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 14},
            {16, 16, 1, 0, 0, 0, 14},
            {32, 16, 1, 0, 0, 0, 15},
            {48, 16, 1, 0, 0, 0, 15},
            {64, 16, 2, 16, 0, 0, 48},
            {96, 4, 1, 0, 0, 0, 50},
            {112, 16, 1, 0, 0, 0, 50},
            {128, 16, 1, 0, 0, 0, 51},
            {144, 16, 2, 16, 0, 0, 52},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 9, entries };
        return map;
    }
};
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, cMorphTargetTextureDim) == 0, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, g_vMiscParams3) == 16, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, cMorphSubrect) == 32, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, g_vMiscParams4) == 48, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, cBaseTexCoordTransform) == 64, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, cSeamlessScale) == 96, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, g_vMiscParams2) == 112, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, g_vMiscParams1) == 128, "HLSL member offset");
static_assert(offsetof(vertexlit_and_unlit_generic_vs20, cDetailTexCoordTransform) == 144, "HLSL member offset");
static_assert(sizeof(vertexlit_and_unlit_generic_vs20) == 176, "HLSL cbuffer size");
} // namespace dx12cb
