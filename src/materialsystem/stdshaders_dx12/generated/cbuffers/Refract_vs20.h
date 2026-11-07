#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) Refract_vs20 {
    float cBumpTexCoordTransform[4][4]{};
    float g_flTime[1]{};
    uint8_t _pad0[12]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0x6de2a16852828dc9ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 4, 16, 0, 0, 49},
            {64, 4, 1, 0, 0, 0, 53},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(Refract_vs20, cBumpTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(Refract_vs20, g_flTime) == 64, "HLSL member offset");
static_assert(sizeof(Refract_vs20) == 80, "HLSL cbuffer size");
} // namespace dx12cb
