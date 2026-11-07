#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) portal_vs20 {
    float g_CustomViewProj[4][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0xaa4694ce4a9f9aa0ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 4, 16, 0, 0, 48},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 1, entries };
        return map;
    }
};
static_assert(offsetof(portal_vs20, g_CustomViewProj) == 0, "HLSL member offset");
static_assert(sizeof(portal_vs20) == 64, "HLSL cbuffer size");
} // namespace dx12cb
