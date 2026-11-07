#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) WaterCheap_ps2x {
    float g_WaterFogColor[3]{};
    uint8_t _pad0[4]{};
    float g_CheapWaterParams[4]{};
    float g_ReflectTint[4]{};
    float g_EyePos[3]{};
    uint8_t _pad1[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x4f1c65bc08737e89ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 12, 1, 0, 0, 0, 4},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(WaterCheap_ps2x, g_WaterFogColor) == 0, "HLSL member offset");
static_assert(offsetof(WaterCheap_ps2x, g_CheapWaterParams) == 16, "HLSL member offset");
static_assert(offsetof(WaterCheap_ps2x, g_ReflectTint) == 32, "HLSL member offset");
static_assert(offsetof(WaterCheap_ps2x, g_EyePos) == 48, "HLSL member offset");
static_assert(sizeof(WaterCheap_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
