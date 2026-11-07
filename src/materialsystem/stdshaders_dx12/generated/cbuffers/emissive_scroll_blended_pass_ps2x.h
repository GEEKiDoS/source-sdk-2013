#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) emissive_scroll_blended_pass_ps2x {
    float g_vPackedConst0[4]{};
    float g_vEmissiveScrollVector[2]{};
    uint8_t _pad0[8]{};
    float g_cSelfIllumTint[3]{};
    uint8_t _pad1[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0x877470aec88d9688ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 8, 1, 0, 0, 0, 1},
            {32, 12, 1, 0, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(emissive_scroll_blended_pass_ps2x, g_vPackedConst0) == 0, "HLSL member offset");
static_assert(offsetof(emissive_scroll_blended_pass_ps2x, g_vEmissiveScrollVector) == 16, "HLSL member offset");
static_assert(offsetof(emissive_scroll_blended_pass_ps2x, g_cSelfIllumTint) == 32, "HLSL member offset");
static_assert(sizeof(emissive_scroll_blended_pass_ps2x) == 48, "HLSL cbuffer size");
} // namespace dx12cb
