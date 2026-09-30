#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) aftershock_ps2x {
    float g_mViewProj0[4]{};
    float g_mViewProj1[4]{};
    float g_vCameraPosition[4]{};
    float g_vPackedConst6[4]{};
    float g_cColorTint[4]{};
    float g_vPackedConst8[4]{};
    float g_vGroundMinMax[2]{};
    uint8_t _pad0[8]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 112;
    static constexpr uint64_t kLayoutHash = 0xe01073ccb76d9678ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 5},
            {48, 16, 1, 0, 0, 0, 6},
            {64, 16, 1, 0, 0, 0, 7},
            {80, 16, 1, 0, 0, 0, 8},
            {96, 8, 1, 0, 0, 0, 9},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 7, entries };
        return map;
    }
};
static_assert(offsetof(aftershock_ps2x, g_mViewProj0) == 0, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_mViewProj1) == 16, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_vCameraPosition) == 32, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_vPackedConst6) == 48, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_cColorTint) == 64, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_vPackedConst8) == 80, "HLSL member offset");
static_assert(offsetof(aftershock_ps2x, g_vGroundMinMax) == 96, "HLSL member offset");
static_assert(sizeof(aftershock_ps2x) == 112, "HLSL cbuffer size");
} // namespace dx12cb
