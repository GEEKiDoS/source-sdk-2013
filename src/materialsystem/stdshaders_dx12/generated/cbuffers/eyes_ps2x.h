#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) eyes_ps2x {
    float cEyeScalars[4]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x6f5ae473d95c2915ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(eyes_ps2x, cEyeScalars) == 0, "HLSL member offset");
static_assert(offsetof(eyes_ps2x, g_EyePos_SpecExponent) == 16, "HLSL member offset");
static_assert(sizeof(eyes_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
