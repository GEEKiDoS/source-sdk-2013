#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) warp_ps2x {
    float g_vWarpParms0[4]{};
    float g_vWarpParms1[4]{};
    float g_vWarpParms2[4]{};
    float g_vWarpParms3[4]{};
    float g_vWarpParms4[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0x7d472520980db26aull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(warp_ps2x, g_vWarpParms0) == 0, "HLSL member offset");
static_assert(offsetof(warp_ps2x, g_vWarpParms1) == 16, "HLSL member offset");
static_assert(offsetof(warp_ps2x, g_vWarpParms2) == 32, "HLSL member offset");
static_assert(offsetof(warp_ps2x, g_vWarpParms3) == 48, "HLSL member offset");
static_assert(offsetof(warp_ps2x, g_vWarpParms4) == 64, "HLSL member offset");
static_assert(sizeof(warp_ps2x) == 80, "HLSL cbuffer size");
} // namespace dx12cb
