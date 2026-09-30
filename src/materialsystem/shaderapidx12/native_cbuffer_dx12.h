#ifndef NATIVE_CBUFFER_DX12_H
#define NATIVE_CBUFFER_DX12_H

#include <cstddef>
#include <cstdint>
#include <limits>

namespace dx12native {
static constexpr int kDX12NativeCBufferPointerVar = (std::numeric_limits<int>::min)() + 0x12012;
static constexpr uint32_t kDX12NativeCBufferWriteMagic = 0x3242434Eu;
static constexpr uint32_t kDX12NativeCBufferVersion = 1;
enum : uint32_t { kStageVertex = 0, kStagePixel = 1 };
enum : uint8_t { kLegacyFloat = 0, kLegacyInt = 1, kLegacyBool = 2 };

struct NativeCBufferLegacyEntryDX12 {
    uint32_t byteOffset;
    uint16_t elementBytes;
    uint16_t elementCount;
    uint16_t srcStride;
    uint8_t bank;
    uint8_t component;
    uint16_t reg;
};
struct NativeCBufferLegacyMapDX12 {
    uint64_t layoutHash;
    uint32_t entryCount;
    const NativeCBufferLegacyEntryDX12 *entries;
};
struct NativeCBufferWriteDX12 {
    uint32_t magic, version, stage, registerSpace, shaderRegister, byteSize;
    uint64_t layoutHash;
    const void *data;
    const NativeCBufferLegacyMapDX12 *legacyMap;
};

static constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
static constexpr uint64_t kFnvPrime = 0x100000001b3ull;
constexpr uint64_t HashByte(uint64_t hash, unsigned char value) {
    return (hash ^ value) * kFnvPrime;
}
constexpr uint64_t HashString(const char *str, uint64_t hash = kFnvOffset) {
    return *str ? HashString(str + 1, HashByte(hash, static_cast<unsigned char>(*str))) : hash;
}
inline uint64_t HashBytes(const char *bytes, size_t size, uint64_t hash = kFnvOffset) {
    for (size_t i = 0; i < size; ++i)
        hash = HashByte(hash, static_cast<unsigned char>(bytes[i]));
    return hash;
}
}

#endif
