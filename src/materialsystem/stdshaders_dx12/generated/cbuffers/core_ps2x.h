#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) core_ps2x {
    float g_EnvmapTint[3]{};
    uint8_t _pad0[4]{};
    float g_RefractTint[3]{};
    uint8_t _pad1[4]{};
    float g_EnvmapContrast[3]{};
    uint8_t _pad2[4]{};
    float g_EnvmapSaturation[3]{};
    uint8_t _pad3[4]{};
    float g_RefractScale[2]{};
    float g_Time[1]{};
    uint8_t _pad4[4]{};
    float g_FlowScrollRate[2]{};
    uint8_t _pad5[8]{};
    float g_EyePos[3]{};
    float g_CoreColorTexCoordOffset[1]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 112;
    static constexpr uint64_t kLayoutHash = 0xd75171dd3250c8c9ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
            {16, 12, 1, 0, 0, 0, 1},
            {32, 12, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 3},
            {64, 8, 1, 0, 0, 0, 5},
            {72, 4, 1, 0, 0, 0, 6},
            {80, 8, 1, 0, 0, 0, 7},
            {96, 12, 1, 0, 0, 0, 8},
            {108, 4, 1, 0, 0, 0, 9},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 9, entries };
        return map;
    }
};
static_assert(offsetof(core_ps2x, g_EnvmapTint) == 0, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_RefractTint) == 16, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_EnvmapContrast) == 32, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_EnvmapSaturation) == 48, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_RefractScale) == 64, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_Time) == 72, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_FlowScrollRate) == 80, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_EyePos) == 96, "HLSL member offset");
static_assert(offsetof(core_ps2x, g_CoreColorTexCoordOffset) == 108, "HLSL member offset");
static_assert(sizeof(core_ps2x) == 112, "HLSL cbuffer size");
} // namespace dx12cb
