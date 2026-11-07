#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) shadow_ps2x {
    float g_ShadowColor[4]{};
    float g_EyePos[3]{};
    uint8_t _pad0[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0xec7b5eaf2b2cd4c1ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 1},
            {16, 12, 1, 0, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(shadow_ps2x, g_ShadowColor) == 0, "HLSL member offset");
static_assert(offsetof(shadow_ps2x, g_EyePos) == 16, "HLSL member offset");
static_assert(sizeof(shadow_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
