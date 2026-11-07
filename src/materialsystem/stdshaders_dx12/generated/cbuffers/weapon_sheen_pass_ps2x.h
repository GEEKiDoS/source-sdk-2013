#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) weapon_sheen_pass_ps2x {
    float g_mViewProj0[4]{};
    float g_mViewProj1[4]{};
    float g_vCameraPosition[4]{};
    float g_vPackedConst6[4]{};
    float g_vPackedConst7[4]{};
    float g_cCloakColorTint[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x2390c0d50e4a6190ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 5},
            {48, 16, 1, 0, 0, 0, 6},
            {64, 16, 1, 0, 0, 0, 7},
            {80, 16, 1, 0, 0, 0, 8},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(weapon_sheen_pass_ps2x, g_mViewProj0) == 0, "HLSL member offset");
static_assert(offsetof(weapon_sheen_pass_ps2x, g_mViewProj1) == 16, "HLSL member offset");
static_assert(offsetof(weapon_sheen_pass_ps2x, g_vCameraPosition) == 32, "HLSL member offset");
static_assert(offsetof(weapon_sheen_pass_ps2x, g_vPackedConst6) == 48, "HLSL member offset");
static_assert(offsetof(weapon_sheen_pass_ps2x, g_vPackedConst7) == 64, "HLSL member offset");
static_assert(offsetof(weapon_sheen_pass_ps2x, g_cCloakColorTint) == 80, "HLSL member offset");
static_assert(sizeof(weapon_sheen_pass_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
