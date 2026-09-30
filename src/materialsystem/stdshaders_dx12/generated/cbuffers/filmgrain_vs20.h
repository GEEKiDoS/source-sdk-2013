#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) filmgrain_vs20 {
    float cFilmGrainOffset[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 16;
    static constexpr uint64_t kLayoutHash = 0x62d712a53ff5e03cull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(filmgrain_vs20, cFilmGrainOffset) == 0, "HLSL member offset");
static_assert(sizeof(filmgrain_vs20) == 16, "HLSL cbuffer size");
} // namespace dx12cb
