#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) particlesphere_vs20 {
    float cCustomConstants[6][4]{};
    float g_vLightPosition[4]{};
    float g_vLightColor[4]{};
    float g_flLightIntensity[1]{};
    uint8_t _pad0[12]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 144;
    static constexpr uint64_t kLayoutHash = 0x67c3882d72dea0e7ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 6, 16, 0, 0, 48},
            {96, 16, 1, 0, 0, 0, 48},
            {112, 16, 1, 0, 0, 0, 49},
            {128, 4, 1, 0, 0, 0, 50},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(particlesphere_vs20, cCustomConstants) == 0, "HLSL member offset");
static_assert(offsetof(particlesphere_vs20, g_vLightPosition) == 96, "HLSL member offset");
static_assert(offsetof(particlesphere_vs20, g_vLightColor) == 112, "HLSL member offset");
static_assert(offsetof(particlesphere_vs20, g_flLightIntensity) == 128, "HLSL member offset");
static_assert(sizeof(particlesphere_vs20) == 144, "HLSL cbuffer size");
} // namespace dx12cb
