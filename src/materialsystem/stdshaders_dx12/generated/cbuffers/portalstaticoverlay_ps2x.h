#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) portalstaticoverlay_ps2x {
    float g_StaticAmount[3]{};
    uint8_t _pad0[4]{};
    float g_EyePos_SpecExponent[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x7faf97a8d242b85dull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 11},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(portalstaticoverlay_ps2x, g_StaticAmount) == 0, "HLSL member offset");
static_assert(offsetof(portalstaticoverlay_ps2x, g_EyePos_SpecExponent) == 16, "HLSL member offset");
static_assert(sizeof(portalstaticoverlay_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
