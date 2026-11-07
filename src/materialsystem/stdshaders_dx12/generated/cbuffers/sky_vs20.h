#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) sky_vs20 {
    float g_vTextureSizeInfo[4]{};
    float g_mBaseTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x7282ddf68967789eull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 2, 16, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(sky_vs20, g_vTextureSizeInfo) == 0, "HLSL member offset");
static_assert(offsetof(sky_vs20, g_mBaseTexCoordTransform) == 16, "HLSL member offset");
static_assert(sizeof(sky_vs20) == 48, "HLSL cbuffer size");
} // namespace dx12cb
