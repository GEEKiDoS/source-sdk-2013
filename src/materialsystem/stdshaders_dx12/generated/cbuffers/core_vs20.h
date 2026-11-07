#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) core_vs20 {
    float cBumpTexCoordTransform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x01bd8ed3cb941fb6ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(core_vs20, cBumpTexCoordTransform) == 0, "HLSL member offset");
static_assert(sizeof(core_vs20) == 32, "HLSL cbuffer size");
} // namespace dx12cb
