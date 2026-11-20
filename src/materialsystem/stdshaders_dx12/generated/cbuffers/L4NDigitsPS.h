#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) L4NDigitsPS {
    float g_DigitModulation[4]{};
    float g_DigitValues[4]{};
    float g_DigitLayout[4]{};
    static constexpr uint32_t kStage = 1, kRegister = 1, kSize = 48;
    static constexpr uint64_t kLayoutHash = 0xf98a424d03084da5ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 1, 0, 0, 0, 0},
            {16, 16, 1, 0, 0, 0, 1},
            {32, 16, 1, 0, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 3, entries };
        return map;
    }
};
static_assert(offsetof(L4NDigitsPS, g_DigitModulation) == 0, "HLSL member offset");
static_assert(offsetof(L4NDigitsPS, g_DigitValues) == 16, "HLSL member offset");
static_assert(offsetof(L4NDigitsPS, g_DigitLayout) == 32, "HLSL member offset");
static_assert(sizeof(L4NDigitsPS) == 48, "HLSL cbuffer size");
} // namespace dx12cb
