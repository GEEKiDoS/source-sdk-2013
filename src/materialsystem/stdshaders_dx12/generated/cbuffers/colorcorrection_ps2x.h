#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) colorcorrection_ps2x {
    float ColorCorrectionDefaultWeight[1]{};
    float ColorCorrectionVolumeWeight0[1]{};
    float ColorCorrectionVolumeWeight1[1]{};
    float ColorCorrectionVolumeWeight2[1]{};
    float ColorCorrectionVolumeWeight3[1]{};
    uint8_t _pad0[12]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0xf988aec629109b00ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 4, 1, 0, 0, 0, 0},
            {4, 4, 1, 0, 0, 0, 1},
            {8, 4, 1, 0, 0, 0, 2},
            {12, 4, 1, 0, 0, 0, 3},
            {16, 4, 1, 0, 0, 0, 4},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(colorcorrection_ps2x, ColorCorrectionDefaultWeight) == 0, "HLSL member offset");
static_assert(offsetof(colorcorrection_ps2x, ColorCorrectionVolumeWeight0) == 4, "HLSL member offset");
static_assert(offsetof(colorcorrection_ps2x, ColorCorrectionVolumeWeight1) == 8, "HLSL member offset");
static_assert(offsetof(colorcorrection_ps2x, ColorCorrectionVolumeWeight2) == 12, "HLSL member offset");
static_assert(offsetof(colorcorrection_ps2x, ColorCorrectionVolumeWeight3) == 16, "HLSL member offset");
static_assert(sizeof(colorcorrection_ps2x) == 32, "HLSL cbuffer size");
} // namespace dx12cb
