#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) Engine_Post_ps2x {
    float psTapOffs_Packed[4]{};
    float tweakables[4]{};
    float uvTransform[4]{};
    float ColorCorrectionDefaultWeight[1]{};
    uint8_t _pad0[12]{};
    float ColorCorrectionVolumeWeights[4]{};
    float BloomFactor[1]{};
    uint8_t _pad1[12]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 96;
    static constexpr uint64_t kLayoutHash = 0x8f24b699987fc0ebull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 4, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 4, 1, 0, 0, 0, 5},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 6, entries };
        return map;
    }
};
static_assert(offsetof(Engine_Post_ps2x, psTapOffs_Packed) == 0, "HLSL member offset");
static_assert(offsetof(Engine_Post_ps2x, tweakables) == 16, "HLSL member offset");
static_assert(offsetof(Engine_Post_ps2x, uvTransform) == 32, "HLSL member offset");
static_assert(offsetof(Engine_Post_ps2x, ColorCorrectionDefaultWeight) == 48, "HLSL member offset");
static_assert(offsetof(Engine_Post_ps2x, ColorCorrectionVolumeWeights) == 64, "HLSL member offset");
static_assert(offsetof(Engine_Post_ps2x, BloomFactor) == 80, "HLSL member offset");
static_assert(sizeof(Engine_Post_ps2x) == 96, "HLSL cbuffer size");
} // namespace dx12cb
