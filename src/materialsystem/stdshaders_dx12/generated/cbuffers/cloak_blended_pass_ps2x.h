#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) cloak_blended_pass_ps2x {
    float g_mViewProj0[4]{};
    float g_mViewProj1[4]{};
    float g_vCameraPosition[4]{};
    float g_vPackedConst6[4]{};
    float g_cCloakColorTint[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0xdd20d917cece3f69ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 5},
            {48, 16, 1, 0, 0, 0, 6},
            {64, 16, 1, 0, 0, 0, 7},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(cloak_blended_pass_ps2x, g_mViewProj0) == 0, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_ps2x, g_mViewProj1) == 16, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_ps2x, g_vCameraPosition) == 32, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_ps2x, g_vPackedConst6) == 48, "HLSL member offset");
static_assert(offsetof(cloak_blended_pass_ps2x, g_cCloakColorTint) == 64, "HLSL member offset");
static_assert(sizeof(cloak_blended_pass_ps2x) == 80, "HLSL cbuffer size");
} // namespace dx12cb
