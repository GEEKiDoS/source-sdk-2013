#pragma once
#include <cstddef>
#include <cstdint>
#include "native_cbuffer_dx12.h"
namespace dx12cb {
struct alignas(16) WorldVertexTransitionEditorMaterial {
    float cEditorBaseTransform[2][4]{};
    float cEditorBase2Transform[2][4]{};
    static constexpr uint32_t kStage = 0, kRegister = 2, kSize = 64;
    static constexpr uint64_t kLayoutHash = 0xf9ed5e8c47e8ccd4ull;
    static const dx12native::NativeCBufferLegacyMapDX12 &LegacyMap() {
        static const dx12native::NativeCBufferLegacyEntryDX12 entries[] = {
            {0, 16, 2, 16, 0, 0, 0},
            {32, 16, 2, 16, 0, 0, 2},
        };
        static const dx12native::NativeCBufferLegacyMapDX12 map = { kLayoutHash, 2, entries };
        return map;
    }
};
static_assert(offsetof(WorldVertexTransitionEditorMaterial, cEditorBaseTransform) == 0, "HLSL member offset");
static_assert(offsetof(WorldVertexTransitionEditorMaterial, cEditorBase2Transform) == 32, "HLSL member offset");
static_assert(sizeof(WorldVertexTransitionEditorMaterial) == 64, "HLSL cbuffer size");
} // namespace dx12cb
