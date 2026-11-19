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
    float gLens[4]{};
    float gCamera[4]{};
    float gSensor[4]{};
    float gAwbParams[4]{};
    float gViewRect[4]{};
    float gBodycam[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0x81f4a3dbc690804full;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 16, 1, 0, 0, 0, 5},
            {96, 16, 1, 0, 0, 0, 6},
            {112, 16, 1, 0, 0, 0, 7},
            {128, 16, 1, 0, 0, 0, 8},
            {144, 16, 1, 0, 0, 0, 9},
            {160, 16, 1, 0, 0, 0, 10},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 11, entries };
        return map;
    }
};
static_assert(offsetof(PostComposite, gSceneBloomFlare) == 0, "HLSL member offset");
static_assert(offsetof(PostComposite, gDirtPeak) == 16, "HLSL member offset");
static_assert(offsetof(PostComposite, gOutput) == 32, "HLSL member offset");
static_assert(offsetof(PostComposite, gLutWeights) == 48, "HLSL member offset");
static_assert(offsetof(PostComposite, gLutWeight4) == 64, "HLSL member offset");
static_assert(offsetof(PostComposite, gLens) == 80, "HLSL member offset");
static_assert(offsetof(PostComposite, gCamera) == 96, "HLSL member offset");
static_assert(offsetof(PostComposite, gSensor) == 112, "HLSL member offset");
static_assert(offsetof(PostComposite, gAwbParams) == 128, "HLSL member offset");
static_assert(offsetof(PostComposite, gViewRect) == 144, "HLSL member offset");
static_assert(offsetof(PostComposite, gBodycam) == 160, "HLSL member offset");
static_assert(sizeof(PostComposite) == 176, "HLSL cbuffer size");
} // namespace dx12cb
