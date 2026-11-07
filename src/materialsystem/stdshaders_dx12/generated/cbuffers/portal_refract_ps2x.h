#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) portal_refract_ps2x {
    float g_mViewProj0[4]{};
    float g_mViewProj1[4]{};
    float g_mViewProj2[4]{};
    float g_mViewProj3[4]{};
    float g_vConst4[3]{};
    uint8_t _pad0[4]{};
    float g_vCameraPosition[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x146c879958674667ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 12, 1, 0, 0, 0, 4},
            {80, 16, 1, 0, 0, 0, 5},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(portal_refract_ps2x, g_mViewProj0) == 0, "HLSL member offset");
static_assert(offsetof(portal_refract_ps2x, g_mViewProj1) == 16, "HLSL member offset");
static_assert(offsetof(portal_refract_ps2x, g_mViewProj2) == 32, "HLSL member offset");
static_assert(offsetof(portal_refract_ps2x, g_mViewProj3) == 48, "HLSL member offset");
static_assert(offsetof(portal_refract_ps2x, g_vConst4) == 64, "HLSL member offset");
static_assert(offsetof(portal_refract_ps2x, g_vCameraPosition) == 80, "HLSL member offset");
static_assert(sizeof(portal_refract_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
