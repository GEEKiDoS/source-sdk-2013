#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) pyro_vision_ps2x {
    float g_vPyroParms1[4]{};
    float g_vPyroParms2[4]{};
    float g_vPyroParms3[4]{};
    float g_vPyroParms4[4]{};
    float g_vPyroParms5[4]{};
    float g_vPyroParms6[4]{};
    float g_vPyroParms7[4]{};
    float g_vPyroParms8[4]{};
    float g_EyePos[3]{};
    uint8_t _pad0[4]{};
    float g_vGeneralPyroParms1[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 160;
    static constexpr uint64_t kLayoutHash = 0xdbbfba74f28626abull;
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
            {128, 12, 1, 0, 0, 0, 10},
            {144, 16, 1, 0, 0, 0, 12},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 10, entries };
        return map;
    }
};
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms1) == 0, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms2) == 16, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms3) == 32, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms4) == 48, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms5) == 64, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms6) == 80, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms7) == 96, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vPyroParms8) == 112, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_EyePos) == 128, "HLSL member offset");
static_assert(offsetof(pyro_vision_ps2x, g_vGeneralPyroParms1) == 144, "HLSL member offset");
static_assert(sizeof(pyro_vision_ps2x) == 160, "HLSL cbuffer size");
} // namespace dx12cb
