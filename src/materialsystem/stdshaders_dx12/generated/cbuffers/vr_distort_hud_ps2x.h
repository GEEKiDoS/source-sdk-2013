#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) vr_distort_hud_ps2x {
    float DistortBounds[4]{};
    float bHudTranslucent[1]{};
    uint8_t _pad0[12]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0xfa311bc5e000e9a3ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 4, 1, 0, 0, 0, 1},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(vr_distort_hud_ps2x, DistortBounds) == 0, "HLSL member offset");
static_assert(offsetof(vr_distort_hud_ps2x, bHudTranslucent) == 16, "HLSL member offset");
static_assert(sizeof(vr_distort_hud_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
