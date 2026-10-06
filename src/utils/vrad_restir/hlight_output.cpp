//========= Copyright Valve Corporation, All rights reserved. ============//
#include "hlight_output.h"
#include "bsp_output.h"
#include "restir_scene_internal.h"
#include "vrad_restir.h"
#include "bsplib.h"
#include "tier1/utlbuffer.h"
#include "gamebspfile.h"
#include <limits.h>
#include <string.h>

extern int numworldlightsLDR, numworldlightsHDR;
extern dworldlight_t dworldlightsLDR[MAX_MAP_WORLDLIGHTS], dworldlightsHDR[MAX_MAP_WORLDLIGHTS];

namespace
{
struct ModeStorage
{
    hlight::ModeDisk record;
    bool active;
    CUtlVector<hlight::LumpIdentity> identities;
    CUtlVector<hlight::ModelDisk> models;
    CUtlVector<hlight::FaceDisk> faces;
    CUtlVector<hlight::TileDisk> tiles;
    CUtlVector<hlight::PageDisk> pages;
    CUtlVector<byte> pixels;
    CUtlVector<ShadowMapLightDisk> lights;
    void Clear()
    {
        active = false;
        memset(&record,0,sizeof(record)); identities.Purge(); models.Purge(); faces.Purge();
        tiles.Purge(); pages.Purge(); pixels.Purge(); lights.Purge();
    }
};
ModeStorage s_Mode[2];
uint32 s_Density = 0;
char s_AssetPath[256];
bool s_Prepared = false, s_Finalized = false;
int SelectedMode() { return g_bHDR ? 1 : 0; }
int SelectedFaces() { return g_bHDR && numfaces_hdr ? numfaces_hdr : numfaces; }
bool Fail(const char *why)
{
    Warning("Hlight: %s (%s mode)\n",why,g_bHDR ? "HDR" : "LDR"); return false;
}
void DeleteManifest()
{
    GameLumpHandle_t h = g_GameLumps.GetGameLumpHandle(GAMELUMP_RESTIR_SHADOWMAPS);
    if (h != g_GameLumps.InvalidGameLump()) g_GameLumps.DestroyGameLump(h);
}
void Identity(ModeStorage &m, uint32 lump, const void *data, uint32 bytes, uint32 version = 0)
{
    hlight::LumpIdentity id = {lump,version,bytes,ShadowMap_CRC32(data,bytes),0};
    m.identities.AddToTail(id);
}
void GeometryIdentities(ModeStorage &m)
{
    Identity(m,1,dplanes,numplanes*sizeof(dplane_t));
    Identity(m,2,dtexdata,numtexdata*sizeof(dtexdata_t));
    Identity(m,3,dvertexes,numvertexes*sizeof(dvertex_t));
    Identity(m,6,texinfo.Base(),texinfo.Count()*sizeof(texinfo_t));
    Identity(m,12,dedges,numedges*sizeof(dedge_t));
    Identity(m,13,dsurfedges,numsurfedges*sizeof(int));
    Identity(m,14,dmodels,nummodels*sizeof(dmodel_t));
    Identity(m,26,g_dispinfo.Base(),g_dispinfo.Count()*sizeof(ddispinfo_t));
    Identity(m,33,g_DispVerts.Base(),g_DispVerts.Count()*sizeof(CDispVert));
    Identity(m,43,g_TexDataStringData.Base(),g_TexDataStringData.Count());
    Identity(m,44,g_TexDataStringTable.Base(),g_TexDataStringTable.Count()*sizeof(int));
    Identity(m,48,g_DispTris.Base(),g_DispTris.Count()*sizeof(CDispTri));
}
void FinalIdentities(ModeStorage &m, int mode)
{
    m.identities.RemoveAll(); GeometryIdentities(m);
    const dface_t *faces = mode ? (numfaces_hdr ? dfaces_hdr : dfaces) : dfaces;
    const int count = mode && numfaces_hdr ? numfaces_hdr : numfaces;
    const CUtlVector<byte> &lighting = mode ? dlightdataHDR : dlightdataLDR;
    m.record.faceLump = mode && numfaces_hdr ? LUMP_FACES_HDR : LUMP_FACES;
    m.record.lightingLump = mode ? LUMP_LIGHTING_HDR : LUMP_LIGHTING;
    m.record.faceVersion = LUMP_FACES_VERSION; m.record.lightingVersion = LUMP_LIGHTING_VERSION;
    m.record.faceBytes = count*sizeof(dface_t); m.record.lightingBytes = lighting.Count();
    m.record.facesCRC32 = ShadowMap_CRC32(faces,(uint32)m.record.faceBytes);
    m.record.lightingCRC32 = ShadowMap_CRC32(lighting.Base(),lighting.Count());
    Identity(m,m.record.faceLump,faces,(uint32)m.record.faceBytes,m.record.faceVersion);
    Identity(m,m.record.lightingLump,lighting.Base(),lighting.Count(),m.record.lightingVersion);
    for (int i = 1; i < m.identities.Count(); ++i)
    {
        hlight::LumpIdentity id = m.identities[i]; int j = i;
        while (j > 0 && m.identities[j-1].lump > id.lump) { m.identities[j] = m.identities[j-1]; --j; }
        m.identities[j] = id;
    }
}
// Checked round-to-nearest-even IEEE binary16. Radiance must never be clipped
// to the half maximum or silently made black after an overflow/NaN.
bool Half(float value, uint16 &out)
{
    if (!ShadowMap_IsFiniteFloat(value) || value < 0 || value > 65504.0f) return false;
    if (value == 0) { out = 0; return true; }
    uint32 bits; memcpy(&bits,&value,4);
    int exponent = int((bits>>23)&255)-127+15;
    uint32 mantissa = bits&0x7fffff;
    if (exponent <= 0)
    {
        if (exponent < -10) { out = 0; return true; }
        mantissa |= 0x800000;
        const int shift = 14-exponent;
        const uint32 truncated = mantissa>>shift, remainder = mantissa&((1u<<shift)-1), midpoint = 1u<<(shift-1);
        out = uint16(truncated+(remainder > midpoint || (remainder == midpoint && (truncated&1))));
    }
    else
    {
        uint32 rounded = (uint32(exponent)<<10)+(mantissa>>13);
        const uint32 remainder = mantissa&8191;
        rounded += remainder > 4096 || (remainder == 4096 && (rounded&1));
        if (rounded >= 0x7c00) return false;
        out = uint16(rounded);
    }
    return true;
}
bool Append(CUtlVector<byte> &out, const void *data, uint64 count, uint64 &offset)
{
    const uint64 start = (uint64(out.Count())+15)&~uint64(15);
    if (start+count > hlight::kMaxFileBytes) return Fail("asset exceeds signed BSP-pak buffer limit");
    const int old = out.Count(); out.SetCount((int)(start+count));
    memset(out.Base()+old,0,(size_t)(start-old));
    if (count) memcpy(out.Base()+start,data,(size_t)count);
    offset = start; return true;
}
bool LoadAsset(const hlight::ManifestView &manifest)
{
    CUtlBuffer asset;
    if (!ReadFileFromPak(GetPakFile(),manifest.header->assetPath,false,asset)) return Fail("manifest pak asset is missing");
    hlight::FileView file; char error[256];
    if (!hlight::ValidateFile(asset.Base(),asset.TellPut(),file,error,sizeof(error))) return Fail(error);
    if (!hlight::ValidateManifestAsset(file,manifest,error,sizeof(error))) return Fail(error);
    s_Density = file.header->density; V_strncpy(s_AssetPath,manifest.header->assetPath,sizeof(s_AssetPath));
    for (int mi = 0; mi < 2; ++mi)
    {
        if (!manifest.mode[mi].runtime) continue;
        const hlight::ManifestModeDisk &mm = *manifest.mode[mi].record;
        if (mm.assetMode >= file.header->modeCount) return Fail("manifest asset mode is absent");
        const hlight::ModeView &v = file.mode[mm.assetMode]; const hlight::ModeDisk &r = *v.record;
        if (r.faceLump != mm.faceLump || r.lightingLump != mm.lightingLump || r.facesCRC32 != mm.facesCRC32 ||
            r.lightingBytes != mm.lightingBytes || r.lightingCRC32 != mm.lightingCRC32) return Fail("manifest/asset identity mismatch");
        const CUtlVector<byte> &rgb = mi ? dlightdataHDR : dlightdataLDR;
        const int faces = mi && numfaces_hdr ? numfaces_hdr : numfaces;
        if (rgb.Count() != r.lightingBytes || ShadowMap_CRC32(rgb.Base(),rgb.Count()) != r.lightingCRC32 ||
            uint32(faces) != r.faceCount || ShadowMap_CRC32(mi && numfaces_hdr ? dfaces_hdr : dfaces,faces*sizeof(dface_t)) != r.facesCRC32)
            return Fail("converted mode does not match loaded BSP lighting/face bytes");
        const int lights = mi ? numworldlightsHDR : numworldlightsLDR;
        if (ShadowMap_CRC32(mi ? dworldlightsHDR : dworldlightsLDR,lights*sizeof(dworldlight_t)) != mm.worldlightsCRC32)
            return Fail("converted worldlights CRC mismatch");
        ModeStorage &m = s_Mode[mi];
        GeometryIdentities(m);
        for (int i = 0; i < m.identities.Count(); ++i)
        {
            uint32 j = 0; while (j < r.identityCount && v.identities[j].lump != m.identities[i].lump) ++j;
            if (j == r.identityCount || memcmp(&m.identities[i],&v.identities[j],sizeof(hlight::LumpIdentity)))
                return Fail("effective geometry identity mismatch");
        }
        m.identities.RemoveAll(); m.record = r; m.active = true;
        m.identities.AddMultipleToTail(r.identityCount,v.identities);
        m.models.AddMultipleToTail(r.modelCount,v.models); m.faces.AddMultipleToTail(r.faceCount,v.faces);
        m.tiles.AddMultipleToTail(r.tileCount,v.tiles); m.pages.AddMultipleToTail(r.pageCount,v.pages);
        for (uint32 p = 0; p < r.pageCount; ++p)
        {
            m.pages[p].pixelsOffset = m.pixels.Count();
            m.pixels.AddMultipleToTail((int)v.pages[p].pixelsBytes,file.base+v.pages[p].pixelsOffset);
        }
        m.lights.AddMultipleToTail(manifest.mode[mi].lightCount,manifest.mode[mi].lights);
    }
    return true;
}
}

bool ReSTIR_PrepareShadowMapStorage(const ReSTIROptions &options)
{
    s_Prepared = false; s_Finalized = false; s_Density = 0; memset(s_AssetPath,0,sizeof(s_AssetPath));
    for (int i = 0; i < 2; ++i) s_Mode[i].Clear();
    GameLumpHandle_t h = g_GameLumps.GetGameLumpHandle(GAMELUMP_RESTIR_SHADOWMAPS);
    if (h != g_GameLumps.InvalidGameLump())
    {
        char error[256]; const uint32 version = g_GameLumps.GetGameLumpVersion(h);
        if (version == hlight::kManifestVersion)
        {
            hlight::ManifestView manifest;
            if (!hlight::ValidateManifest(g_GameLumps.GetGameLump(h),g_GameLumps.GameLumpSize(h),g_LevelFlags,manifest,error,sizeof(error))) return Fail(error);
            if (!LoadAsset(manifest)) return false;
        }
        else if (version == GAMELUMP_RESTIR_SHADOWMAPS_VERSION)
        {
            // Frozen v3 is admitted only as an input for a clean rebake. It is
            // never exported, extended or used by the new lighting route.
            ShadowMapValidateInput in; in.lump = g_GameLumps.GetGameLump(h); in.lumpBytes = g_GameLumps.GameLumpSize(h);
            in.lumpVersion = version; in.levelFlags = g_LevelFlags; in.checkMask = SHADOWMAP_CHECK_ALL;
            in.models = dmodels; in.modelCount = nummodels;
            in.texinfos = texinfo.Base(); in.texinfoCount = texinfo.Count();
            for (int mi = 0; mi < 2; ++mi)
            {
                const CUtlVector<byte> &rgb = mi ? dlightdataHDR : dlightdataLDR;
                ShadowMapValidateMode &vm = in.mode[mi]; vm.available = rgb.Count() > 0;
                vm.faces = mi && numfaces_hdr ? dfaces_hdr : dfaces;
                vm.faceCount = mi && numfaces_hdr ? numfaces_hdr : numfaces;
                vm.lightingBytes = rgb.Count(); vm.lightingCRC32 = ShadowMap_CRC32(rgb.Base(),rgb.Count());
                vm.facesCRC32 = ShadowMap_CRC32(vm.faces,vm.faceCount*sizeof(dface_t));
                vm.worldlightsCRC32 = ShadowMap_CRC32(mi ? dworldlightsHDR : dworldlightsLDR,
                    (mi ? numworldlightsHDR : numworldlightsLDR)*sizeof(dworldlight_t));
            }
            ShadowMapLumpView old;
            if (!ShadowMap_ValidateLump(in,old,error,sizeof(error))) return Fail(error);
        }
        else return Fail("unsupported existing selected-light manifest version");
    }
    else if (g_LevelFlags & (ShadowMap_LevelFlagDirect(0)|ShadowMap_LevelFlagDirect(1))) return Fail("selected-direct flags have no manifest");
    // Every conversion/reconversion is paired. Drop the preexisting opposite
    // enhanced mode rather than retain an asset at a different requested density.
    if (options.shadowMaps)
    {
        s_Mode[SelectedMode()].Clear();
        if (s_Density && s_Density != uint32(options.highresDensity)) s_Mode[1-SelectedMode()].Clear();
        s_Density = options.highresDensity;
    }
    s_Prepared = true; return true;
}

bool ReSTIR_InvalidateShadowMapTopology(const ReSTIROptions &options)
{
    (void)options;
    if (!s_Prepared) return Fail("storage not prepared before topology change");
    // An ordinary rebake replaces both selected-direct modes. Their metadata
    // must not survive a change in the face/grid topology.
    for (int i = 0; i < 2; ++i) s_Mode[i].Clear();
    s_Finalized = false; return true;
}

bool ReSTIR_StageReceiverPakFile(const char *name, const void *data, int bytes, bool compress)
{
    if (!data || bytes <= 0) return Fail("invalid staged pak member");
    AddBufferToPak(GetPakFile(),name,const_cast<void *>(data),bytes,false,compress ? IZip::eCompressionType_LZMA : IZip::eCompressionType_None);
    CUtlBuffer copied;
    if (!ReadFileFromPak(GetPakFile(),name,false,copied) || copied.TellPut() != bytes || memcmp(data,copied.Base(),bytes))
        return Fail("staged pak member failed lossless readback");
    return true;
}

bool ReSTIR_CaptureHighres(const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result,
    ReSTIRHighresColorFn color, void *context)
{
    if (!s_Prepared) return Fail("capture without prepared storage");
    s_Finalized = false;
    if (!options.shadowMaps)
    {
        for (int i = 0; i < 2; ++i) s_Mode[i].Clear();
        return true;
    }
    ModeStorage &m = s_Mode[SelectedMode()]; m.Clear(); m.active = true; m.lights.AddVectorToTail(scene.shadowLights);
    const int faceCount = SelectedFaces();
    if (nummodels <= 0 || nummodels > MAX_MAP_MODELS || faceCount <= 0 || faceCount > MAX_MAP_FACES ||
        scene.dfaceToFace.Count() != faceCount || !color) return Fail("invalid model/face capture domain");
    m.faces.SetCount(faceCount); m.models.SetCount(nummodels);
    int next = 0;
    for (int i = 0; i < nummodels; ++i)
    {
        const dmodel_t &model = dmodels[i]; hlight::ModelDisk &out = m.models[i]; memset(&out,0,sizeof(out));
        if (model.firstface != next || model.numfaces < 0 || (int64)next+model.numfaces > faceCount) return Fail("nonpartitioning BSP model face ranges");
        out.modelIndex = i; out.firstFace = next; out.faceCount = model.numfaces;
        // This is the exact forward transform used to construct the bake scene,
        // not a guess from renderer geometry or the BSP model's auxiliary origin.
        Vector bakedOrigin;
        ReSTIR_SceneFaceOrigin(i, bakedOrigin);
        if (!bakedOrigin.IsValid()) return Fail("nonfinite baked model origin");
        out.flags = hlight::kModelBakedPoseKnown;
        out.bakedModelToWorld[0] = out.bakedModelToWorld[5] = out.bakedModelToWorld[10] = 1;
        out.bakedModelToWorld[3] = bakedOrigin.x; out.bakedModelToWorld[7] = bakedOrigin.y; out.bakedModelToWorld[11] = bakedOrigin.z;
        next += model.numfaces;
    }
    if (next != faceCount) return Fail("BSP models do not cover the face domain");
    const bool sun = m.lights.Count() && m.lights[0].light.type == emit_skylight;
    if (sun && result.sunVisibility.Count() != scene.luxels.Count()) return Fail("sun visibility does not cover the dense geometric grid");
    if (!sun && result.sunVisibility.Count()) return Fail("unexpected sun visibility without selected sun");
    uint32 pageSide = hlight::kDefaultPageSize;
    for (int i = 0; i < scene.faces.Count(); ++i)
    {
        const ReSTIRGpuFace &f = scene.faces[i];
        while (pageSide < uint32(f.luxelW+2) || pageSide < uint32(f.luxelH+2)) pageSide *= 2;
        if (pageSide > hlight::kMaxPageSize) return Fail("requested dense tile exceeds maximum supported page");
    }
    const uint64 pageBytes = uint64(pageSide)*pageSide*8;
    uint32 x = 0, y = 0, rowHeight = 0; int page = -1, owner = 0;
    for (int i = 0; i < faceCount; ++i)
    {
        const dface_t &native = g_pFaces[i]; hlight::FaceDisk &out = m.faces[i]; memset(&out,0,sizeof(out));
        while (owner+1 < nummodels && i >= dmodels[owner].firstface+dmodels[owner].numfaces) ++owner;
        out.faceOrdinal = i; out.modelIndex = owner;
        for (int axis = 0; axis < 2; ++axis)
        {
            out.nativeMins[axis] = native.m_LightmapTextureMinsInLuxels[axis];
            // Native unlit faces can carry -1 extents; canonicalize to zero
            // only for these nonparticipating records, never for a lit face.
            out.nativeExtents[axis] = MAX(0,native.m_LightmapTextureSizeInLuxels[axis]);
        }
        out.highWidth = out.nativeExtents[0]*s_Density+1; out.highHeight = out.nativeExtents[1]*s_Density+1;
        for (int s = 0; s < 4; ++s) out.styles[s] = 255;
        for (int t = 0; t < 16; ++t) out.tiles[t] = hlight::kMissing;
        const int sf = scene.dfaceToFace[i]; if (sf < 0) continue;
        if (sf >= scene.faces.Count()) return Fail("invalid dense dface mapping");
        const ReSTIRGpuFace &f = scene.faces[sf];
        if (f.dface != i || f.numStyles < 1 || f.numStyles > 4 || f.styles[0] != 0 ||
            (f.numChannels != 1 && f.numChannels != 4) || out.highWidth != uint32(f.luxelW) || out.highHeight != uint32(f.luxelH))
            return Fail("dense face style/plane/grid mismatch");
        const int64 luxels = (int64)f.luxelW*f.luxelH;
        if (f.firstOutput < 0 || f.firstLuxel < 0 || (int64)f.firstOutput+luxels*f.numStyles*f.numChannels > result.radiance.Count() ||
            (int64)f.firstLuxel+luxels > result.luxelValid.Count() ||
            (int64)f.firstLuxel+luxels > scene.luxels.Count()) return Fail("dense result range mismatch");
        out.flags = hlight::kFaceHasLighting | (f.numChannels == 4 ? hlight::kFaceBumped : 0) |
            (f.flags & RESTIR_FACE_DISP ? hlight::kFaceDisplacement : 0) | (sun ? hlight::kFaceHasSun : 0);
        out.styleCount = f.numStyles;
        for (int s = 0; s < f.numStyles; ++s)
        {
            out.styles[s] = f.styles[s];
            for (int p = 0; p < f.numChannels; ++p)
            {
                const uint32 w = f.luxelW+2, h = f.luxelH+2;
                if (x+w > pageSide) { x = 0; y += rowHeight; rowHeight = 0; }
                if (page < 0 || y+h > pageSide)
                {
                    if (uint64(m.pixels.Count())+pageBytes > hlight::kMaxFileBytes) return Fail("dense page payload exceeds BSP-pak signed limit");
                    hlight::PageDisk record; memset(&record,0,sizeof(record));
                    record.width = record.height = pageSide; record.format = hlight::kRGBA16Float;
                    record.firstTile = m.tiles.Count(); record.pixelsOffset = m.pixels.Count(); record.pixelsBytes = pageBytes;
                    const int old = m.pixels.Count(); m.pixels.SetCount(old+(int)pageBytes); memset(m.pixels.Base()+old,0,(size_t)pageBytes);
                    page = m.pages.AddToTail(record); x = y = rowHeight = 0;
                }
                hlight::TileDisk tile = {uint32(i),uint32(s),uint32(p),uint32(page),x+1,y+1,uint32(f.luxelW),uint32(f.luxelH)};
                out.tiles[s*4+p] = m.tiles.AddToTail(tile); ++m.pages[page].tileCount;
                uint16 *pixels = reinterpret_cast<uint16 *>(m.pixels.Base()+m.pages[page].pixelsOffset);
                for (uint32 yy = 1; yy < h-1; ++yy)
                for (uint32 xx = 1; xx < w-1; ++xx)
                {
                    const int local = int(yy-1)*f.luxelW + int(xx-1);
                    Vector rgb = result.radiance[f.firstOutput+(s*f.numChannels+p)*(int)luxels+local];
                    if (!rgb.IsValid() || rgb.x < 0 || rgb.y < 0 || rgb.z < 0) return Fail("nonfinite/negative dense transport RGB");
                    rgb = color(context,sf,local,p,rgb)/255.0f;
                    const float alpha = s == 0 && p == 0 ? (sun ? result.sunVisibility[f.firstLuxel+local] : 1.0f) : 0.0f;
                    uint16 *dest = pixels+(uint64(y+yy)*pageSide+x+xx)*4;
                    if (alpha < 0 || alpha > 1 || !Half(rgb.x,dest[0]) || !Half(rgb.y,dest[1]) || !Half(rgb.z,dest[2]) || !Half(alpha,dest[3]))
                    {
                        Warning("Hlight: face=%d style=%d plane=%d dense=(%d,%d) RGBA=(%g,%g,%g,%g) is not representable\n",
                            i,f.styles[s],p,local%f.luxelW,local/f.luxelW,rgb.x,rgb.y,rgb.z,alpha);
                        return false;
                    }
                }
                for (uint32 yy = 1; yy < h-1; ++yy)
                {
                    uint16 *row = pixels+(uint64(y+yy)*pageSide+x)*4;
                    memcpy(row,row+4,8);
                    memcpy(row+(w-1)*4,row+(w-2)*4,8);
                }
                memcpy(pixels+(uint64(y)*pageSide+x)*4,pixels+(uint64(y+1)*pageSide+x)*4,w*8);
                memcpy(pixels+(uint64(y+h-1)*pageSide+x)*4,pixels+(uint64(y+h-2)*pageSide+x)*4,w*8);
                x += w; rowHeight = MAX(rowHeight,h);
            }
        }
    }
    m.record.faceCount = m.faces.Count(); m.record.modelCount = m.models.Count();
    m.record.tileCount = m.tiles.Count(); m.record.pageCount = m.pages.Count(); m.record.identityCount = 14;
    if (!s_AssetPath[0])
    {
        char basename[256]; V_FileBase(options.mapPath.String(),basename,sizeof(basename)); V_strlower(basename);
        if (!basename[0] || strlen(basename)+13 > sizeof(s_AssetPath)) return Fail("map basename cannot fit the manifest asset path");
        V_snprintf(s_AssetPath,sizeof(s_AssetPath),"maps/%s.hlight",basename);
    }
    Msg("Hlight: density=%u independent cells=%d grid samples=%d styles/planes=%d pages=%d page-side=%u GPU/pixel-bytes=%d\n",
        s_Density,scene.samples.Count(),scene.luxels.Count(),m.tiles.Count(),m.pages.Count(),pageSide,m.pixels.Count());
    return true;
}

int ReSTIR_SelectedLightCount() { return s_Mode[SelectedMode()].lights.Count(); }
bool ReSTIR_FinishSelectedLighting(const ReSTIROptions &options, int selected)
{
    if (selected != ReSTIR_SelectedLightCount()) return Fail("selected light export/capture count mismatch");
    const int mode = SelectedMode(); g_LevelFlags &= ~ShadowMap_LevelFlagDirect(mode);
    if (s_Mode[mode].active) g_LevelFlags |= ShadowMap_LevelFlagDirect(mode);
    if (!options.shadowMaps)
    {
        g_LevelFlags &= ~(ShadowMap_LevelFlagDirect(0)|ShadowMap_LevelFlagDirect(1));
        if (s_AssetPath[0]) RemoveFileFromPak(GetPakFile(),s_AssetPath);
    }
    return true;
}

bool ReSTIR_WriteShadowMapSidecar(const ReSTIROptions &options)
{
    (void)options;
    if (s_Finalized) return true;
    uint32 mask = 0; for (int mi = 0; mi < 2; ++mi) if (s_Mode[mi].active) mask |= 1u<<mi;
    // Clear stale flags for modes dropped by a clean cutover.
    g_LevelFlags &= ~(ShadowMap_LevelFlagDirect(0)|ShadowMap_LevelFlagDirect(1));
    for (int mi = 0; mi < 2; ++mi) if (mask & (1u<<mi)) g_LevelFlags |= ShadowMap_LevelFlagDirect(mi);
    if (!mask)
    {
        DeleteManifest();
        if (s_AssetPath[0]) RemoveFileFromPak(GetPakFile(),s_AssetPath);
        s_Finalized = true; return true;
    }
    hlight::FileHeader header; memset(&header,0,sizeof(header));
    header.magic = hlight::kMagic; header.version = hlight::kVersion; header.headerBytes = sizeof(header);
    header.endian = hlight::kEndian; header.density = s_Density;
    int modeOrder[2]; for (int mi = 0; mi < 2; ++mi) if (mask & (1u<<mi)) { FinalIdentities(s_Mode[mi],mi); modeOrder[header.modeCount++] = mi; }
    if (header.modeCount == 2 && (s_Mode[modeOrder[0]].record.faceLump > s_Mode[modeOrder[1]].record.faceLump ||
        (s_Mode[modeOrder[0]].record.faceLump == s_Mode[modeOrder[1]].record.faceLump &&
         s_Mode[modeOrder[0]].record.lightingLump > s_Mode[modeOrder[1]].record.lightingLump))) V_swap(modeOrder[0],modeOrder[1]);
    CUtlVector<byte> file; file.SetCount(sizeof(header)); memset(file.Base(),0,file.Count());
    hlight::ModeDisk records[2]; memset(records,0,sizeof(records));
    if (!Append(file,records,uint64(header.modeCount)*sizeof(hlight::ModeDisk),header.modesOffset)) return false;
    for (uint32 i = 0; i < header.modeCount; ++i)
    {
        ModeStorage &m = s_Mode[modeOrder[i]]; hlight::ModeDisk &r = records[i]; r = m.record;
        if (!Append(file,m.identities.Base(),uint64(m.identities.Count())*sizeof(hlight::LumpIdentity),r.identitiesOffset) ||
            !Append(file,m.models.Base(),uint64(m.models.Count())*sizeof(hlight::ModelDisk),r.modelsOffset) ||
            !Append(file,m.faces.Base(),uint64(m.faces.Count())*sizeof(hlight::FaceDisk),r.facesOffset) ||
            !Append(file,m.tiles.Base(),uint64(m.tiles.Count())*sizeof(hlight::TileDisk),r.tilesOffset) ||
            !Append(file,m.pages.Base(),uint64(m.pages.Count())*sizeof(hlight::PageDisk),r.pagesOffset)) return false;
        for (int p = 0; p < m.pages.Count(); ++p)
        {
            hlight::PageDisk page = m.pages[p];
            if (!Append(file,m.pixels.Base()+page.pixelsOffset,page.pixelsBytes,page.pixelsOffset)) return false;
            memcpy(file.Base()+r.pagesOffset+p*sizeof(page),&page,sizeof(page));
        }
    }
    memcpy(file.Base()+header.modesOffset,records,header.modeCount*sizeof(hlight::ModeDisk));
    header.fileBytes = file.Count(); memcpy(file.Base(),&header,sizeof(header));
    reinterpret_cast<hlight::FileHeader *>(file.Base())->crc32 = hlight::FileCRC32(file.Base(),file.Count());
    hlight::FileView verified; char error[256];
    if (!hlight::ValidateFile(file.Base(),file.Count(),verified,error,sizeof(error))) return Fail(error);
    hlight::ManifestDisk manifest; memset(&manifest,0,sizeof(manifest));
    manifest.runtimeModeMask = mask; manifest.assetVersion = hlight::kVersion; V_strncpy(manifest.assetPath,s_AssetPath,sizeof(manifest.assetPath));
    uint64 total = sizeof(manifest); for (int mi = 0; mi < 2; ++mi) total += uint64(s_Mode[mi].lights.Count())*sizeof(ShadowMapLightDisk);
    if (total > hlight::kMaxFileBytes) return Fail("selected-light manifest size overflow");
    CUtlVector<byte> bytes; bytes.SetCount((int)total); memset(bytes.Base(),0,bytes.Count());
    uint32 cursor = sizeof(manifest);
    for (int mi = 0; mi < 2; ++mi)
    {
        hlight::ManifestModeDisk &mm = manifest.mode[mi]; mm.sunLightIndex = -1;
        if (!(mask & (1u<<mi))) continue;
        uint32 ai = 0; while (modeOrder[ai] != mi) ++ai;
        const hlight::ModeDisk &r = records[ai];
        mm.faceLump = r.faceLump; mm.lightingLump = r.lightingLump; mm.worldlightsLump = mi ? LUMP_WORLDLIGHTS_HDR : LUMP_WORLDLIGHTS;
        mm.lightingBytes = (uint32)r.lightingBytes; mm.lightingCRC32 = r.lightingCRC32; mm.facesCRC32 = r.facesCRC32;
        mm.worldlightsCRC32 = ShadowMap_CRC32(mi ? dworldlightsHDR : dworldlightsLDR,(mi ? numworldlightsHDR : numworldlightsLDR)*sizeof(dworldlight_t));
        mm.assetMode = ai; mm.lightsOffset = cursor; mm.lightCount = s_Mode[mi].lights.Count();
        mm.sunLightIndex = mm.lightCount && s_Mode[mi].lights[0].light.type == emit_skylight ? 0 : -1;
        if (mm.lightCount) memcpy(bytes.Base()+cursor,s_Mode[mi].lights.Base(),mm.lightCount*sizeof(ShadowMapLightDisk));
        cursor += mm.lightCount*sizeof(ShadowMapLightDisk);
    }
    manifest.byteSize = bytes.Count(); memcpy(bytes.Base(),&manifest,sizeof(manifest));
    hlight::ManifestView mv;
    if (!hlight::ValidateManifest(bytes.Base(),bytes.Count(),g_LevelFlags,mv,error,sizeof(error))) return Fail(error);
    if (!hlight::ValidateManifestAsset(verified,mv,error,sizeof(error))) return Fail(error);
    if (!ReSTIR_StageReceiverPakFile(s_AssetPath,file.Base(),file.Count(),true)) return false;
    DeleteManifest(); GameLumpHandle_t h = g_GameLumps.CreateGameLump(GAMELUMP_RESTIR_SHADOWMAPS,bytes.Count(),0,hlight::kManifestVersion);
    if (h == g_GameLumps.InvalidGameLump() || !g_GameLumps.GetGameLump(h)) return Fail("cannot allocate native selected-light manifest");
    memcpy(g_GameLumps.GetGameLump(h),bytes.Base(),bytes.Count()); s_Finalized = true;
    Msg("Hlight: checked pak asset %s (%d bytes), rshd v4 (%d bytes), explicit mode mask=%u\n",s_AssetPath,file.Count(),bytes.Count(),mask);
    return true;
}
