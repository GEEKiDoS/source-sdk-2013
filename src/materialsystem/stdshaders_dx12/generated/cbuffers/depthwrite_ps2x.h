#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) depthwrite_ps2x {
    float g_AlphaThreshold[1]{};
    float g_vNearFarPlanes[2]{};
    uint8_t _pad0[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0x4c44ef8e30256492ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 4, 1, 0, 0, 0, 0},
            {4, 8, 1, 0, 0, 0, 1},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(depthwrite_ps2x, g_AlphaThreshold) == 0, "HLSL member offset");
static_assert(offsetof(depthwrite_ps2x, g_vNearFarPlanes) == 4, "HLSL member offset");
static_assert(sizeof(depthwrite_ps2x) == 16, "HLSL cbuffer size");
} // namespace dx12cb
