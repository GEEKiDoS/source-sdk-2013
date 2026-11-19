#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) NekoModelPS {
    float g_NekoColor[4]{};
    float g_NekoEye[4]{};
    float g_NekoLighting[4]{};
    float g_NekoWarp[4]{};
    float g_NekoRim[4]{};
    float g_NekoCap[4]{};
    float g_NekoCapTint[4]{};
    float g_NekoOutline[4]{};
    float g_NekoSurface[4]{};
    float g_NekoEmit[4]{};
    float g_NekoRight[4]{};
    float g_NekoUp[4]{};
    float g_NekoDisplay[4]{};
    float g_NekoProbeOrigin[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 224;
    static constexpr uint64_t kLayoutHash = 0x4217a20a29827827ull;
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
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 14, entries };
        return map;
    }
};
static_assert(offsetof(NekoModelPS, g_NekoColor) == 0, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoEye) == 16, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoLighting) == 32, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoWarp) == 48, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoRim) == 64, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoCap) == 80, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoCapTint) == 96, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoOutline) == 112, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoSurface) == 128, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoEmit) == 144, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoRight) == 160, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoUp) == 176, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoDisplay) == 192, "HLSL member offset");
static_assert(offsetof(NekoModelPS, g_NekoProbeOrigin) == 208, "HLSL member offset");
static_assert(sizeof(NekoModelPS) == 224, "HLSL cbuffer size");
} // namespace dx12cb
