#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedreflective_ps2x {
    float vRefractTint[4]{};
    float g_FresnelConstants[4]{};
    float vReflectTint[4]{};
    float g_ReflectRefractScale[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0x247261cc23f9d1a6ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 1},
            {16, 16, 1, 0, 0, 0, 3},
            {32, 16, 1, 0, 0, 0, 4},
            {48, 16, 1, 0, 0, 0, 5},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 4, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedreflective_ps2x, vRefractTint) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedreflective_ps2x, g_FresnelConstants) == 16, "HLSL member offset");
static_assert(offsetof(lightmappedreflective_ps2x, vReflectTint) == 32, "HLSL member offset");
static_assert(offsetof(lightmappedreflective_ps2x, g_ReflectRefractScale) == 48, "HLSL member offset");
static_assert(sizeof(lightmappedreflective_ps2x) == 64, "HLSL cbuffer size");
} // namespace dx12cb
