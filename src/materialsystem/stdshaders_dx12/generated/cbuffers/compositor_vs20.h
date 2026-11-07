#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) compositor_vs20 {
    float cXformTexCoord0[2][4]{};
    float cXformTexCoord1[2][4]{};
    float cXformTexCoord2[2][4]{};
    float cXformTexCoord3[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 128;
    static constexpr uint64_t kLayoutHash = 0x1cafa792302bd104ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 2},
            {32, 16, 2, 16, 0, 0, 4},
            {64, 16, 2, 16, 0, 0, 6},
            {96, 16, 2, 16, 0, 0, 8},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(compositor_vs20, cXformTexCoord0) == 0, "HLSL member offset");
static_assert(offsetof(compositor_vs20, cXformTexCoord1) == 32, "HLSL member offset");
static_assert(offsetof(compositor_vs20, cXformTexCoord2) == 64, "HLSL member offset");
static_assert(offsetof(compositor_vs20, cXformTexCoord3) == 96, "HLSL member offset");
static_assert(sizeof(compositor_vs20) == 128, "HLSL cbuffer size");
} // namespace dx12cb
