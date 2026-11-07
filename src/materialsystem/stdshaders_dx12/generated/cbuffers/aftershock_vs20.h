#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) aftershock_vs20 {
    float g_flTime[1]{};
    uint8_t _pad0[12]{};
    float cBaseTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x67ed4c8e1de54f0full;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 4, 1, 0, 0, 0, 48},
            {16, 16, 2, 16, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(aftershock_vs20, g_flTime) == 0, "HLSL member offset");
static_assert(offsetof(aftershock_vs20, cBaseTexCoordTransform) == 16, "HLSL member offset");
static_assert(sizeof(aftershock_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
