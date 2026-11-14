#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) l4d2_spritecard_ps {
    float g_Parameters[4]{};
    float g_vCardDisplayCurve[4]{};
    float g_vCardDepthFeathering[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x9bf8f02429b1dde0ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(l4d2_spritecard_ps, g_Parameters) == 0, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_ps, g_vCardDisplayCurve) == 16, "HLSL member offset");
static_assert(offsetof(l4d2_spritecard_ps, g_vCardDepthFeathering) == 32, "HLSL member offset");
static_assert(sizeof(l4d2_spritecard_ps) == 48, "HLSL cbuffer size");
} // namespace dx12cb
