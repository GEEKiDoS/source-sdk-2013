#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) refract_ps2x {
    float g_EnvmapTint[3]{};
    uint8_t _pad0[4]{};
    float g_RefractTint[3]{};
    uint8_t _pad1[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad2[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad3[4]{};
    float g_c5[4]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x859315df081fe6b6ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
            {16, 12, 1, 0, 0, 0, 1},
            {32, 12, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 5},
            {80, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(refract_ps2x, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(refract_ps2x, g_RefractTint) == 16, "HLSL member offset");
static_assert(offsetof(refract_ps2x, g_EnvmapContrast) == 32, "HLSL member offset");
static_assert(offsetof(refract_ps2x, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(refract_ps2x, g_c5) == 64, "HLSL member offset");
static_assert(offsetof(refract_ps2x, g_EyePos_SpecExponent) == 80, "HLSL member offset");
static_assert(sizeof(refract_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
