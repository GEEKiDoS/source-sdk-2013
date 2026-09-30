#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) monitorscreen_ps2x {
    float g_Contrast[4]{};
    float g_Saturation[4]{};
    float g_Tint[4]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x00facc2b67e831bdull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 1},
            {16, 16, 1, 0, 0, 0, 2},
            {32, 16, 1, 0, 0, 0, 3},
            {48, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(monitorscreen_ps2x, g_Contrast) == 0, "HLSL member offset");
static_assert(offsetof(monitorscreen_ps2x, g_Saturation) == 16, "HLSL member offset");
static_assert(offsetof(monitorscreen_ps2x, g_Tint) == 32, "HLSL member offset");
static_assert(offsetof(monitorscreen_ps2x, g_EyePos_SpecExponent) == 48, "HLSL member offset");
static_assert(sizeof(monitorscreen_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
