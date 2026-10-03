#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) PostComposite {
    float gSceneBloomFlare[4]{};
    float gDirtPeak[4]{};
    float gOutput[4]{};
    float gLutWeights[4]{};
    float gLutWeight4[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0xa55f5b6adbd800acull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(PostComposite, gSceneBloomFlare) == 0, "HLSL member offset");
static_assert(offsetof(PostComposite, gDirtPeak) == 16, "HLSL member offset");
static_assert(offsetof(PostComposite, gOutput) == 32, "HLSL member offset");
static_assert(offsetof(PostComposite, gLutWeights) == 48, "HLSL member offset");
static_assert(offsetof(PostComposite, gLutWeight4) == 64, "HLSL member offset");
static_assert(sizeof(PostComposite) == 80, "HLSL cbuffer size");
} // namespace dx12cb
