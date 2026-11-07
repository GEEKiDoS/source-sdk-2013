#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) spritecard_vsxx {
    float ViewportTransformScaled[4]{};
    float cModelView[3][4]{};
    float cProj[4][4]{};
    float ScaleParms[4]{};
    float SizeParms[4]{};
    float SizeParms2[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0x0fd3ec0573ec0e26ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 14},
            {16, 16, 3, 16, 0, 0, 48},
            {64, 16, 4, 16, 0, 0, 51},
            {128, 16, 1, 0, 0, 0, 55},
            {144, 16, 1, 0, 0, 0, 56},
            {160, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(spritecard_vsxx, ViewportTransformScaled) == 0, "HLSL member offset");
static_assert(offsetof(spritecard_vsxx, cModelView) == 16, "HLSL member offset");
static_assert(offsetof(spritecard_vsxx, cProj) == 64, "HLSL member offset");
static_assert(offsetof(spritecard_vsxx, ScaleParms) == 128, "HLSL member offset");
static_assert(offsetof(spritecard_vsxx, SizeParms) == 144, "HLSL member offset");
static_assert(offsetof(spritecard_vsxx, SizeParms2) == 160, "HLSL member offset");
static_assert(sizeof(spritecard_vsxx) == 176, "HLSL cbuffer size");
} // namespace dx12cb
