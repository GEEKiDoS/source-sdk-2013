#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) GtaoApply {
    float gApplyOriginFullSize[4]{};
    float gApplyAoSizeUnpack[4]{};
    float gApplyOptions[4]{};
    float gApplyFogEye[4]{};
    float gApplyFogClipZ[4]{};
    float gApplyFogWorld0[4]{};
    float gApplyFogWorld1[4]{};
    float gApplyFogWorld2[4]{};
    float gApplyFogWorld3[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 144;
    static constexpr uint64_t kLayoutHash = 0x571a3a57979e2f5cull;
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
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 9, entries };
        return map;
    }
};
static_assert(offsetof(GtaoApply, gApplyOriginFullSize) == 0, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyAoSizeUnpack) == 16, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyOptions) == 32, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogEye) == 48, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogClipZ) == 64, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogWorld0) == 80, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogWorld1) == 96, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogWorld2) == 112, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyFogWorld3) == 128, "HLSL member offset");
static_assert(sizeof(GtaoApply) == 144, "HLSL cbuffer size");
} // namespace dx12cb
