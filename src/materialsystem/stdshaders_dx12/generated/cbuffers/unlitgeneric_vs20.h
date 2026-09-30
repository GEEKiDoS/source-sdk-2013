#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) unlitgeneric_vs20 {
    float cModulationColor[4]{};
    float cBaseTextureTransform[2][4]{};
    float cMaskTextureTransform[2][4]{};
    float cDetailTextureTransform[2][4]{};
    float g_vVertexColor[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 128;
    static constexpr uint64_t kLayoutHash = 0x311956c7798b7da7ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 47},
            {16, 16, 2, 16, 0, 0, 48},
            {48, 16, 2, 16, 0, 0, 50},
            {80, 16, 2, 16, 0, 0, 52},
            {112, 16, 1, 0, 0, 0, 54},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(unlitgeneric_vs20, cModulationColor) == 0, "HLSL member offset");
static_assert(offsetof(unlitgeneric_vs20, cBaseTextureTransform) == 16, "HLSL member offset");
static_assert(offsetof(unlitgeneric_vs20, cMaskTextureTransform) == 48, "HLSL member offset");
static_assert(offsetof(unlitgeneric_vs20, cDetailTextureTransform) == 80, "HLSL member offset");
static_assert(offsetof(unlitgeneric_vs20, g_vVertexColor) == 112, "HLSL member offset");
static_assert(sizeof(unlitgeneric_vs20) == 128, "HLSL cbuffer size");
} // namespace dx12cb
