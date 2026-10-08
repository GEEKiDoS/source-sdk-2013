//========= Copyright Valve Corporation, All rights reserved. ============//
// Independently sampled linear HDR lightmaps. No receiver geometry or encoded native colors.
#ifndef HLIGHT_BSP_H
#define HLIGHT_BSP_H
#ifdef _WIN32
#pragma once
#endif

#include "shadowmap_bsp.h"
#include "zip_uncompressed.h"

namespace hlight
{
static const uint32 kMagic = 0x54494c48u; // HLIT, little endian
static const uint32 kVersion = 4;
static const uint32 kEndian = 0x01020304u;
static const uint32 kManifestVersion = 5; // hybrid visibility; older enhanced assets require a rebake
static const uint32 kMissing = 0xffffffffu;
static const uint32 kDefaultDensity = 4;
static const uint32 kDefaultPageSize = 2048;
static const uint32 kMaxPageSize = 16384;
static const uint32 kMaxFileBytes = 0x7fffffffu; // BSP/IZip signed buffer-size contract
static const uint32 kMaxStyles = 4;
static const uint32 kLightstyleCount = 64; // native engine LightStyleValue domain
static const uint32 kPlaneSlots = 4;
static const uint32 kRGBA16Float = 10; // DXGI_FORMAT_R16G16B16A16_FLOAT
static const uint32 kFaceBumped = 1;
static const uint32 kFaceDisplacement = 2;
static const uint32 kFaceHasLighting = 4;
static const uint32 kFaceHasSun = 8;
static const uint32 kFaceHasBakedLocalDirect = 16;
static const uint32 kModelBakedPoseKnown = 1;
static const uint32 kVisibilityDense = 256;
static const uint32 kPropMeshHasBakedLocalDirect = 1;
static const uint32 kPropDirectAngularPlanes = 2; // Lambert, squared half-Lambert
// Canonical file order: header, modes, then per mode identities/models/faces/tiles/pages/pixels.
// Each section starts at the next 16-byte boundary; all padding/reserved bytes are zero.
// Mode pairs are unique and lexicographically ordered by (faceLump, lightingLump).
// Tiles are contiguous per page in non-overlapping shelf order. x/y denote the interior;
// each tile includes a replicated one-texel gutter. No mipmaps or compressed GPU formats.
#pragma pack(push, 4)
struct FileHeader
{
    uint32 magic, version, headerBytes, endian;
    uint64 fileBytes;
    uint32 density, modeCount;
    uint64 modesOffset;
    uint32 crc32, visibilitySetCount;
    uint64 visibilitySetsOffset;
    uint32 reserved[2]; // CRC of complete file with crc32 treated as zero
};
struct LumpIdentity
{
    uint32 lump, version;
    uint64 bytes;
    uint32 crc32, reserved;
};
struct ModeDisk
{
    uint32 faceLump, lightingLump, faceVersion, lightingVersion;
    uint64 faceBytes, lightingBytes;
    uint32 facesCRC32, lightingCRC32, faceCount, modelCount;
    uint64 facesOffset, modelsOffset, tilesOffset, pagesOffset, identitiesOffset;
    uint32 tileCount, pageCount, identityCount, reserved;
    uint64 reserved2;
    uint32 visibilitySetIndex, reserved3[3];
};
struct ModelDisk
{
    uint32 modelIndex, firstFace, faceCount, flags;
    float bakedModelToWorld[12]; // row-major 3x4; not inferred from current draw geometry
};
struct FaceDisk
{
    uint32 faceOrdinal, modelIndex;
    int32 nativeMins[2];
    uint32 nativeExtents[2];
    uint32 highWidth, highHeight;
    uint32 flags, styleCount;
    uint32 styles[kMaxStyles]; // authored outputs before RGBExp pruning; style 0 first
    uint32 tiles[kMaxStyles * kPlaneSlots]; // style*4 + plane; unused slots kMissing
    uint32 reserved[2];
};
struct TileDisk
{
    uint32 faceOrdinal, styleSlot, plane, page;
    uint32 x, y, width, height;
};
struct PageDisk
{
    uint32 width, height, format, reserved;
    uint32 firstTile, tileCount, reserved2[2];
    uint64 pixelsOffset, pixelsBytes; // little-endian, row-major RGBA16F; no row padding
};
// Visibility sections follow all RGB mode sections. A set may be shared only after
// whole-scene paired equality. Canonical light indices include the manifest sun slot.
struct VisibilitySetDisk
{
    uint32 selectedLightCount, selectedLightsCRC32, faceCount, entryCount;
    uint32 propCount, meshCount;
    int32 sunLightIndex;
    uint32 reserved;
    uint64 facesOffset, entriesOffset, propsOffset, meshesOffset, payloadOffset;
    uint64 payloadBytes;
    uint32 payloadCRC32, reserved2;
    uint64 faceSupportOffset, faceSupportBytes;
    uint64 unbakedFacesOffset, unbakedLightIndicesOffset;
    uint32 unbakedFaceCount, unbakedLightIndexCount;
};
struct FaceVisibilityDisk
{
    uint32 faceOrdinal, firstEntry, entryCount, reserved;
};
// Sparse style-overflow exceptions, sorted by faceOrdinal. Light-index ranges
// partition unbakedLightIndices with strictly increasing selected-local indices.
struct UnbakedFaceDisk
{
    uint32 faceOrdinal, firstLightIndex, lightCount;
};
struct VisibilityEntryDisk
{
    uint32 selectedLightIndex, encoding, payloadByteOffset, sampleCount;
};
struct PropVisibilityDisk
{
    uint32 staticPropOrdinal, modelChecksum, firstMesh, meshCount;
    uint64 poseIdentity, reserved;
};
struct PropMeshVisibilityDisk
{
    uint32 meshOrdinal, lod, vertexCount, firstEntry, entryCount, vertexOrderCRC32;
    uint32 directPayloadByteOffset, directPayloadBytes; // both zero: full runtime local lighting
};
// Inline visibility-payload block: header, RGBA16F[style][angularPlane][vertex],
// then sorted uint32 unbaked selected-local indices. RGB is unstyled normalized
// linear radiance; A is zero. Angular planes use the authored vertex normal.
struct PropDirectDisk
{
    uint32 flags, styleCount, styles[kMaxStyles], vertexCount, angularPlaneCount;
    uint32 radianceBytes, unbakedLightCount, reserved[2];
};
// The small native game-lump manifest retains selected-light transport metadata without
// shipping or invoking the abandoned receiver geometry/mask representation. assetPath
// names the exact BSP-pak member, so renaming a BSP does not change its embedded identity.
struct ManifestModeDisk
{
    uint32 faceLump, lightingLump, worldlightsLump;
    uint32 lightingBytes, lightingCRC32, facesCRC32, worldlightsCRC32;
    uint32 lightsOffset, lightCount;
    int32 sunLightIndex;
    uint32 assetMode;
    uint32 reserved[5];
};
struct ManifestDisk
{
    uint32 byteSize, runtimeModeMask, assetVersion, reserved;
    ManifestModeDisk mode[SHADOWMAP_MODE_COUNT];
    char assetPath[256];
};
#pragma pack(pop)

COMPILE_TIME_ASSERT(sizeof(FileHeader) == 64);
COMPILE_TIME_ASSERT(sizeof(LumpIdentity) == 24);
COMPILE_TIME_ASSERT(sizeof(ModeDisk) == 128);
COMPILE_TIME_ASSERT(sizeof(ModelDisk) == 64);
COMPILE_TIME_ASSERT(sizeof(FaceDisk) == 128);
COMPILE_TIME_ASSERT(sizeof(TileDisk) == 32);
COMPILE_TIME_ASSERT(sizeof(PageDisk) == 48);
COMPILE_TIME_ASSERT(sizeof(VisibilitySetDisk) == 128);
COMPILE_TIME_ASSERT(sizeof(FaceVisibilityDisk) == 16);
COMPILE_TIME_ASSERT(sizeof(UnbakedFaceDisk) == 12);
COMPILE_TIME_ASSERT(sizeof(VisibilityEntryDisk) == 16);
COMPILE_TIME_ASSERT(sizeof(PropVisibilityDisk) == 32);
COMPILE_TIME_ASSERT(sizeof(PropMeshVisibilityDisk) == 32);
COMPILE_TIME_ASSERT(sizeof(PropDirectDisk) == 48);
COMPILE_TIME_ASSERT(sizeof(ManifestModeDisk) == 64);
COMPILE_TIME_ASSERT(sizeof(ManifestDisk) == 400);

struct VisibilityView
{
    const VisibilitySetDisk *record;
    const FaceVisibilityDisk *faces;
    const VisibilityEntryDisk *entries;
    const PropVisibilityDisk *props;
    const PropMeshVisibilityDisk *meshes;
    const UnbakedFaceDisk *unbakedFaces;
    const uint32 *unbakedLightIndices;
    const uint8 *payload, *faceSupport;
};
struct PropDirectView
{
    const PropDirectDisk *record;
    const uint16 *pixels;
    const uint32 *unbakedLightIndices;
};
// VisibilityView/mesh must be admitted by ValidateFile before borrowing.
inline PropDirectView GetPropDirect(const VisibilityView &v, const PropMeshVisibilityDisk &mesh)
{
    PropDirectView out = {};
    if (!mesh.directPayloadBytes) return out;
    out.record = reinterpret_cast<const PropDirectDisk *>(v.payload+mesh.directPayloadByteOffset);
    out.pixels = reinterpret_cast<const uint16 *>(out.record+1);
    out.unbakedLightIndices = reinterpret_cast<const uint32 *>(reinterpret_cast<const uint8 *>(out.pixels)+out.record->radianceBytes);
    return out;
}
struct ModeView
{
    const ModeDisk *record;
    const LumpIdentity *identities;
    const ModelDisk *models;
    const FaceDisk *faces;
    const TileDisk *tiles;
    const PageDisk *pages;
    VisibilityView visibility;
};
struct FileView
{
    const uint8 *base;
    uint32 bytes;
    const FileHeader *header;
    ModeView mode[4]; // only explicitly declared face/lighting pairs are supported
    VisibilityView visibility[4];
};
struct ManifestModeView
{
    const ManifestModeDisk *record;
    const ShadowMapLightDisk *lights;
    uint32 lightCount;
    int32 sunLightIndex;
    bool runtime;
};
struct ManifestView
{
    const ManifestDisk *header;
    ManifestModeView mode[SHADOWMAP_MODE_COUNT];
};

// Views borrow only the caller's owned file buffer. Validation is transactional: failure
// clears the view and provides a bounded error. No native-engine pointer enters this API.
inline uint32 FileCRC32(const void *bytes, uint32 count)
{
    if (!bytes || count < sizeof(FileHeader)) return 0;
    CRC32_t crc; CRC32_Init(&crc);
    const uint8 *p = static_cast<const uint8 *>(bytes);
    const uint32 zero = 0, at = offsetof(FileHeader, crc32);
    CRC32_ProcessBuffer(&crc, p, at);
    CRC32_ProcessBuffer(&crc, &zero, sizeof(zero));
    CRC32_ProcessBuffer(&crc, p + at + 4, count - at - 4);
    CRC32_Final(&crc); return crc;
}

inline bool ValidationError(char *error, int bytes, const char *reason)
{
    if (error && bytes > 0) V_snprintf(error, bytes, "Hlight: %s", reason);
    return false;
}
inline bool ZeroBytes(const void *data, uint64 count)
{
    const uint8 *p = static_cast<const uint8 *>(data);
    for (uint64 i = 0; i < count; ++i) if (p[i]) return false;
    return true;
}
inline bool Section(const uint8 *base, uint32 bytes, uint64 &cursor, uint64 offset, uint64 count)
{
    const uint64 aligned = (cursor + 15) & ~uint64(15);
    if (aligned > bytes || offset != aligned || count > bytes - aligned ||
        !ZeroBytes(base + cursor, aligned - cursor)) return false;
    cursor = aligned + count;
    return true;
}
inline bool ValidPair(uint32 face, uint32 lighting)
{
    return (face == LUMP_FACES || face == LUMP_FACES_HDR) &&
        (lighting == LUMP_LIGHTING || lighting == LUMP_LIGHTING_HDR);
}
inline bool PageSide(uint32 n)
{
    return n >= kDefaultPageSize && n <= kMaxPageSize && !(n & (n - 1));
}
inline bool VisibilityRequired(const VisibilityView &v, uint32 face, uint32 light)
{
    const uint64 bit = uint64(face) * v.record->selectedLightCount + light;
    return (v.faceSupport[bit >> 3] & (1u << (bit & 7))) != 0;
}
inline const VisibilityEntryDisk *FindVisibilityEntry(const VisibilityView &v,
    uint32 first, uint32 count, uint32 selectedLightIndex)
{
    uint32 lo = first, hi = first + count;
    while (lo < hi)
    {
        const uint32 mid = lo + (hi - lo) / 2;
        if (v.entries[mid].selectedLightIndex < selectedLightIndex) lo = mid + 1;
        else hi = mid;
    }
    return lo < first + count && v.entries[lo].selectedLightIndex == selectedLightIndex ? &v.entries[lo] : NULL;
}
inline float VisibilitySample(const VisibilityView &v, const VisibilityEntryDisk &entry, uint32 sample)
{
    return float(entry.encoding == kVisibilityDense ? v.payload[entry.payloadByteOffset + sample] : entry.encoding) / 255.0f;
}
inline bool ValidateVisibilityEntries(const VisibilityView &v, uint32 first, uint32 entries,
    uint64 samples, uint64 &payloadCursor, char *error, int errorBytes)
{
    const VisibilitySetDisk &s = *v.record;
    if (first > s.entryCount || entries > s.entryCount - first)
        return ValidationError(error,errorBytes,"visibility entry range");
    for (uint32 i = first; i < first + entries; ++i)
    {
        const VisibilityEntryDisk &e = v.entries[i];
        if (e.selectedLightIndex >= s.selectedLightCount || int32(e.selectedLightIndex) == s.sunLightIndex ||
            (i > first && e.selectedLightIndex <= v.entries[i-1].selectedLightIndex) ||
            e.encoding > kVisibilityDense)
            return ValidationError(error,errorBytes,"visibility light ordering/encoding");
        if (e.encoding != kVisibilityDense)
        {
            if (e.sampleCount || e.payloadByteOffset)
                return ValidationError(error,errorBytes,"uniform visibility has payload");
        }
        else
        {
            const uint64 aligned = (payloadCursor + 3) & ~uint64(3);
            if (!samples || samples > kMaxFileBytes || e.sampleCount != samples ||
                aligned > s.payloadBytes || e.payloadByteOffset != aligned ||
                samples > s.payloadBytes - aligned ||
                !ZeroBytes(v.payload + payloadCursor, aligned - payloadCursor))
                return ValidationError(error,errorBytes,"dense visibility sample/payload range");
            payloadCursor = aligned + samples;
        }
    }
    return true;
}
inline bool ValidateFile(const void *bytes, uint32 count, FileView &out, char *error, int errorBytes)
{
    memset(&out, 0, sizeof(out));
    if (error && errorBytes > 0) error[0] = 0;
    if (!bytes || count < sizeof(FileHeader) || count > kMaxFileBytes ||
        (reinterpret_cast<uintp>(bytes) & 3))
        return ValidationError(error, errorBytes, "invalid file buffer");
    const uint8 *base = static_cast<const uint8 *>(bytes);
    const FileHeader &h = *reinterpret_cast<const FileHeader *>(base);
    if (h.magic != kMagic || h.version != kVersion || h.headerBytes != sizeof(h) ||
        h.endian != kEndian || h.fileBytes != count || !h.density || h.density > kMaxPageSize ||
        !h.modeCount || h.modeCount > 4 || !h.visibilitySetCount || h.visibilitySetCount > h.modeCount ||
        !ZeroBytes(h.reserved, sizeof(h.reserved)) ||
        h.crc32 != FileCRC32(bytes, count))
        return ValidationError(error, errorBytes, "header/version/density/CRC mismatch");
    FileView candidate; memset(&candidate, 0, sizeof(candidate));
    candidate.base = base; candidate.bytes = count; candidate.header = &h;
    uint64 cursor = sizeof(h);
    if (!Section(base, count, cursor, h.modesOffset, uint64(h.modeCount) * sizeof(ModeDisk)))
        return ValidationError(error, errorBytes, "noncanonical mode section");
    const ModeDisk *modes = reinterpret_cast<const ModeDisk *>(base + h.modesOffset);
    const uint32 geometry[] = {1,2,3,6,12,13,14,26,33,43,44,48};
    for (uint32 mi = 0; mi < h.modeCount; ++mi)
    {
        const ModeDisk &m = modes[mi];
        if (!ValidPair(m.faceLump, m.lightingLump) || m.faceVersion != LUMP_FACES_VERSION ||
            m.lightingVersion != LUMP_LIGHTING_VERSION || !m.faceCount || m.faceCount > MAX_MAP_FACES ||
            !m.modelCount || m.modelCount > MAX_MAP_MODELS || m.faceBytes != uint64(m.faceCount) * sizeof(dface_t) ||
            (!m.lightingBytes && m.tileCount) || m.lightingBytes > MAX_MAP_LIGHTING || (m.lightingBytes & 3) ||
            m.identityCount != ARRAYSIZE(geometry) + 2 || m.reserved || m.reserved2 ||
            m.visibilitySetIndex >= h.visibilitySetCount || !ZeroBytes(m.reserved3,sizeof(m.reserved3)) ||
            (mi && (m.faceLump < modes[mi-1].faceLump ||
                (m.faceLump == modes[mi-1].faceLump && m.lightingLump <= modes[mi-1].lightingLump))))
            return ValidationError(error, errorBytes, "mode identity/count/order mismatch");
        ModeView &v = candidate.mode[mi]; v.record = &m;
        if (!Section(base,count,cursor,m.identitiesOffset,uint64(m.identityCount)*sizeof(LumpIdentity)))
            return ValidationError(error,errorBytes,"identity section range");
        v.identities = reinterpret_cast<const LumpIdentity *>(base + m.identitiesOffset);
        uint32 geometryIndex = 0; bool faceSeen = false, lightSeen = false;
        for (uint32 i = 0; i < m.identityCount; ++i)
        {
            const LumpIdentity &id = v.identities[i];
            if (id.lump >= HEADER_LUMPS || id.bytes > kMaxFileBytes || (!id.bytes && id.crc32) || id.reserved ||
                (i && id.lump <= v.identities[i-1].lump))
                return ValidationError(error,errorBytes,"duplicate/invalid lump identity");
            if (id.lump == m.faceLump)
            {
                if (id.version != m.faceVersion || id.bytes != m.faceBytes || id.crc32 != m.facesCRC32)
                    return ValidationError(error,errorBytes,"face identity mismatch");
                faceSeen = true;
            }
            else if (id.lump == m.lightingLump)
            {
                if (id.version != m.lightingVersion || id.bytes != m.lightingBytes || id.crc32 != m.lightingCRC32)
                    return ValidationError(error,errorBytes,"lighting identity mismatch");
                lightSeen = true;
            }
            else
            {
                if (geometryIndex >= ARRAYSIZE(geometry) || id.lump != geometry[geometryIndex++])
                    return ValidationError(error,errorBytes,"missing geometry identity");
                if (mi)
                {
                    const ModeView &previous = candidate.mode[0];
                    uint32 j = 0; while (j < previous.record->identityCount && previous.identities[j].lump != id.lump) ++j;
                    if (j == previous.record->identityCount || memcmp(&id, &previous.identities[j], sizeof(id)))
                        return ValidationError(error,errorBytes,"paired geometry identities disagree");
                }
            }
            for (uint32 previousMode = 0; previousMode < mi; ++previousMode)
            {
                const ModeView &previous = candidate.mode[previousMode];
                for (uint32 j = 0; j < previous.record->identityCount; ++j)
                    if (previous.identities[j].lump == id.lump && memcmp(&id,&previous.identities[j],sizeof(id)))
                        return ValidationError(error,errorBytes,"paired effective lump identities disagree");
            }
        }
        if (!faceSeen || !lightSeen || geometryIndex != ARRAYSIZE(geometry))
            return ValidationError(error,errorBytes,"incomplete effective domain");
        if (!Section(base,count,cursor,m.modelsOffset,uint64(m.modelCount)*sizeof(ModelDisk)))
            return ValidationError(error,errorBytes,"model section range");
        v.models = reinterpret_cast<const ModelDisk *>(base + m.modelsOffset);
        uint32 nextFace = 0;
        for (uint32 i = 0; i < m.modelCount; ++i)
        {
            const ModelDisk &model = v.models[i];
            if (model.modelIndex != i || model.firstFace != nextFace || model.faceCount > m.faceCount - nextFace ||
                (model.flags & ~kModelBakedPoseKnown) || (i == 0 && !(model.flags & kModelBakedPoseKnown)))
                return ValidationError(error,errorBytes,"invalid model face partition/pose");
            for (uint32 c = 0; c < 12; ++c)
                if (!ShadowMap_IsFiniteFloat(model.bakedModelToWorld[c]))
                    return ValidationError(error,errorBytes,"nonfinite baked model pose");
            if (model.flags & kModelBakedPoseKnown)
            {
                const float *a = model.bakedModelToWorld;
                const double determinant = double(a[0])*(double(a[5])*a[10]-double(a[6])*a[9]) -
                    double(a[1])*(double(a[4])*a[10]-double(a[6])*a[8]) +
                    double(a[2])*(double(a[4])*a[9]-double(a[5])*a[8]);
                if (determinant == 0.0) return ValidationError(error,errorBytes,"singular baked model pose");
            }
            nextFace += model.faceCount;
        }
        if (nextFace != m.faceCount) return ValidationError(error,errorBytes,"incomplete model partition");
        if (!Section(base,count,cursor,m.facesOffset,uint64(m.faceCount)*sizeof(FaceDisk)))
            return ValidationError(error,errorBytes,"face section range");
        v.faces = reinterpret_cast<const FaceDisk *>(base + m.facesOffset);
        uint64 expectedTiles = 0; uint32 modelIndex = 0;
        for (uint32 i = 0; i < m.faceCount; ++i)
        {
            const FaceDisk &f = v.faces[i];
            while (modelIndex + 1 < m.modelCount && i >= v.models[modelIndex].firstFace + v.models[modelIndex].faceCount) ++modelIndex;
            if (f.faceOrdinal != i || f.modelIndex != modelIndex || (f.flags & ~31u) ||
                !ZeroBytes(f.reserved,sizeof(f.reserved)) || f.styleCount > kMaxStyles ||
                uint64(f.nativeExtents[0])*h.density+1 != f.highWidth ||
                uint64(f.nativeExtents[1])*h.density+1 != f.highHeight ||
                !f.highWidth || !f.highHeight ||
                (f.styleCount && (f.highWidth > kMaxPageSize-2 || f.highHeight > kMaxPageSize-2)) ||
                bool(f.flags & kFaceHasLighting) != bool(f.styleCount) ||
                bool(f.flags & kFaceHasBakedLocalDirect) != bool(f.styleCount) ||
                ((f.flags & kFaceHasBakedLocalDirect) && !(v.models[modelIndex].flags & kModelBakedPoseKnown)) ||
                ((f.flags & kFaceHasSun) && (!f.styleCount || !(v.models[modelIndex].flags & kModelBakedPoseKnown))))
                return ValidationError(error,errorBytes,"invalid face density/flags/domain");
            for (uint32 s = 0; s < kMaxStyles; ++s)
            {
                if (s < f.styleCount)
                {
                    if (f.styles[s] >= kLightstyleCount || (s == 0 && f.styles[s] != 0))
                        return ValidationError(error,errorBytes,"invalid authored style");
                    for (uint32 j = 0; j < s; ++j) if (f.styles[j] == f.styles[s])
                        return ValidationError(error,errorBytes,"duplicate authored style");
                }
                else if (f.styles[s] != 255) return ValidationError(error,errorBytes,"unused style not canonical");
                for (uint32 p = 0; p < kPlaneSlots; ++p)
                {
                    const bool used = s < f.styleCount && (p == 0 || (f.flags & kFaceBumped));
                    if (used) { if (f.tiles[s*4+p] >= m.tileCount) return ValidationError(error,errorBytes,"missing style/bump plane"); ++expectedTiles; }
                    else if (f.tiles[s*4+p] != kMissing) return ValidationError(error,errorBytes,"unexpected style/bump plane");
                }
            }
        }
        if (expectedTiles != m.tileCount) return ValidationError(error,errorBytes,"tile count mismatch");
        if (!Section(base,count,cursor,m.tilesOffset,uint64(m.tileCount)*sizeof(TileDisk)))
            return ValidationError(error,errorBytes,"tile section range");
        v.tiles = reinterpret_cast<const TileDisk *>(base + m.tilesOffset);
        if (!Section(base,count,cursor,m.pagesOffset,uint64(m.pageCount)*sizeof(PageDisk)))
            return ValidationError(error,errorBytes,"page section range");
        v.pages = reinterpret_cast<const PageDisk *>(base + m.pagesOffset);
        uint32 nextTile = 0;
        for (uint32 pi = 0; pi < m.pageCount; ++pi)
        {
            const PageDisk &page = v.pages[pi];
            if (!PageSide(page.width) || page.height != page.width || page.format != kRGBA16Float ||
                page.reserved || !ZeroBytes(page.reserved2,sizeof(page.reserved2)) || page.firstTile != nextTile ||
                !page.tileCount || page.tileCount > m.tileCount-nextTile ||
                page.pixelsBytes != uint64(page.width)*page.height*8 ||
                !Section(base,count,cursor,page.pixelsOffset,page.pixelsBytes))
                return ValidationError(error,errorBytes,"invalid page/pixel range");
            uint32 rowY = 0, rowEnd = 0, nextX = 0;
            for (uint32 ti = nextTile; ti < nextTile+page.tileCount; ++ti)
            {
                const TileDisk &t = v.tiles[ti];
                if (t.faceOrdinal >= m.faceCount || t.styleSlot >= kMaxStyles || t.plane >= kPlaneSlots ||
                    t.page != pi || !t.x || !t.y || uint64(t.x)+t.width+1 > page.width || uint64(t.y)+t.height+1 > page.height)
                    return ValidationError(error,errorBytes,"tile bounds/owner");
                const FaceDisk &f = v.faces[t.faceOrdinal];
                if (t.width != f.highWidth || t.height != f.highHeight || f.tiles[t.styleSlot*4+t.plane] != ti)
                    return ValidationError(error,errorBytes,"tile backlink/dimensions");
                const uint32 y = t.y-1, x = t.x-1;
                if (y != rowY)
                {
                    if (y != rowEnd || x) return ValidationError(error,errorBytes,"noncanonical shelf transition");
                    rowY = y; nextX = 0;
                }
                if (x != nextX) return ValidationError(error,errorBytes,"overlapping/noncanonical shelf");
                nextX = x+t.width+2; rowEnd = MAX(rowEnd,y+t.height+2);
                const uint16 *pixels = reinterpret_cast<const uint16 *>(base+page.pixelsOffset);
                for (uint32 yy = 0; yy < t.height+2; ++yy)
                for (uint32 xx = 0; xx < t.width+2; ++xx)
                {
                    const uint64 at = (uint64(y+yy)*page.width+x+xx)*4;
                    for (uint32 c = 0; c < 3; ++c)
                        if ((pixels[at+c] & 0x8000) || (pixels[at+c] & 0x7c00) == 0x7c00)
                            return ValidationError(error,errorBytes,"nonfinite/negative half RGB");
                    const uint16 a = pixels[at+3];
                    if (t.styleSlot == 0 && t.plane == 0)
                    {
                        if (a > 0x3c00 || (!(f.flags & kFaceHasSun) && a != 0x3c00))
                            return ValidationError(error,errorBytes,"invalid/unnormalized sun alpha");
                    }
                    else if (a) return ValidationError(error,errorBytes,"reserved alpha is nonzero");
                    const uint32 cx = MAX(1u,MIN(t.width,xx)), cy = MAX(1u,MIN(t.height,yy));
                    if ((cx != xx || cy != yy) && memcmp(pixels+at,pixels+(uint64(y+cy)*page.width+x+cx)*4,8))
                        return ValidationError(error,errorBytes,"nonreplicated tile gutter");
                }
            }
            // Empty shelf/page pixels must not carry undeclared payload.
            const uint16 *pixels = reinterpret_cast<const uint16 *>(base+page.pixelsOffset);
            uint32 tile = nextTile;
            for (uint32 y = 0; y < page.height; ++y)
            {
                while (tile < nextTile+page.tileCount && v.tiles[tile].y-1+v.tiles[tile].height+2 <= y) ++tile;
                uint32 x = 0;
                for (uint32 t = tile; t < nextTile+page.tileCount && v.tiles[t].y-1 <= y; ++t)
                {
                    const TileDisk &rect = v.tiles[t];
                    if (y >= rect.y-1+rect.height+2) continue;
                    if (!ZeroBytes(pixels+(uint64(y)*page.width+x)*4,uint64(rect.x-1-x)*8))
                        return ValidationError(error,errorBytes,"nonzero unused page pixels");
                    x = rect.x+rect.width+1;
                }
                if (!ZeroBytes(pixels+(uint64(y)*page.width+x)*4,uint64(page.width-x)*8))
                    return ValidationError(error,errorBytes,"nonzero unused page pixels");
            }
            nextTile += page.tileCount;
        }
        if (nextTile != m.tileCount || bool(m.pageCount) != bool(m.tileCount))
            return ValidationError(error,errorBytes,"incomplete tile pages");
    }
    if (!Section(base,count,cursor,h.visibilitySetsOffset,uint64(h.visibilitySetCount)*sizeof(VisibilitySetDisk)))
        return ValidationError(error,errorBytes,"visibility set section");
    const VisibilitySetDisk *sets = reinterpret_cast<const VisibilitySetDisk *>(base+h.visibilitySetsOffset);
    uint32 setMask = 0;
    for (uint32 si = 0; si < h.visibilitySetCount; ++si)
    {
        const VisibilitySetDisk &s = sets[si]; VisibilityView &v = candidate.visibility[si]; v.record = &s;
        const ModeView *owner = NULL;
        for (uint32 mi = 0; mi < h.modeCount; ++mi)
            if (modes[mi].visibilitySetIndex == si)
            {
                if (!owner) owner = &candidate.mode[mi];
                else if (modes[mi].faceCount != owner->record->faceCount)
                    return ValidationError(error,errorBytes,"shared visibility face domain mismatch");
                if (owner != &candidate.mode[mi])
                    for (uint32 fi = 0; fi < modes[mi].faceCount; ++fi)
                    {
                        const FaceDisk &a = owner->faces[fi], &b = candidate.mode[mi].faces[fi];
                        if (a.highWidth != b.highWidth || a.highHeight != b.highHeight ||
                            a.styleCount != b.styleCount || memcmp(a.styles,b.styles,sizeof(a.styles)) || a.modelIndex != b.modelIndex ||
                            memcmp(&owner->models[a.modelIndex],&candidate.mode[mi].models[b.modelIndex],sizeof(ModelDisk)))
                            return ValidationError(error,errorBytes,"shared visibility receiver domains disagree");
                    }
                setMask |= 1u << si;
            }
        if (!owner || s.faceCount != owner->record->faceCount || s.selectedLightCount > MAX_MAP_WORLDLIGHTS ||
            (s.sunLightIndex != -1 && (s.sunLightIndex != 0 || !s.selectedLightCount)) ||
            s.reserved || s.reserved2 || s.unbakedFaceCount > s.faceCount ||
            uint64(s.unbakedLightIndexCount) > uint64(s.faceCount)*s.selectedLightCount || s.payloadBytes > kMaxFileBytes ||
            s.faceSupportBytes != (uint64(s.faceCount)*s.selectedLightCount+7)/8)
            return ValidationError(error,errorBytes,"visibility identity/domain/count");
        if (!Section(base,count,cursor,s.facesOffset,uint64(s.faceCount)*sizeof(FaceVisibilityDisk)) ||
            !Section(base,count,cursor,s.entriesOffset,uint64(s.entryCount)*sizeof(VisibilityEntryDisk)) ||
            !Section(base,count,cursor,s.propsOffset,uint64(s.propCount)*sizeof(PropVisibilityDisk)) ||
            !Section(base,count,cursor,s.meshesOffset,uint64(s.meshCount)*sizeof(PropMeshVisibilityDisk)) ||
            !Section(base,count,cursor,s.faceSupportOffset,s.faceSupportBytes) ||
            !Section(base,count,cursor,s.unbakedFacesOffset,uint64(s.unbakedFaceCount)*sizeof(UnbakedFaceDisk)) ||
            !Section(base,count,cursor,s.unbakedLightIndicesOffset,uint64(s.unbakedLightIndexCount)*sizeof(uint32)) ||
            !Section(base,count,cursor,s.payloadOffset,s.payloadBytes))
            return ValidationError(error,errorBytes,"visibility section range/order");
        v.faces = reinterpret_cast<const FaceVisibilityDisk *>(base+s.facesOffset);
        v.entries = reinterpret_cast<const VisibilityEntryDisk *>(base+s.entriesOffset);
        v.props = reinterpret_cast<const PropVisibilityDisk *>(base+s.propsOffset);
        v.meshes = reinterpret_cast<const PropMeshVisibilityDisk *>(base+s.meshesOffset);
        v.faceSupport = base+s.faceSupportOffset; v.payload = base+s.payloadOffset;
        v.unbakedFaces = reinterpret_cast<const UnbakedFaceDisk *>(base+s.unbakedFacesOffset);
        v.unbakedLightIndices = reinterpret_cast<const uint32 *>(base+s.unbakedLightIndicesOffset);
        uint32 nextUnbaked = 0;
        for (uint32 uf = 0; uf < s.unbakedFaceCount; ++uf)
        {
            const UnbakedFaceDisk &f = v.unbakedFaces[uf];
            if (f.faceOrdinal >= s.faceCount || (uf && f.faceOrdinal <= v.unbakedFaces[uf-1].faceOrdinal) ||
                !(owner->faces[f.faceOrdinal].flags & kFaceHasBakedLocalDirect) || owner->faces[f.faceOrdinal].styleCount != kMaxStyles ||
                f.firstLightIndex != nextUnbaked || !f.lightCount || f.lightCount > s.unbakedLightIndexCount-nextUnbaked)
                return ValidationError(error,errorBytes,"unbaked face partition");
            for (uint32 j = 0; j < f.lightCount; ++j)
            {
                const uint32 li = v.unbakedLightIndices[nextUnbaked+j];
                if (li >= s.selectedLightCount || int32(li) == s.sunLightIndex ||
                    (j && li <= v.unbakedLightIndices[nextUnbaked+j-1]) || !VisibilityRequired(v,f.faceOrdinal,li))
                    return ValidationError(error,errorBytes,"unbaked selected-local range/order/support");
            }
            nextUnbaked += f.lightCount;
        }
        if (nextUnbaked != s.unbakedLightIndexCount) return ValidationError(error,errorBytes,"orphan unbaked light indices");
        if (ShadowMap_CRC32(v.payload,uint32(s.payloadBytes)) != s.payloadCRC32)
            return ValidationError(error,errorBytes,"visibility payload CRC");
        const uint64 supportBits = uint64(s.faceCount)*s.selectedLightCount;
        if ((supportBits & 7) && (v.faceSupport[s.faceSupportBytes-1] & (0xffu << (supportBits & 7))))
            return ValidationError(error,errorBytes,"visibility support padding");
        uint32 nextEntry = 0, nextMesh = 0; uint64 payloadCursor = 0;
        for (uint32 fi = 0; fi < s.faceCount; ++fi)
        {
            const FaceVisibilityDisk &f = v.faces[fi];
            if (f.faceOrdinal != fi || f.firstEntry != nextEntry || f.reserved ||
                !ValidateVisibilityEntries(v,f.firstEntry,f.entryCount,
                    uint64(owner->faces[fi].highWidth)*owner->faces[fi].highHeight,payloadCursor,error,errorBytes))
                return ValidationError(error,errorBytes,"visibility face partition/entries");
            uint32 required = 0;
            for (uint32 li = 0; li < s.selectedLightCount; ++li)
                if (VisibilityRequired(v,fi,li))
                {
                    if (int32(li) == s.sunLightIndex || !owner->faces[fi].styleCount ||
                        !FindVisibilityEntry(v,f.firstEntry,f.entryCount,li))
                        return ValidationError(error,errorBytes,"incomplete required face visibility");
                    ++required;
                }
            if (required != f.entryCount) return ValidationError(error,errorBytes,"undeclared face visibility");
            nextEntry += f.entryCount;
        }
        for (uint32 pi = 0; pi < s.propCount; ++pi)
        {
            const PropVisibilityDisk &p = v.props[pi];
            if (p.staticPropOrdinal != pi || p.firstMesh != nextMesh || p.reserved ||
                p.meshCount > s.meshCount-nextMesh)
                return ValidationError(error,errorBytes,"visibility prop partition");
            for (uint32 mesh = nextMesh; mesh < nextMesh+p.meshCount; ++mesh)
            {
                const PropMeshVisibilityDisk &m = v.meshes[mesh];
                if (m.meshOrdinal != mesh-nextMesh || !m.vertexCount || m.firstEntry != nextEntry ||
                    m.entryCount != s.selectedLightCount-uint32(s.sunLightIndex >= 0) ||
                    (!m.directPayloadBytes && m.directPayloadByteOffset) ||
                    !ValidateVisibilityEntries(v,m.firstEntry,m.entryCount,m.vertexCount,payloadCursor,error,errorBytes))
                    return ValidationError(error,errorBytes,"incomplete prop mesh visibility");
                if (m.directPayloadBytes)
                {
                    const uint64 aligned = (payloadCursor+3)&~uint64(3);
                    if (m.directPayloadByteOffset != aligned || aligned > s.payloadBytes ||
                        m.directPayloadBytes > s.payloadBytes-aligned || m.directPayloadBytes < sizeof(PropDirectDisk) ||
                        !ZeroBytes(v.payload+payloadCursor,aligned-payloadCursor))
                        return ValidationError(error,errorBytes,"prop direct block range/order");
                    const PropDirectDisk &d = *reinterpret_cast<const PropDirectDisk *>(v.payload+aligned);
                    if (d.flags != kPropMeshHasBakedLocalDirect || !d.styleCount || d.styleCount > kMaxStyles ||
                        d.styles[0] || d.vertexCount != m.vertexCount || d.angularPlaneCount != kPropDirectAngularPlanes ||
                        !ZeroBytes(d.reserved,sizeof(d.reserved)) ||
                        uint64(d.radianceBytes) != uint64(d.styleCount)*d.angularPlaneCount*m.vertexCount*8 ||
                        uint64(m.directPayloadBytes) != sizeof(d)+uint64(d.radianceBytes)+uint64(d.unbakedLightCount)*4)
                        return ValidationError(error,errorBytes,"prop direct identity/planes");
                    const PropDirectView direct = GetPropDirect(v,m);
                    for (uint32 style = 0; style < kMaxStyles; ++style)
                    {
                        if (style >= d.styleCount)
                        {
                            if (d.styles[style] != 255) return ValidationError(error,errorBytes,"prop direct unused style");
                            continue;
                        }
                        if (d.styles[style] >= kLightstyleCount) return ValidationError(error,errorBytes,"prop direct style range");
                        for (uint32 previous = 0; previous < style; ++previous)
                            if (d.styles[previous] == d.styles[style]) return ValidationError(error,errorBytes,"prop direct duplicate style");
                    }
                    for (uint32 value = 0; value < d.radianceBytes/2; value += 4)
                    {
                        for (uint32 c = 0; c < 3; ++c)
                            if ((direct.pixels[value+c]&0x8000) || (direct.pixels[value+c]&0x7c00) == 0x7c00)
                                return ValidationError(error,errorBytes,"prop direct nonfinite/negative RGB");
                        if (direct.pixels[value+3]) return ValidationError(error,errorBytes,"prop direct reserved alpha");
                    }
                    if (d.unbakedLightCount && d.styleCount != kMaxStyles)
                        return ValidationError(error,errorBytes,"prop direct fallback with free style slot");
                    for (uint32 j = 0; j < d.unbakedLightCount; ++j)
                    {
                        const uint32 li = direct.unbakedLightIndices[j];
                        if (li >= s.selectedLightCount || int32(li) == s.sunLightIndex ||
                            (j && li <= direct.unbakedLightIndices[j-1]))
                            return ValidationError(error,errorBytes,"prop direct unbaked local order/range");
                    }
                    payloadCursor = aligned+m.directPayloadBytes;
                }
                nextEntry += m.entryCount;
            }
            nextMesh += p.meshCount;
        }
        if (nextEntry != s.entryCount || nextMesh != s.meshCount || payloadCursor != s.payloadBytes)
            return ValidationError(error,errorBytes,"orphan visibility entry/mesh/payload");
    }
    if (setMask != (1u << h.visibilitySetCount)-1) return ValidationError(error,errorBytes,"unreferenced visibility set");
    for (uint32 mi = 0; mi < h.modeCount; ++mi)
        candidate.mode[mi].visibility = candidate.visibility[modes[mi].visibilitySetIndex];
    if (cursor != count) return ValidationError(error,errorBytes,"trailing file bytes");
    out = candidate; return true;
}
inline bool ValidWorldLight(const dworldlight_t &w)
{
    return ShadowMap_FiniteVector(w.origin) && ShadowMap_FiniteVector(w.intensity) &&
        ShadowMap_FiniteVector(w.normal) && w.intensity.x >= 0 && w.intensity.y >= 0 && w.intensity.z >= 0 &&
        ShadowMap_IsFiniteFloat(w.constant_attn) && w.constant_attn >= 0 &&
        ShadowMap_IsFiniteFloat(w.linear_attn) && w.linear_attn >= 0 &&
        ShadowMap_IsFiniteFloat(w.quadratic_attn) && w.quadratic_attn >= 0 &&
        ShadowMap_IsFiniteFloat(w.stopdot) && w.stopdot >= -1 && w.stopdot <= 1 &&
        ShadowMap_IsFiniteFloat(w.stopdot2) && w.stopdot2 >= -1 && w.stopdot2 <= 1 &&
        ShadowMap_IsFiniteFloat(w.exponent) && w.exponent >= 0 &&
        ShadowMap_IsFiniteFloat(w.radius) && w.radius >= 0;
}
// Inspect only ZIP metadata when a manifest is absent. Never infer the asset
// name from the BSP basename or inflate/copy the embedded lightmap pages.
inline bool FindPakAsset(ShadowMapBspReadFn read, void *context,
    const ShadowMapBspLumpInfo &pak, bool &found)
{
    found = false;
    if (!pak.filelen) return true;
    if (!read || pak.uncompressedSize || pak.filelen < sizeof(ZIP_EndOfCentralDirRecord) ||
        uint64(pak.fileofs) + pak.filelen > 0xffffffffu) return false;
    uint8 tail[sizeof(ZIP_EndOfCentralDirRecord) + 65535];
    const uint32 tailBytes = MIN(pak.filelen, uint32(sizeof(tail)));
    const uint32 tailOffset = pak.filelen - tailBytes;
    if (!read(context, pak.fileofs + tailOffset, tailBytes, tail)) return false;
    ZIP_EndOfCentralDirRecord end{};
    int endOffset = -1;
    for (int i = int(tailBytes - sizeof(end)); i >= 0; --i)
    {
        memcpy(&end, tail + i, sizeof(end));
        if (end.signature == PKID(5,6) && uint32(i) + sizeof(end) + end.commentLength == tailBytes)
        {
            endOffset = i;
            break;
        }
    }
    if (endOffset < 0 || end.numberOfThisDisk || end.numberOfTheDiskWithStartOfCentralDirectory ||
        end.nCentralDirectoryEntries_ThisDisk != end.nCentralDirectoryEntries_Total ||
        uint64(end.startOfCentralDirOffset) + end.centralDirectorySize > tailOffset + uint32(endOffset))
        return false;
    // Source XZP2 omits central-entry extra fields/comments even when their
    // header lengths are nonzero; match CZipFile::ParseFromBuffer.
    const bool compatible = end.commentLength < 4 ||
        V_strnicmp(reinterpret_cast<const char *>(tail + endOffset + sizeof(end)), "XZP2", 4) != 0;
    uint32 cursor = end.startOfCentralDirOffset;
    const uint32 limit = cursor + end.centralDirectorySize;
    for (uint32 i = 0; i < end.nCentralDirectoryEntries_Total; ++i)
    {
        ZIP_FileHeader entry;
        if (cursor > limit || sizeof(entry) > limit - cursor ||
            !read(context, pak.fileofs + cursor, sizeof(entry), &entry) ||
            entry.signature != PKID(1,2) || entry.diskNumberStart) return false;
        cursor += sizeof(entry);
        const uint32 span = uint32(entry.fileNameLength) +
            (compatible ? uint32(entry.extraFieldLength) + entry.fileCommentLength : 0);
        if (span > limit - cursor) return false;
        char name[256];
        if (entry.fileNameLength >= 13)
        {
            const uint32 nameBytes = MIN(uint32(entry.fileNameLength), uint32(sizeof(name)));
            if (!read(context, pak.fileofs + cursor, nameBytes, name)) return false;
            // A ZIP length must not hide a shorter asset name from C-string consumers.
            if (memchr(name, 0, nameBytes)) return false;
            if (entry.fileNameLength < sizeof(name))
            {
                name[entry.fileNameLength] = 0;
                if (!V_strnicmp(name, "maps/", 5) && !V_stricmp(name + entry.fileNameLength - 7, ".hlight"))
                {
                    found = true;
                    return true;
                }
            }
        }
        cursor += span;
    }
    return true; // Aligned Source ZIPs can pad the central-directory tail.
}

inline bool ValidateManifest(const void *bytes, uint32 count, uint32 levelFlags,
    ManifestView &out, char *error, int errorBytes)
{
    memset(&out,0,sizeof(out));
    if (error && errorBytes > 0) error[0] = 0;
    if (!bytes || count < sizeof(ManifestDisk) || count > kMaxFileBytes || (reinterpret_cast<uintp>(bytes)&3))
        return ValidationError(error,errorBytes,"manifest buffer");
    const ManifestDisk &h = *static_cast<const ManifestDisk *>(bytes);
    const uint32 mask = (levelFlags & ShadowMap_LevelFlagDirect(0) ? 1u : 0u) |
        (levelFlags & ShadowMap_LevelFlagDirect(1) ? 2u : 0u);
    if (h.byteSize != count || !h.runtimeModeMask || (h.runtimeModeMask & ~3u) ||
        h.runtimeModeMask != mask || h.assetVersion != kVersion || h.reserved)
        return ValidationError(error,errorBytes,"manifest version/flags mismatch");
    uint32 pathLength = 0; while (pathLength < sizeof(h.assetPath) && h.assetPath[pathLength]) ++pathLength;
    if (pathLength < 13 || pathLength == sizeof(h.assetPath) || strncmp(h.assetPath,"maps/",5) ||
        strcmp(h.assetPath+pathLength-7,".hlight") || !ZeroBytes(h.assetPath+pathLength,sizeof(h.assetPath)-pathLength))
        return ValidationError(error,errorBytes,"invalid pak asset path");
    for (uint32 i = 5; i < pathLength; ++i)
        if (static_cast<uint8>(h.assetPath[i]) < 32 || h.assetPath[i] == 127 ||
            h.assetPath[i] == '/' || h.assetPath[i] == '\\' || h.assetPath[i] == ':')
            return ValidationError(error,errorBytes,"unsafe pak asset path");
    ManifestView candidate; memset(&candidate,0,sizeof(candidate)); candidate.header = &h;
    uint64 cursor = sizeof(h);
    for (uint32 mi = 0; mi < SHADOWMAP_MODE_COUNT; ++mi)
    {
        const ManifestModeDisk &m = h.mode[mi]; ManifestModeView &v = candidate.mode[mi];
        v.record = &m; v.runtime = (h.runtimeModeMask & (1u<<mi)) != 0;
        if (!v.runtime)
        {
            ManifestModeDisk empty; memset(&empty,0,sizeof(empty)); empty.sunLightIndex = -1;
            if (memcmp(&m,&empty,sizeof(m))) return ValidationError(error,errorBytes,"noncanonical absent manifest mode");
            continue;
        }
        if (!ValidPair(m.faceLump,m.lightingLump) || m.lightingLump != uint32(mi ? LUMP_LIGHTING_HDR : LUMP_LIGHTING) ||
            m.worldlightsLump != uint32(mi ? LUMP_WORLDLIGHTS_HDR : LUMP_WORLDLIGHTS) ||
            m.lightingBytes > MAX_MAP_LIGHTING || (m.lightingBytes & 3) || (!m.lightingBytes && m.lightingCRC32) ||
            m.lightCount > MAX_MAP_WORLDLIGHTS || m.assetMode >= 4 ||
            (m.sunLightIndex != -1 && (m.sunLightIndex != 0 || !m.lightCount)) || !ZeroBytes(m.reserved,sizeof(m.reserved)) ||
            m.lightsOffset != cursor || uint64(m.lightCount)*sizeof(ShadowMapLightDisk) > count-cursor)
            return ValidationError(error,errorBytes,"manifest mode/range");
        v.lights = reinterpret_cast<const ShadowMapLightDisk *>(static_cast<const uint8 *>(bytes)+cursor);
        v.lightCount = m.lightCount; v.sunLightIndex = m.sunLightIndex;
        cursor += uint64(m.lightCount)*sizeof(ShadowMapLightDisk);
        for (uint32 i = 0; i < m.lightCount; ++i)
        {
            const ShadowMapLightDisk &l = v.lights[i]; const dworldlight_t &w = l.light;
            const bool sun = int32(i) == m.sunLightIndex;
            if (!ValidWorldLight(w) || w.flags || w.style < 0 || w.style >= int32(kLightstyleCount) || l.sourceEntity < -1 ||
                l.reserved || !ShadowMap_IsFiniteFloat(l.startFade) || !ShadowMap_IsFiniteFloat(l.endFade) ||
                !ShadowMap_IsFiniteFloat(l.capDist) || l.startFade < 0 ||
                (sun ? (w.type != emit_skylight || !ShadowMap_ValidSunAngularRadius(l.shadowSunAngularRadius) ||
                    l.shadowSourceRadius != 0 || l.startFade != 0 || l.endFade != 0 || l.capDist != 0) :
                    ((w.type != emit_point && w.type != emit_spotlight) || !ShadowMap_ValidLocalSourceRadius(l.shadowSourceRadius) ||
                    l.shadowSunAngularRadius != 0 || l.capDist <= 0)))
                return ValidationError(error,errorBytes,"invalid selected-light record");
        }
    }
    if (cursor != count) return ValidationError(error,errorBytes,"manifest trailing bytes");
    out = candidate; return true;
}
inline const ModeView *FindMode(const FileView &file, uint32 faceLump, uint32 lightingLump)
{
    if (!file.header) return NULL;
    for (uint32 i = 0; i < file.header->modeCount; ++i)
        if (file.mode[i].record->faceLump == faceLump && file.mode[i].record->lightingLump == lightingLump) return &file.mode[i];
    return NULL;
}
// Both views must have passed their individual validators. No ordinal aliasing:
// each admitted native lighting mode names exactly one explicit file mode.
inline bool ValidateManifestAsset(const FileView &file, const ManifestView &manifest,
    char *error, int errorBytes)
{
    if (!file.header || !manifest.header) return ValidationError(error,errorBytes,"unvalidated manifest/asset pair");
    uint32 admitted = 0, assetMask = 0;
    for (uint32 mi = 0; mi < SHADOWMAP_MODE_COUNT; ++mi)
    {
        const ManifestModeView &mv = manifest.mode[mi];
        if (!mv.runtime) continue;
        const ManifestModeDisk &mm = *mv.record;
        if (mm.assetMode >= file.header->modeCount || (assetMask & (1u << mm.assetMode)))
            return ValidationError(error,errorBytes,"duplicate/missing manifest asset mode");
        const ModeView &v = file.mode[mm.assetMode]; const ModeDisk &m = *v.record;
        if (mm.faceLump != m.faceLump || mm.lightingLump != m.lightingLump ||
            mm.facesCRC32 != m.facesCRC32 || mm.lightingBytes != m.lightingBytes ||
            mm.lightingCRC32 != m.lightingCRC32)
            return ValidationError(error,errorBytes,"manifest/asset mode identity mismatch");
        const VisibilitySetDisk &visibility = *v.visibility.record;
        if (visibility.selectedLightCount != mv.lightCount || visibility.sunLightIndex != mv.sunLightIndex ||
            visibility.selectedLightsCRC32 != ShadowMap_CRC32(mv.lights,mv.lightCount*sizeof(ShadowMapLightDisk)))
            return ValidationError(error,errorBytes,"visibility/manifest selected lights mismatch");
        for (uint32 f = 0; f < m.faceCount; ++f)
        {
            const FaceDisk &face = v.faces[f];
            const bool sun = mm.sunLightIndex >= 0 && face.styleCount &&
                (v.models[face.modelIndex].flags & kModelBakedPoseKnown);
            if (bool(face.flags & kFaceHasSun) != sun)
                return ValidationError(error,errorBytes,"designated sun/face eligibility mismatch");
            if (face.styleCount)
                for (uint32 li = 0; li < mv.lightCount; ++li)
                    if (int32(li) != mv.sunLightIndex && mv.lights[li].light.radius == 0 &&
                        !FindVisibilityEntry(v.visibility,v.visibility.faces[f].firstEntry,v.visibility.faces[f].entryCount,li))
                        return ValidationError(error,errorBytes,"unbounded local visibility omitted");
        }
        for (uint32 uf = 0; uf < visibility.unbakedFaceCount; ++uf)
        {
            const UnbakedFaceDisk &exceptions = v.visibility.unbakedFaces[uf];
            const FaceDisk &face = v.faces[exceptions.faceOrdinal];
            for (uint32 j = 0; j < exceptions.lightCount; ++j)
            {
                const uint32 li = v.visibility.unbakedLightIndices[exceptions.firstLightIndex+j];
                for (uint32 style = 0; style < face.styleCount; ++style)
                    if (face.styles[style] == uint32(mv.lights[li].light.style))
                        return ValidationError(error,errorBytes,"unbaked local style already baked");
            }
        }
        for (uint32 mesh = 0; mesh < visibility.meshCount; ++mesh)
        {
            const PropDirectView direct = GetPropDirect(v.visibility,v.visibility.meshes[mesh]);
            if (!direct.record) continue;
            for (uint32 j = 0; j < direct.record->unbakedLightCount; ++j)
            {
                const uint32 li = direct.unbakedLightIndices[j];
                for (uint32 style = 0; style < direct.record->styleCount; ++style)
                    if (direct.record->styles[style] == uint32(mv.lights[li].light.style))
                        return ValidationError(error,errorBytes,"prop unbaked local style already baked");
            }
        }
        ++admitted; assetMask |= 1u << mm.assetMode;
    }
    if (admitted != file.header->modeCount)
        return ValidationError(error,errorBytes,"unreferenced asset mode");
    return true;
}
} // namespace hlight
#endif
