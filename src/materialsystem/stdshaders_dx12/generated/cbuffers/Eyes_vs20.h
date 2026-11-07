#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) Eyes_vs20 {
    float cEyeOrigin[3]{};
    uint8_t _pad0[4]{};
    float cHalfEyeballUp[3]{};
    uint8_t _pad1[4]{};
    float cIrisProjectionU[4]{};
    float cIrisProjectionV[4]{};
    float cGlintProjectionU[4]{};
    float cGlintProjectionV[4]{};
    float const4[4]{};
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad2[4]{};
    float cMorphSubrect[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 144;
    static constexpr uint64_t kLayoutHash = 0xf07b5156705f3b13ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 48},
            {16, 12, 1, 0, 0, 0, 49},
            {32, 16, 1, 0, 0, 0, 50},
            {48, 16, 1, 0, 0, 0, 51},
            {64, 16, 1, 0, 0, 0, 52},
            {80, 16, 1, 0, 0, 0, 53},
            {96, 16, 1, 0, 0, 0, 54},
            {112, 12, 1, 0, 0, 0, 55},
            {128, 16, 1, 0, 0, 0, 56},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 9, entries };
        return map;
    }
};
static_assert(offsetof(Eyes_vs20, cEyeOrigin) == 0, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cHalfEyeballUp) == 16, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cIrisProjectionU) == 32, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cIrisProjectionV) == 48, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cGlintProjectionU) == 64, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cGlintProjectionV) == 80, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, const4) == 96, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cMorphTargetTextureDim) == 112, "HLSL member offset");
static_assert(offsetof(Eyes_vs20, cMorphSubrect) == 128, "HLSL member offset");
static_assert(sizeof(Eyes_vs20) == 144, "HLSL cbuffer size");
} // namespace dx12cb
