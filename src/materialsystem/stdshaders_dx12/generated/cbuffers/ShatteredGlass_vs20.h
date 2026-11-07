#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) ShatteredGlass_vs20 {
    float cModulationColor[4]{};
    float cBaseTexCoordTransform[2][4]{};
    float cDetailTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0x685ebf4e626e9eb4ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 47},
            {16, 16, 2, 16, 0, 0, 48},
            {48, 16, 2, 16, 0, 0, 50},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(ShatteredGlass_vs20, cModulationColor) == 0, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_vs20, cBaseTexCoordTransform) == 16, "HLSL member offset");
static_assert(offsetof(ShatteredGlass_vs20, cDetailTexCoordTransform) == 48, "HLSL member offset");
static_assert(sizeof(ShatteredGlass_vs20) == 80, "HLSL cbuffer size");
} // namespace dx12cb
