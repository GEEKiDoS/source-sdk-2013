#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) DebugTextureView_ps2x {
    float g_vConst0[3]{};
    uint8_t _pad0[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0xf608865228887d93ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 0},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(DebugTextureView_ps2x, g_vConst0) == 0, "HLSL member offset");
static_assert(sizeof(DebugTextureView_ps2x) == 16, "HLSL cbuffer size");
} // namespace dx12cb
