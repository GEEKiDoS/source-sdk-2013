#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) motion_blur_ps2x {
    float g_flMaxMotionBlur[1]{};
    uint8_t _pad0[12]{};
    float g_vConst5[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x43dce61229d8d2f8ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 4, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(motion_blur_ps2x, g_flMaxMotionBlur) == 0, "HLSL member offset");
static_assert(offsetof(motion_blur_ps2x, g_vConst5) == 16, "HLSL member offset");
static_assert(sizeof(motion_blur_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
