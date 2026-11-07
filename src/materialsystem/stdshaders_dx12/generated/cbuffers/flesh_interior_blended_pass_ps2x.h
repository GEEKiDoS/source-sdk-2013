#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) flesh_interior_blended_pass_ps2x {
    float g_cSubsurfaceTint[3]{};
    uint8_t _pad0[4]{};
    float g_flBorderWidth[2]{};
    float g_flBorderSoftness[1]{};
    uint8_t _pad1[4]{};
    float g_cBorderTint[3]{};
    float g_flGlobalOpacity[1]{};
    float g_flGlossBrightness[1]{};
    uint8_t _pad2[12]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0xf553135d7757c125ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
            {16, 8, 1, 0, 0, 0, 1},
            {24, 4, 1, 0, 0, 0, 2},
            {32, 12, 1, 0, 0, 0, 3},
            {44, 4, 1, 0, 0, 0, 4},
            {48, 4, 1, 0, 0, 0, 5},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_cSubsurfaceTint) == 0, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_flBorderWidth) == 16, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_flBorderSoftness) == 24, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_cBorderTint) == 32, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_flGlobalOpacity) == 44, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_ps2x, g_flGlossBrightness) == 48, "HLSL member offset");
static_assert(sizeof(flesh_interior_blended_pass_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
