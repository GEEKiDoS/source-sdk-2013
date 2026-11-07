#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) flesh_interior_blended_pass_vs20 {
    float g_vConst0[4]{};
    float g_vEffectCenterOoRadius1[4]{};
    float g_vEffectCenterOoRadius2[4]{};
    float g_vEffectCenterOoRadius3[4]{};
    float g_vEffectCenterOoRadius4[4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 80;
    static constexpr uint64_t kLayoutHash = 0xa05a3bf6984448deull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 48},
            {16, 16, 1, 0, 0, 0, 49},
            {32, 16, 1, 0, 0, 0, 50},
            {48, 16, 1, 0, 0, 0, 51},
            {64, 16, 1, 0, 0, 0, 52},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 5, entries };
        return map;
    }
};
static_assert(offsetof(flesh_interior_blended_pass_vs20, g_vConst0) == 0, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_vs20, g_vEffectCenterOoRadius1) == 16, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_vs20, g_vEffectCenterOoRadius2) == 32, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_vs20, g_vEffectCenterOoRadius3) == 48, "HLSL member offset");
static_assert(offsetof(flesh_interior_blended_pass_vs20, g_vEffectCenterOoRadius4) == 64, "HLSL member offset");
static_assert(sizeof(flesh_interior_blended_pass_vs20) == 80, "HLSL cbuffer size");
} // namespace dx12cb
