//========= Copyright Valve Corporation, All rights reserved. ============//
// Dense baked SH-L2 diffuse irradiance probes, paired to exactly one .hlight asset. The grid is a separate
// pak member (maps/<name>.hprobe); the .hlight format and the native leaf ambient lumps are not touched.
#ifndef HPROBE_BSP_H
#define HPROBE_BSP_H
#ifdef _WIN32
#pragma once
#endif

#include "hlight_bsp.h"
#include <math.h>
#include <string.h>

namespace hprobe
{
static const uint32 kMagic = 0x42525048u; // HPRB, little endian
static const uint32 kVersion = 1;
static const uint32 kEndian = 0x01020304u;
static const uint32 kBrickCells = 4;
static const uint32 kBrickProbes = 5; // cells + 1: edge probes are duplicated so trilinear never crosses a brick
static const uint32 kBands = 6; // RGBA8_SNORM textures: two per color channel, bands k = 1..4 and 5..8
static const uint32 kMaxBricksPerAxis = 1024;
static const uint32 kMaxAtlasBricksPerAxis = 256; // atlas side <= 1280 <= the D3D12 3D texture limit
static const uint32 kMaxBricks = 1u << 18;
static const uint32 kMinSpacing = 8;
static const uint32 kMaxSpacing = 512;
static const uint32 kMaxReach = 65536;
static const uint32 kMaxGridBytes = 256u << 20; // padded atlas plus indirection: the baker's and the runtime's budget
static const uint32 kR11G11B10Float = 26; // DXGI_FORMAT_R11G11B10_FLOAT
static const uint32 kRGBA8Snorm = 31; // DXGI_FORMAT_R8G8B8A8_SNORM
static const uint32 kR8Unorm = 61; // DXGI_FORMAT_R8_UNORM
static const uint8 kValid = 255;
static const uint32 kBytesPerProbe = 29; // 4 DC + 6*4 bands + 1 validity
// Canonical file order: header, modes, then per mode its grid section. Each section starts at the next
// 16-byte boundary with zero padding. Modes are unique and ordered by (faceLump, lightingLump).
// Real SH basis, k = 0..8: Y0=0.282095, Y1=0.488603 y, Y2=0.488603 z, Y3=0.488603 x, Y4=1.092548 xy,
// Y5=1.092548 yz, Y6=0.315392 (3z^2-1), Y7=1.092548 xz, Y8=0.546274 (x^2-y^2).
// DC = e_0*Y0 per channel; band s_k = clamp(e_k/max(DC,1e-6)*0.25,-1,1); E(n) = DC*(1+4*sum(s_k*Y_k(n))),
// in the engine's linear cube units. DC and bands are premultiplied by validity, so hardware
// filtering across valid/invalid neighbours renormalises by the filtered validity.
#pragma pack(push, 4)
struct FileHeader
{
    uint32 magic, version, headerBytes, endian;
    uint64 fileBytes;
    uint32 hlightCRC32, modeCount; // hlightCRC32 == hlight::FileHeader::crc32 of the paired asset
    uint64 modesOffset;
    uint32 crc32, reserved[5]; // CRC of the complete file with crc32 treated as zero
};
struct ModeDisk
{
    uint32 faceLump, lightingLump, facesCRC32, lightingCRC32; // identity of the paired hlight mode
    uint64 lightingBytes;
    uint64 gridOffset;
    uint32 brickCount;
    uint32 reserved[7];
};
struct GridDisk // first record of the grid section
{
    float origin[3]; // world position of probe (0,0,0) = min corner of indirection brick (0,0,0)
    float spacing; // integer-valued, kMinSpacing..kMaxSpacing
    uint32 brickDims[3]; // indirection extent in bricks
    uint32 brickCount; // allocated bricks == ModeDisk::brickCount
    uint32 atlasBricks[3]; // see AtlasBricks
    uint32 reach; // 0 = unlimited
    uint32 dcFormat, bandFormat, validityFormat;
    uint32 reserved;
};
#pragma pack(pop)

COMPILE_TIME_ASSERT(sizeof(FileHeader) == 64);
COMPILE_TIME_ASSERT(sizeof(ModeDisk) == 64);
COMPILE_TIME_ASSERT(sizeof(GridDisk) == 64);
COMPILE_TIME_ASSERT(offsetof(FileHeader, crc32) == offsetof(hlight::FileHeader, crc32));

// Grid section: GridDisk; uint32 indirection[bx*by*bz] (x fastest; hlight::kMissing or a slot < brickCount, every
// slot referenced once); uint32 dc[texels] (R11G11B10F); int8 bands[6][texels][4]; uint8 validity[texels].
// Texels are atlas probes, x fastest. Slot s occupies [a*5, a*5+5) per axis with
// a = (s % abx, (s / abx) % aby, s / (abx*aby)); texels of unallocated slots are zero.
struct ModeView
{
    const ModeDisk *record;
    const GridDisk *grid;
    const uint32 *indirection, *dc;
    const int8 *bands;
    const uint8 *validity;
};
struct FileView
{
    const FileHeader *header;
    ModeView mode[4];
};

inline uint64 IndirectionCount(const GridDisk &g)
{
    return uint64(g.brickDims[0]) * g.brickDims[1] * g.brickDims[2];
}
// x = min(n, 256), y = min(ceil(n/x), 256), z = ceil(n/(x*y)); n >= 1.
inline void AtlasBricks(uint32 brickCount, uint32 out[3])
{
    out[0] = brickCount < kMaxAtlasBricksPerAxis ? brickCount : kMaxAtlasBricksPerAxis;
    const uint32 rows = (brickCount + out[0] - 1) / out[0];
    out[1] = rows < kMaxAtlasBricksPerAxis ? rows : kMaxAtlasBricksPerAxis;
    out[2] = (brickCount + out[0] * out[1] - 1) / (out[0] * out[1]);
}
inline uint64 AtlasTexels(const GridDisk &g)
{
    return uint64(g.atlasBricks[0]) * g.atlasBricks[1] * g.atlasBricks[2] * (kBrickProbes * kBrickProbes * kBrickProbes);
}
inline uint64 GridBytes(const GridDisk &g)
{
    return sizeof(GridDisk) + 4 * IndirectionCount(g) + AtlasTexels(g) * kBytesPerProbe;
}
// Index into the atlas arrays of local probe (lx,ly,lz) in 0..4 of an allocated slot.
inline uint32 TexelIndex(const GridDisk &g, uint32 slot, uint32 lx, uint32 ly, uint32 lz)
{
    const uint32 abx = g.atlasBricks[0], aby = g.atlasBricks[1];
    const uint32 width = abx * kBrickProbes, height = aby * kBrickProbes;
    const uint32 x = (slot % abx) * kBrickProbes + lx, y = ((slot / abx) % aby) * kBrickProbes + ly,
        z = (slot / (abx * aby)) * kBrickProbes + lz;
    return x + width * (y + height * z);
}
// R11G11B10_FLOAT has no NaN/Inf: the three exponent fields must not be all ones.
inline bool FinitePackedDC(uint32 packed)
{
    return ((packed >> 6) & 31) != 31 && ((packed >> 17) & 31) != 31 && ((packed >> 27) & 31) != 31;
}

inline bool ValidationError(char *error, int bytes, const char *reason)
{
    if (error && bytes > 0) V_snprintf(error, bytes, "Hprobe: %s", reason);
    return false;
}
inline uint32 FileCRC32(const void *bytes, uint32 count)
{
    return hlight::FileCRC32(bytes, count);
}

inline bool ValidateGrid(const uint8 *base, uint32 count, uint64 &cursor, const ModeDisk &m, ModeView &v,
    char *error, int errorBytes)
{
    const uint64 at = (cursor + 15) & ~uint64(15);
    if (m.gridOffset != at || at + sizeof(GridDisk) > count)
        return ValidationError(error, errorBytes, "noncanonical grid section");
    const GridDisk &g = *reinterpret_cast<const GridDisk *>(base + at);
    uint32 atlas[3] = {};
    if (g.brickCount && g.brickCount <= kMaxBricks) AtlasBricks(g.brickCount, atlas);
    if (!ShadowMap_IsFiniteFloat(g.origin[0]) || !ShadowMap_IsFiniteFloat(g.origin[1]) || !ShadowMap_IsFiniteFloat(g.origin[2]) ||
        !ShadowMap_IsFiniteFloat(g.spacing) || g.spacing != floorf(g.spacing) ||
        g.spacing < float(kMinSpacing) || g.spacing > float(kMaxSpacing) ||
        !g.brickDims[0] || g.brickDims[0] > kMaxBricksPerAxis || !g.brickDims[1] || g.brickDims[1] > kMaxBricksPerAxis ||
        !g.brickDims[2] || g.brickDims[2] > kMaxBricksPerAxis || IndirectionCount(g) > (1u << 30) ||
        !g.brickCount || g.brickCount > kMaxBricks || g.brickCount != m.brickCount || g.brickCount > IndirectionCount(g) ||
        memcmp(g.atlasBricks, atlas, sizeof(atlas)) || g.reach > kMaxReach ||
        g.dcFormat != kR11G11B10Float || g.bandFormat != kRGBA8Snorm || g.validityFormat != kR8Unorm || g.reserved)
        return ValidationError(error, errorBytes, "invalid grid record");
    if (GridBytes(g) > kMaxGridBytes) return ValidationError(error, errorBytes, "grid exceeds the 256 MiB budget");
    if (!hlight::Section(base, count, cursor, m.gridOffset, GridBytes(g)))
        return ValidationError(error, errorBytes, "grid section range");
    const uint64 cells = IndirectionCount(g), texels = AtlasTexels(g);
    v.grid = &g;
    v.indirection = reinterpret_cast<const uint32 *>(base + at + sizeof(GridDisk));
    v.dc = v.indirection + cells;
    v.bands = reinterpret_cast<const int8 *>(v.dc + texels);
    v.validity = reinterpret_cast<const uint8 *>(v.bands) + texels * kBands * 4;
    uint32 referenced[kMaxBricks / 32]; memset(referenced, 0, sizeof(referenced));
    uint32 slots = 0;
    for (uint64 i = 0; i < cells; ++i)
    {
        const uint32 slot = v.indirection[i];
        if (slot == hlight::kMissing) continue;
        if (slot >= g.brickCount || (referenced[slot >> 5] & (1u << (slot & 31))))
            return ValidationError(error, errorBytes, "indirection slot out of range or referenced twice");
        referenced[slot >> 5] |= 1u << (slot & 31); ++slots;
    }
    if (slots != g.brickCount) return ValidationError(error, errorBytes, "unreferenced brick slot");
    const uint32 abx = atlas[0], aby = atlas[1], width = abx * kBrickProbes, height = aby * kBrickProbes;
    const uint32 depth = atlas[2] * kBrickProbes;
    uint32 t = 0;
    for (uint32 z = 0; z < depth; ++z)
        for (uint32 y = 0; y < height; ++y)
            for (uint32 x = 0; x < width; ++x, ++t)
            {
                const uint64 slot = uint64(x / kBrickProbes) + uint64(y / kBrickProbes) * abx + uint64(z / kBrickProbes) * abx * aby;
                bool bandsZero = true, bandsRepresentable = true;
                for (uint32 b = 0; b < kBands; ++b)
                    for (uint32 j = 0; j < 4; ++j)
                    {
                        const int8 band = v.bands[(uint64(b) * texels + t) * 4 + j];
                        bandsZero = bandsZero && !band;
                        bandsRepresentable = bandsRepresentable && band != -128;
                    }
                const bool valid = slot < g.brickCount && v.validity[t] == kValid;
                if (valid ? (!FinitePackedDC(v.dc[t]) || !bandsRepresentable) :
                    (v.validity[t] || v.dc[t] || !bandsZero))
                    return ValidationError(error, errorBytes, "invalid probe texel");
            }
    return true;
}

// Views borrow only the caller's owned file buffer (4-byte aligned). Validation is transactional:
// failure clears the view and provides a bounded error.
inline bool ValidateFile(const void *bytes, uint32 count, FileView &out, char *error, int errorBytes)
{
    memset(&out, 0, sizeof(out));
    if (error && errorBytes > 0) error[0] = 0;
    if (!bytes || count < sizeof(FileHeader) || count > hlight::kMaxFileBytes || (reinterpret_cast<uintp>(bytes) & 3))
        return ValidationError(error, errorBytes, "invalid file buffer");
    const uint8 *base = static_cast<const uint8 *>(bytes);
    const FileHeader &h = *reinterpret_cast<const FileHeader *>(base);
    if (h.magic != kMagic || h.version != kVersion || h.headerBytes != sizeof(h) || h.endian != kEndian ||
        h.fileBytes != count || !h.modeCount || h.modeCount > 4 || !hlight::ZeroBytes(h.reserved, sizeof(h.reserved)) ||
        h.crc32 != FileCRC32(bytes, count))
        return ValidationError(error, errorBytes, "header/version/CRC mismatch");
    FileView candidate; memset(&candidate, 0, sizeof(candidate));
    candidate.header = &h;
    uint64 cursor = sizeof(h);
    if (!hlight::Section(base, count, cursor, h.modesOffset, uint64(h.modeCount) * sizeof(ModeDisk)))
        return ValidationError(error, errorBytes, "noncanonical mode section");
    const ModeDisk *modes = reinterpret_cast<const ModeDisk *>(base + h.modesOffset);
    for (uint32 mi = 0; mi < h.modeCount; ++mi)
    {
        const ModeDisk &m = modes[mi];
        if (!hlight::ValidPair(m.faceLump, m.lightingLump) || m.lightingBytes > MAX_MAP_LIGHTING ||
            !hlight::ZeroBytes(m.reserved, sizeof(m.reserved)) ||
            (mi && (m.faceLump < modes[mi - 1].faceLump ||
                (m.faceLump == modes[mi - 1].faceLump && m.lightingLump <= modes[mi - 1].lightingLump))))
            return ValidationError(error, errorBytes, "mode identity/order mismatch");
        ModeView &v = candidate.mode[mi]; v.record = &m;
        if (!ValidateGrid(base, count, cursor, m, v, error, errorBytes)) return false;
    }
    if (cursor != count) return ValidationError(error, errorBytes, "trailing bytes");
    out = candidate;
    return true;
}
inline const ModeView *FindMode(const FileView &file, uint32 faceLump, uint32 lightingLump)
{
    if (!file.header) return NULL;
    for (uint32 i = 0; i < file.header->modeCount; ++i)
        if (file.mode[i].record->faceLump == faceLump && file.mode[i].record->lightingLump == lightingLump) return &file.mode[i];
    return NULL;
}
// Both views must have passed their individual validators.
inline bool ValidatePair(const FileView &probes, const hlight::FileView &lightmaps, char *error, int errorBytes)
{
    if (!probes.header || !lightmaps.header) return ValidationError(error, errorBytes, "unvalidated probe/lightmap pair");
    bool match = probes.header->hlightCRC32 == lightmaps.header->crc32 && probes.header->modeCount == lightmaps.header->modeCount;
    for (uint32 i = 0; match && i < probes.header->modeCount; ++i)
    {
        const ModeDisk &p = *probes.mode[i].record;
        const hlight::ModeView *l = hlight::FindMode(lightmaps, p.faceLump, p.lightingLump);
        match = l && p.facesCRC32 == l->record->facesCRC32 && p.lightingCRC32 == l->record->lightingCRC32 &&
            p.lightingBytes == l->record->lightingBytes;
    }
    return match || ValidationError(error, errorBytes, "probe asset does not match the lightmap asset");
}
// maps/<name>.hlight -> maps/<name>.hprobe
inline bool ProbeAssetPath(const char *hlightPath, char *out, int bytes)
{
    static const char from[] = ".hlight", to[] = ".hprobe"; // same length
    const int suffix = int(sizeof(from)) - 1;
    const int length = hlightPath ? int(strlen(hlightPath)) : 0;
    if (!out || length <= suffix || length >= bytes || V_stricmp(hlightPath + length - suffix, from)) return false;
    memcpy(out, hlightPath, length - suffix);
    memcpy(out + length - suffix, to, sizeof(to));
    return true;
}
} // namespace hprobe
#endif
