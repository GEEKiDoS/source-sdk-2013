#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) shadow_vs20 {
    float cBaseTexCoordTransform[2][4]{};
    float cTextureJitter[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x7871b05f5e3015d5ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 48},
            {32, 16, 2, 16, 0, 0, 50},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(shadow_vs20, cBaseTexCoordTransform) == 0, "HLSL member offset");
static_assert(offsetof(shadow_vs20, cTextureJitter) == 32, "HLSL member offset");
static_assert(sizeof(shadow_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
