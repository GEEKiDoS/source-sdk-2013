#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) cloud_vs20 {
    float g_matBaseTexCoordTransform[2][4]{};
    float g_matCloudTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x801269b681957e54ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 16, 2, 16, 0, 0, 50},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(cloud_vs20, g_matBaseTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(cloud_vs20, g_matCloudTexCoordTransform) == 32, "HLSL member offset");
static_assert(sizeof(cloud_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
