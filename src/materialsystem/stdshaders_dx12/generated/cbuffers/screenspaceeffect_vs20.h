#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) screenspaceeffect_vs20 {
    float Texel_Sizes[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0x71b2317bd4e9a9feull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(screenspaceeffect_vs20, Texel_Sizes) == 0, "HLSL member offset");
static_assert(sizeof(screenspaceeffect_vs20) == 16, "HLSL cbuffer size");
} // namespace dx12cb
