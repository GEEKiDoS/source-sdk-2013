#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) eye_refract_ps2x {
    float g_vPackedConst0[4]{};
    float g_vEyeOrigin[3]{};
    uint8_t _pad0[4]{};
    float g_vIrisProjectionU[4]{};
    float g_vIrisProjectionV[4]{};
    float g_vCameraPosition[4]{};
    float g_cAmbientOcclColor[3]{};
    uint8_t _pad1[4]{};
    float g_vPackedConst6[4]{};
    float g_vFlashlightAttenuationFactors[4]{};
    float g_vFlashlightPos[3]{};
    uint8_t _pad2[4]{};
    float g_vShadowTweaks[4]{};
    float cFlashlightColor[4]{};
    float cFlashlightScreenScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 192;
    static constexpr uint64_t kLayoutHash = 0x8b147f7ec4bc365dull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 12, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
            {48, 16, 1, 0, 0, 0, 3},
            {64, 16, 1, 0, 0, 0, 4},
            {80, 12, 1, 0, 0, 0, 5},
            {96, 16, 1, 0, 0, 0, 6},
            {112, 16, 1, 0, 0, 0, 7},
            {128, 12, 1, 0, 0, 0, 8},
            {144, 16, 1, 0, 0, 0, 9},
            {160, 16, 1, 0, 0, 0, 28},
            {176, 16, 1, 0, 0, 0, 31},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 12, entries };
        return map;
    }
};
static_assert(offsetof(eye_refract_ps2x, g_vPackedConst0) == 0, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vEyeOrigin) == 16, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vIrisProjectionU) == 32, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vIrisProjectionV) == 48, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vCameraPosition) == 64, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_cAmbientOcclColor) == 80, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vPackedConst6) == 96, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vFlashlightAttenuationFactors) == 112, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vFlashlightPos) == 128, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, g_vShadowTweaks) == 144, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, cFlashlightColor) == 160, "HLSL member offset");
static_assert(offsetof(eye_refract_ps2x, cFlashlightScreenScale) == 176, "HLSL member offset");
static_assert(sizeof(eye_refract_ps2x) == 192, "HLSL cbuffer size");
} // namespace dx12cb
