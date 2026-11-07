#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) volume_clouds_vs20 {
    float g_vTime[3]{};
    uint8_t _pad0[4]{};
    float cBaseTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0xf1f0e8b5e2185096ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 48},
            {16, 16, 2, 16, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(volume_clouds_vs20, g_vTime) == 0, "HLSL member offset");
static_assert(offsetof(volume_clouds_vs20, cBaseTexCoordTransform) == 16, "HLSL member offset");
static_assert(sizeof(volume_clouds_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
