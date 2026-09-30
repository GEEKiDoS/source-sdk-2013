#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) pyro_vision_vs20 {
    float cBlendMaskTexCoordTransform[2][4]{};
    float g_vPyroParms1[4]{};
    float g_vPyroParms2[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x8e180ca8f1772becull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 14},
            {32, 16, 1, 0, 0, 0, 48},
            {48, 16, 1, 0, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(pyro_vision_vs20, cBlendMaskTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(pyro_vision_vs20, g_vPyroParms1) == 32, "HLSL member offset");
static_assert(offsetof(pyro_vision_vs20, g_vPyroParms2) == 48, "HLSL member offset");
static_assert(sizeof(pyro_vision_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
