#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) GtaoApply {
    float gApplyOriginFullSize[4]{};
    float gApplyAoSizeUnpack[4]{};
    float gApplyOptions[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0xab954e3331bba1c0ull;
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
static_assert(offsetof(GtaoApply, gApplyOriginFullSize) == 0, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyAoSizeUnpack) == 16, "HLSL member offset");
static_assert(offsetof(GtaoApply, gApplyOptions) == 32, "HLSL member offset");
static_assert(sizeof(GtaoApply) == 48, "HLSL cbuffer size");
} // namespace dx12cb
