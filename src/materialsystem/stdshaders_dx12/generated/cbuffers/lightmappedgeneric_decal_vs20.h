#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) lightmappedgeneric_decal_vs20 {
    float cShaderConst0[4]{};
    float cShaderConst1[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 32;
    static constexpr uint64_t kLayoutHash = 0x13fb6d65843c06a7ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 1, 0, 0, 0, 49},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(lightmappedgeneric_decal_vs20, cShaderConst0) == 0, "HLSL member offset");
static_assert(offsetof(lightmappedgeneric_decal_vs20, cShaderConst1) == 16, "HLSL member offset");
static_assert(sizeof(lightmappedgeneric_decal_vs20) == 32, "HLSL cbuffer size");
} // namespace dx12cb
