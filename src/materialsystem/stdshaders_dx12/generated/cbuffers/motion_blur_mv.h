#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) motion_blur_mv {
    float g_vMotionBlurInternal[4]{};
    float g_vVelocityRegion[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x197b0494c02c9543ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(motion_blur_mv, g_vMotionBlurInternal) == 0, "HLSL member offset");
static_assert(offsetof(motion_blur_mv, g_vVelocityRegion) == 16, "HLSL member offset");
static_assert(sizeof(motion_blur_mv) == 32, "HLSL cbuffer size");
} // namespace dx12cb
