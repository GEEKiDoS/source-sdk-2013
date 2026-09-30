#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) water_ps2x {
    float vRefractTint[4]{};
    float vReflectTint[4]{};
    float g_ReflectRefractScale[4]{};
    float g_WaterFogColor[4]{};
    float g_WaterFogParams[4]{};
    float g_EyePos[3]{};
    uint8_t _pad0[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x991ecf0654a35293ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 1},
            {16, 16, 1, 0, 0, 0, 4},
            {32, 16, 1, 0, 0, 0, 5},
            {48, 16, 1, 0, 0, 0, 6},
            {64, 16, 1, 0, 0, 0, 7},
            {80, 12, 1, 0, 0, 0, 9},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(water_ps2x, vRefractTint) == 0, "HLSL member offset");
static_assert(offsetof(water_ps2x, vReflectTint) == 16, "HLSL member offset");
static_assert(offsetof(water_ps2x, g_ReflectRefractScale) == 32, "HLSL member offset");
static_assert(offsetof(water_ps2x, g_WaterFogColor) == 48, "HLSL member offset");
static_assert(offsetof(water_ps2x, g_WaterFogParams) == 64, "HLSL member offset");
static_assert(offsetof(water_ps2x, g_EyePos) == 80, "HLSL member offset");
static_assert(sizeof(water_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
