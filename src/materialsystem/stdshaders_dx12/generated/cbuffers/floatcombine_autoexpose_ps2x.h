#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) floatcombine_autoexpose_ps2x {
    float settings[4]{};
    float settings2[4]{};
    float settings3[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x949306c09276a2ceull;
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
static_assert(offsetof(floatcombine_autoexpose_ps2x, settings) == 0, "HLSL member offset");
static_assert(offsetof(floatcombine_autoexpose_ps2x, settings2) == 16, "HLSL member offset");
static_assert(offsetof(floatcombine_autoexpose_ps2x, settings3) == 32, "HLSL member offset");
static_assert(sizeof(floatcombine_autoexpose_ps2x) == 48, "HLSL cbuffer size");
} // namespace dx12cb
