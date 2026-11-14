#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) l4d2_spritecard_vs {
    float ViewportTransformScaled[4]{};
    float g_vCropFactor[4]{};
    float cModelView[3][4]{};
    float cProj[4][4]{};
    float ScaleParms[4]{};
    float SizeParms[4]{};
    float SizeParms2[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 192;
    static constexpr uint64_t kLayoutHash = 0x565d354261ea1c09ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 14},
            {16, 16, 1, 0, 0, 0, 15},
            {32, 16, 3, 16, 0, 0, 48},
            {80, 16, 4, 16, 0, 0, 51},
            {144, 16, 1, 0, 0, 0, 55},
            {160, 16, 1, 0, 0, 0, 56},
            {176, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(l4d2_spritecard_vs, ViewportTransformScaled) == 0, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, g_vCropFactor) == 16, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, cModelView) == 32, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, cProj) == 80, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, ScaleParms) == 144, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, SizeParms) == 160, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_vs, SizeParms2) == 176, "HLSL member offset");
static_assert(sizeof(l4d2_spritecard_vs) == 192, "HLSL cbuffer size");
} // namespace dx12cb
