#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) eye_refract_vs20 {
    float cMorphTargetTextureDim[3]{};
    uint8_t _pad0[4]{};
    float cMorphSubrect[4]{};
    float g_cEyeOrigin[3]{};
    uint8_t _pad1[4]{};
    float g_vIrisProjectionU[4]{};
    float g_vIrisProjectionV[4]{};
    float g_vFlashlightPosition[4]{};
    float g_vConst4[4]{};
    float g_vFlashlightMatrixRow1[4]{};
    float g_vFlashlightMatrixRow2[4]{};
    float g_vFlashlightMatrixRow3[4]{};
    float g_vFlashlightMatrixRow4[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 176;
    static constexpr uint64_t kLayoutHash = 0xf117a70aa742d59full;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 12, 1, 0, 0, 0, 14},
            {16, 16, 1, 0, 0, 0, 15},
            {32, 12, 1, 0, 0, 0, 48},
            {48, 16, 1, 0, 0, 0, 50},
            {64, 16, 1, 0, 0, 0, 51},
            {80, 16, 1, 0, 0, 0, 52},
            {96, 16, 1, 0, 0, 0, 53},
            {112, 16, 1, 0, 0, 0, 54},
            {128, 16, 1, 0, 0, 0, 55},
            {144, 16, 1, 0, 0, 0, 56},
            {160, 16, 1, 0, 0, 0, 57},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 11, entries };
        return map;
    }
};
static_assert(offsetof(eye_refract_vs20, cMorphTargetTextureDim) == 0, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, cMorphSubrect) == 16, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_cEyeOrigin) == 32, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vIrisProjectionU) == 48, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vIrisProjectionV) == 64, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vFlashlightPosition) == 80, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vConst4) == 96, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vFlashlightMatrixRow1) == 112, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vFlashlightMatrixRow2) == 128, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vFlashlightMatrixRow3) == 144, "HLSL member offset");
static_assert(offsetof(eye_refract_vs20, g_vFlashlightMatrixRow4) == 160, "HLSL member offset");
static_assert(sizeof(eye_refract_vs20) == 176, "HLSL cbuffer size");
} // namespace dx12cb
