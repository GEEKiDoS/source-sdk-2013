//========= Copyright Valve Corporation, All rights reserved. ============//
#include "hlight_output.h"
#include "bsp_output.h"
#include "restir_scene_internal.h"
#include "restir_staticprops.h"
#include "restir_baked_direct.h"
#include "restir_byte_buffer.h"
#include "ambient_probes.h"
#include "vrad_restir.h"
#include "bsplib.h"
#include "tier1/utlbuffer.h"
#include "gamebspfile.h"
#include <float.h>
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
    hlight::VisibilitySetDisk visibility;
    CUtlVector<hlight::FaceVisibilityDisk> visibilityFaces;
    CUtlVector<hlight::VisibilityEntryDisk> visibilityEntries;
    CUtlVector<hlight::PropVisibilityDisk> visibilityProps;
    CUtlVector<hlight::PropMeshVisibilityDisk> visibilityMeshes;
    CUtlVector<hlight::UnbakedFaceDisk> unbakedFaces;
    CUtlVector<uint32> unbakedLightIndices;
    CUtlVector<byte> visibilityPayload, faceSupport;
    bool visibilityPropsCaptured, visibilityShared;
    ReSTIRAmbientProbeGrid probes; bool probesCaptured;
    void Clear()
    {
        active = false;
        memset(&record,0,sizeof(record)); identities.Purge(); models.Purge(); faces.Purge();
        tiles.Purge(); pages.Purge(); pixels.Purge(); lights.Purge();
        memset(&visibility,0,sizeof(visibility)); visibility.sunLightIndex = -1;
        visibilityFaces.Purge(); visibilityEntries.Purge(); visibilityProps.Purge(); visibilityMeshes.Purge();
        unbakedFaces.Purge(); unbakedLightIndices.Purge();
        visibilityPayload.Purge(); faceSupport.Purge(); visibilityPropsCaptured = visibilityShared = false;
        probes.Purge(); probesCaptured = false;
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
// maps/<name>.hprobe: the ambient probe grids paired to the .hlight asset. s_AssetPath is a manifest-validated or
// generated maps/<name>.hlight, so the suffix swap cannot fail.
void ProbePath(char (&out)[256])
{
    hprobe::ProbeAssetPath(s_AssetPath,out,sizeof(out));
}
// The .hlight and its paired .hprobe leave the pak together.
void RemoveAssets()
{
    char probe[256]; ProbePath(probe);
    RemoveFileFromPak(GetPakFile(),s_AssetPath);
    RemoveFileFromPak(GetPakFile(),probe);
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
    if (!ReSTIR_EnsureByteCapacity(out,start+count)) return Fail("asset byte capacity exceeds signed limit");
    const int old = out.Count(); out.SetCount((int)(start+count));
    memset(out.Base()+old,0,(size_t)(start-old));
    if (count) memcpy(out.Base()+start,data,(size_t)count);
    offset = start; return true;
}
// The grid section is written straight from the vectors: one aligned section, no staging copy of up to 256 MiB.
bool AppendProbeGrid(CUtlVector<byte> &out, const ReSTIRAmbientProbeGrid &g, uint64 &offset)
{
    const uint64 start = (uint64(out.Count())+15)&~uint64(15), bytes = hprobe::GridBytes(g.grid);
    if (start+bytes > hlight::kMaxFileBytes) return Fail("probe asset exceeds signed BSP-pak buffer limit");
    if (!ReSTIR_EnsureByteCapacity(out,start+bytes)) return Fail("probe asset byte capacity exceeds signed limit");
    const int old = out.Count(); out.SetCount((int)(start+bytes));
    memset(out.Base()+old,0,(size_t)(start-old));
    byte *at = out.Base()+start;
    memcpy(at,&g.grid,sizeof(g.grid)); at += sizeof(g.grid);
    memcpy(at,g.indirection.Base(),g.indirection.Count()*sizeof(uint32)); at += g.indirection.Count()*sizeof(uint32);
    memcpy(at,g.dc.Base(),g.dc.Count()*sizeof(uint32)); at += g.dc.Count()*sizeof(uint32);
    memcpy(at,g.bands.Base(),g.bands.Count()); at += g.bands.Count();
    memcpy(at,g.validity.Base(),g.validity.Count());
    offset = start; return true;
}
// The .hprobe pak member: one grid per hlight mode, bound to that exact .hlight by its CRC and each mode's identity.
bool BuildProbeAsset(const hlight::FileView &lightmaps, const int modeOrder[2], CUtlVector<byte> &file)
{
    const uint32 modeCount = lightmaps.header->modeCount;
    uint64 fileBytes = sizeof(hprobe::FileHeader);
    if (!ReSTIR_AddByteSectionSize(fileBytes,uint64(modeCount)*sizeof(hprobe::ModeDisk)))
        return Fail("probe asset exceeds signed BSP-pak buffer limit");
    for (uint32 i = 0; i < modeCount; ++i)
    {
        const ModeStorage &m = s_Mode[modeOrder[i]];
        if (!m.probesCaptured) return Fail("mode lacks ambient probe grid; rebake with -restir_shadowmaps");
        if (!ReSTIR_AddByteSectionSize(fileBytes,hprobe::GridBytes(m.probes.grid)))
            return Fail("probe asset exceeds signed BSP-pak buffer limit");
    }
    hprobe::FileHeader header; memset(&header,0,sizeof(header));
    header.magic = hprobe::kMagic; header.version = hprobe::kVersion; header.headerBytes = sizeof(header);
    header.endian = hprobe::kEndian; header.hlightCRC32 = lightmaps.header->crc32; header.modeCount = modeCount;
    file.EnsureCapacity(int(fileBytes));
    file.SetCount(sizeof(header)); memset(file.Base(),0,file.Count());
    hprobe::ModeDisk records[2]; memset(records,0,sizeof(records));
    if (!Append(file,records,uint64(modeCount)*sizeof(hprobe::ModeDisk),header.modesOffset)) return false;
    for (uint32 i = 0; i < modeCount; ++i)
    {
        const hlight::ModeDisk &identity = *lightmaps.mode[i].record; hprobe::ModeDisk &r = records[i];
        const ReSTIRAmbientProbeGrid &grid = s_Mode[modeOrder[i]].probes;
        r.faceLump = identity.faceLump; r.lightingLump = identity.lightingLump;
        r.facesCRC32 = identity.facesCRC32; r.lightingCRC32 = identity.lightingCRC32;
        r.lightingBytes = identity.lightingBytes; r.brickCount = grid.grid.brickCount;
        if (!AppendProbeGrid(file,grid,r.gridOffset)) return false;
    }
    memcpy(file.Base()+header.modesOffset,records,modeCount*sizeof(hprobe::ModeDisk));
    header.fileBytes = file.Count(); memcpy(file.Base(),&header,sizeof(header));
    reinterpret_cast<hprobe::FileHeader *>(file.Base())->crc32 = hprobe::FileCRC32(file.Base(),file.Count());
    hprobe::FileView probes; char error[256];
    if (!hprobe::ValidateFile(file.Base(),file.Count(),probes,error,sizeof(error)) ||
        !hprobe::ValidatePair(probes,lightmaps,error,sizeof(error))) return Fail(error);
    return true;
}
void CopyProbeGrid(const hprobe::ModeView &v, ReSTIRAmbientProbeGrid &out)
{
    const uint64 texels = hprobe::AtlasTexels(*v.grid);
    out.grid = *v.grid;
    out.indirection.CopyArray(v.indirection,int(hprobe::IndirectionCount(*v.grid)));
    out.dc.CopyArray(v.dc,int(texels));
    out.bands.CopyArray(v.bands,int(texels*hprobe::kBands*4));
    out.validity.CopyArray(v.validity,int(texels));
}
bool AddVisibilityPlane(ModeStorage &m, uint32 light, const byte *values, uint32 count)
{
    if (!count || !values) return Fail("empty visibility receiver plane");
    hlight::VisibilityEntryDisk e = {light,values[0],0,0};
    bool uniform = true;
    for (uint32 i = 1; i < count; ++i) if (values[i] != values[0]) { uniform = false; break; }
    if (!uniform)
    {
        const uint64 at = (uint64(m.visibilityPayload.Count())+3)&~uint64(3);
        if (at+count > hlight::kMaxFileBytes) return Fail("visibility payload exceeds signed BSP-pak limit");
        if (!ReSTIR_EnsureByteCapacity(m.visibilityPayload,at+count)) return Fail("visibility byte capacity exceeds signed limit");
        const int old = m.visibilityPayload.Count(); m.visibilityPayload.SetCount(int(at+count));
        memset(m.visibilityPayload.Base()+old,0,size_t(at-old));
        memcpy(m.visibilityPayload.Base()+at,values,count);
        e.encoding = hlight::kVisibilityDense; e.payloadByteOffset = uint32(at); e.sampleCount = count;
    }
    m.visibilityEntries.AddToTail(e); return true;
}
// Bounding-box distance rejects only finite spherical support. Cone intersection
// is deliberately retained: corner-only cone tests can miss interior influence.
// Unbounded authored lights are never omitted, even if their fade could bound them.
bool FaceSupportsLight(const ShadowMapLightDisk &l, const Vector &mins, const Vector &maxs)
{
    if (l.light.radius <= 0) return true;
    double distance2 = 0;
    for (int a = 0; a < 3; ++a)
    {
        const double d = l.light.origin[a] < mins[a] ? double(mins[a])-l.light.origin[a] :
            (l.light.origin[a] > maxs[a] ? double(l.light.origin[a])-maxs[a] : 0.0);
        distance2 += d*d;
    }
    return distance2 <= double(l.light.radius)*l.light.radius;
}
struct FaceDirectStorage
{
    int styles[MAXLIGHTMAPS], sourceSlots[MAXLIGHTMAPS], count;
    CUtlVector<Vector> values;
    CUtlVector<uint32> unbaked;
};
bool BuildFaceDirect(const ReSTIRScene &scene, const ReSTIRLightmapResult &result,
    const ReSTIRGpuFace &face, FaceDirectStorage &out)
{
    out.count = 0;
    const uint64 sampleCount = uint64(face.luxelW)*face.luxelH;
    if (sampleCount*face.numChannels*MAXLIGHTMAPS > INT_MAX)
        return Fail("baked direct face scratch exceeds signed sample indexing");
    const int samples = int(sampleCount), planeValues = samples*face.numChannels;
    for (int s = 0; s < face.numStyles; ++s)
    {
        if (!scene.IsReceiverStyle(face.styles[s])) continue;
        if (out.count == MAXLIGHTMAPS)
        {
            Warning("Hlight: receiver style overflow face=%d styles=%d,%d,%d,%d + %d (%s)\n",
                face.dface,out.styles[0],out.styles[1],out.styles[2],out.styles[3],face.styles[s],g_bHDR ? "HDR" : "LDR");
            return Fail("dense face exceeds four receiver styles");
        }
        out.styles[out.count] = face.styles[s]; out.sourceSlots[out.count++] = s;
    }
    out.values.SetCount(out.count*planeValues);
    if (out.values.Count()) memset(out.values.Base(),0,out.values.Count()*sizeof(Vector));
    if (!scene.shadowLights.Count()) return true;
    if (scene.localDirectPositions.Count() != scene.luxels.Count() ||
        scene.localDirectNormals.Count() != scene.luxels.Count() ||
        uint64(scene.localDirectBumpNormals.Count()) != uint64(scene.luxels.Count())*3)
        return Fail("local direct surface/frame domain incomplete");
    CUtlVector<Vector> contribution; contribution.SetCount(planeValues);
    for (int li = 0; li < scene.shadowLights.Count(); ++li)
    {
        const ShadowMapLightDisk &light = scene.shadowLights[li];
        if (light.light.type == emit_skylight) continue;
        if (uint64(result.localVisibility.Count()) != uint64(scene.shadowLights.Count())*scene.luxels.Count())
            return Fail("local direct requires complete baked visibility");
        bool nonzero = false;
        for (int j = 0; j < samples; ++j)
        {
            const int luxel = face.firstLuxel+j;
            const byte visibility = result.localVisibility[li*scene.luxels.Count()+luxel];
            if (!visibility)
            {
                for (int p = 0; p < face.numChannels; ++p) contribution[p*samples+j] = vec3_origin;
                continue;
            }
            Vector L, radiance = ReSTIR_BakedLocalRadiance(light,scene.localDirectPositions[luxel],L);
            radiance *= float(visibility)/255.0f;
            const Vector N = scene.localDirectNormals[luxel];
            for (int p = 0; p < face.numChannels; ++p)
            {
                const Vector value = radiance*ReSTIR_BakedLocalAngular(L,p ? scene.localDirectBumpNormals[3*luxel+p-1] : N);
                if (!value.IsValid() || value.x < 0 || value.y < 0 || value.z < 0) return Fail("nonfinite baked local diffuse");
                contribution[p*samples+j] = value;
                nonzero = nonzero || value.x > 0 || value.y > 0 || value.z > 0;
            }
        }
        if (!nonzero) continue;
        int slot = 0; while (slot < out.count && out.styles[slot] != light.light.style) ++slot;
        if (slot == out.count)
        {
            if (out.count == MAXLIGHTMAPS)
            {
                out.unbaked.AddToTail(uint32(li));
                continue;
            }
            out.styles[slot] = light.light.style; out.sourceSlots[slot] = -1;
            for (int s = 0; s < face.numStyles; ++s) if (face.styles[s] == light.light.style) out.sourceSlots[slot] = s;
            ++out.count; out.values.SetCount(out.count*planeValues);
            memset(out.values.Base()+slot*planeValues,0,planeValues*sizeof(Vector));
        }
        for (int j = 0; j < planeValues; ++j) out.values[slot*planeValues+j] += contribution[j];
    }
    return true;
}
bool CaptureWorldVisibility(ModeStorage &m, const ReSTIRScene &scene, const ReSTIRLightmapResult &result)
{
    const uint64 samples = uint64(scene.luxels.Count())*m.lights.Count();
    const bool locals = m.lights.Count() > int(m.visibility.sunLightIndex >= 0);
    if ((locals && (samples > INT_MAX || result.localVisibility.Count() != samples)) ||
        (!locals && result.localVisibility.Count())) return Fail("local visibility incomplete geometric grid");
    m.visibility.selectedLightCount = m.lights.Count();
    m.visibility.selectedLightsCRC32 = ShadowMap_CRC32(m.lights.Base(),m.lights.Count()*sizeof(ShadowMapLightDisk));
    m.visibility.faceCount = m.faces.Count();
    const uint64 supportBytes = (uint64(m.faces.Count())*m.lights.Count()+7)/8;
    if (supportBytes > hlight::kMaxFileBytes) return Fail("visibility support directory exceeds signed limit");
    m.faceSupport.SetCount(int(supportBytes));
    if (supportBytes) memset(m.faceSupport.Base(),0,size_t(supportBytes));
    CUtlVector<Vector> faceMins, faceMaxs; faceMins.SetCount(m.faces.Count()); faceMaxs.SetCount(m.faces.Count());
    for (int i = 0; i < m.faces.Count(); ++i)
    {
        faceMins[i].Init(FLT_MAX,FLT_MAX,FLT_MAX); faceMaxs[i].Init(-FLT_MAX,-FLT_MAX,-FLT_MAX);
    }
    for (int ti = 0; ti < scene.triangles.Count(); ++ti)
    {
        const ReSTIRGpuTriangle &t = scene.triangles[ti];
        if (t.face < 0 || t.face >= m.faces.Count()) continue;
        for (int a = 0; a < 3; ++a)
        {
            faceMins[t.face][a] = MIN(faceMins[t.face][a],MIN(t.v0[a],MIN(t.v1[a],t.v2[a])));
            faceMaxs[t.face][a] = MAX(faceMaxs[t.face][a],MAX(t.v0[a],MAX(t.v1[a],t.v2[a])));
        }
    }
    int unbakedFace = 0;
    for (int i = 0; i < m.faces.Count(); ++i)
    {
        hlight::FaceVisibilityDisk fd = {uint32(i),uint32(m.visibilityEntries.Count()),0,0};
        const hlight::UnbakedFaceDisk *fallback = unbakedFace < m.unbakedFaces.Count() &&
            m.unbakedFaces[unbakedFace].faceOrdinal == uint32(i) ? &m.unbakedFaces[unbakedFace++] : NULL;
        const int sf = scene.dfaceToFace[i];
        if (sf >= 0 && locals)
        {
            const ReSTIRGpuFace &f = scene.faces[sf];
            const uint32 count = f.luxelW*f.luxelH;
            Vector mins = faceMins[i], maxs = faceMaxs[i];
            for (uint32 j = 0; j < count; ++j)
            {
                const Vector4D &o = scene.sunVisibilityOrigins[f.firstLuxel+j];
                for (int a = 0; a < 3; ++a) { mins[a] = MIN(mins[a],o[a]); maxs[a] = MAX(maxs[a],o[a]); }
            }
            for (int li = 0; li < m.lights.Count(); ++li)
            {
                bool unbaked = false;
                if (fallback)
                    for (uint32 j = 0; j < fallback->lightCount; ++j)
                        if (m.unbakedLightIndices[fallback->firstLightIndex+j] == uint32(li)) { unbaked = true; break; }
                if (li == m.visibility.sunLightIndex || (!unbaked && !FaceSupportsLight(m.lights[li],mins,maxs))) continue;
                const byte *plane = result.localVisibility.Base()+li*scene.luxels.Count()+f.firstLuxel;
                if (!AddVisibilityPlane(m,li,plane,count)) return false;
                const uint64 bit = uint64(i)*m.lights.Count()+li;
                m.faceSupport[int(bit>>3)] |= byte(1u<<(bit&7)); ++fd.entryCount;
            }
        }
        m.visibilityFaces.AddToTail(fd);
    }
    return true;
}
bool CapturePropVisibility(ModeStorage &m, int mode)
{
    if (m.visibilityPropsCaptured) return true;
    const ReSTIRPropVisibilityData *data = ReSTIR_GetStaticPropVisibility(mode != 0);
    if (!data || data->selectedLightCount != uint32(m.lights.Count())) return Fail("static prop visibility missing or selected-light mismatch");
    m.visibilityProps.AddVectorToTail(data->props);
    m.visibilityMeshes.AddVectorToTail(data->meshes);
    for (int mi = 0; mi < m.visibilityMeshes.Count(); ++mi)
    {
        hlight::PropMeshVisibilityDisk &mesh = m.visibilityMeshes[mi];
        const uint32 first = mesh.firstEntry; mesh.firstEntry = m.visibilityEntries.Count();
        for (uint32 ei = first; ei < first+mesh.entryCount; ++ei)
        {
            const hlight::VisibilityEntryDisk &e = data->entries[ei];
            if (e.encoding != hlight::kVisibilityDense || e.sampleCount != mesh.vertexCount ||
                uint64(e.payloadByteOffset)+e.sampleCount > uint32(data->payload.Count()))
                return Fail("invalid static prop visibility scatter");
            if (!AddVisibilityPlane(m,e.selectedLightIndex,data->payload.Base()+e.payloadByteOffset,e.sampleCount)) return false;
        }
        if (mesh.directPayloadBytes)
        {
            if (uint64(mesh.directPayloadByteOffset)+mesh.directPayloadBytes > uint32(data->payload.Count()))
                return Fail("invalid static prop direct scatter");
            const uint32 sourceOffset = mesh.directPayloadByteOffset;
            const uint64 aligned = (uint64(m.visibilityPayload.Count())+3)&~uint64(3);
            const uint64 end = aligned+mesh.directPayloadBytes;
            if (!ReSTIR_EnsureByteCapacity(m.visibilityPayload,end)) return Fail("static prop direct exceeds signed payload limit");
            const int old = m.visibilityPayload.Count(); m.visibilityPayload.SetCount(int(end));
            memset(m.visibilityPayload.Base()+old,0,size_t(aligned-old));
            memcpy(m.visibilityPayload.Base()+aligned,data->payload.Base()+sourceOffset,mesh.directPayloadBytes);
            mesh.directPayloadByteOffset = uint32(aligned);
        }
    }
    m.visibilityPropsCaptured = true;
    m.visibility.propCount = m.visibilityProps.Count(); m.visibility.meshCount = m.visibilityMeshes.Count();
    return true;
}
bool LoadAsset(const hlight::ManifestView &manifest, uint32 previousVersion = 0)
{
    CUtlBuffer asset;
    if (!ReadFileFromPak(GetPakFile(),manifest.header->assetPath,false,asset)) return Fail("manifest pak asset is missing");
    if (previousVersion)
    {
        if (asset.TellPut() < int(sizeof(hlight::FileHeader))) return Fail("previous visibility asset truncated");
        hlight::FileHeader &h = *static_cast<hlight::FileHeader *>(asset.Base());
        if (h.version != previousVersion || (previousVersion != 2 && previousVersion != 3) || h.crc32 != hlight::FileCRC32(asset.Base(),asset.TellPut()) ||
            !h.modeCount || h.modeCount > 4 || (h.modesOffset & 3) || h.modesOffset > uint32(asset.TellPut()) ||
            uint64(h.modeCount)*sizeof(hlight::ModeDisk) > uint32(asset.TellPut())-h.modesOffset)
            return Fail("previous visibility asset version/CRC/range mismatch");
        hlight::ModeDisk *modes = reinterpret_cast<hlight::ModeDisk *>(static_cast<byte *>(asset.Base())+h.modesOffset);
        for (uint32 mi = 0; mi < h.modeCount; ++mi)
        {
            if ((modes[mi].facesOffset & 3) || modes[mi].facesOffset > uint32(asset.TellPut()) ||
                uint64(modes[mi].faceCount)*sizeof(hlight::FaceDisk) > uint32(asset.TellPut())-modes[mi].facesOffset)
                return Fail("previous visibility face directory range");
            hlight::FaceDisk *faces = reinterpret_cast<hlight::FaceDisk *>(static_cast<byte *>(asset.Base())+modes[mi].facesOffset);
            for (uint32 fi = 0; fi < modes[mi].faceCount; ++fi)
            {
                if (faces[fi].flags & ~(previousVersion == 2 ? 15u : 31u)) return Fail("previous visibility face flags");
                if (previousVersion == 2 && faces[fi].styleCount) faces[fi].flags |= hlight::kFaceHasBakedLocalDirect;
            }
        }
        if (h.visibilitySetsOffset > uint32(asset.TellPut()) ||
            uint64(h.visibilitySetCount)*sizeof(hlight::VisibilitySetDisk) > uint32(asset.TellPut())-h.visibilitySetsOffset)
            return Fail("previous visibility set directory range");
        hlight::VisibilitySetDisk *sets = reinterpret_cast<hlight::VisibilitySetDisk *>(static_cast<byte *>(asset.Base())+h.visibilitySetsOffset);
        for (uint32 si = 0; si < h.visibilitySetCount; ++si)
        {
            if (previousVersion == 2)
            {
                if (!hlight::ZeroBytes(reinterpret_cast<byte *>(&sets[si])+104,24))
                    return Fail("previous visibility set reserved tail");
                sets[si].unbakedFacesOffset = sets[si].unbakedLightIndicesOffset = sets[si].payloadOffset;
            }
            if ((sets[si].meshesOffset & 3) || sets[si].meshesOffset > uint32(asset.TellPut()) ||
                uint64(sets[si].meshCount)*sizeof(hlight::PropMeshVisibilityDisk) > uint32(asset.TellPut())-sets[si].meshesOffset)
                return Fail("previous visibility prop mesh range");
            const hlight::PropMeshVisibilityDisk *meshes = reinterpret_cast<const hlight::PropMeshVisibilityDisk *>(static_cast<const byte *>(asset.Base())+sets[si].meshesOffset);
            for (uint32 mesh = 0; mesh < sets[si].meshCount; ++mesh)
                if (meshes[mesh].directPayloadByteOffset || meshes[mesh].directPayloadBytes)
                    return Fail("previous visibility prop mesh reserved fields");
        }
        h.version = hlight::kVersion; h.crc32 = hlight::FileCRC32(asset.Base(),asset.TellPut());
    }
    hlight::FileView file; char error[256];
    if (!hlight::ValidateFile(asset.Base(),asset.TellPut(),file,error,sizeof(error))) return Fail(error);
    if (!hlight::ValidateManifestAsset(file,manifest,error,sizeof(error))) return Fail(error);
    s_Density = file.header->density; V_strncpy(s_AssetPath,manifest.header->assetPath,sizeof(s_AssetPath));
    if (previousVersion)
    {
        // Admission above checked identities/CRC/ranges. A complete rebake must
        // not allocate and copy gigabytes of old RGB/visibility only to discard it.
        for (int mi = 0; mi < 2; ++mi) s_Mode[mi].Clear();
        s_Density = 0;
        Msg("Hlight: checked v%u input; discarding previous RGB for complete v4 prop-direct rebake\n",previousVersion);
        return true;
    }
    // The ambient probe grids ride a separate pak member paired to this asset. A mode without a valid paired grid
    // cannot be carried into a rewritten asset, so it is not retained: the paired rebake replaces it.
    CUtlBuffer probeAsset; hprobe::FileView probes; char probePath[256]; ProbePath(probePath); error[0] = 0;
    const bool paired = ReadFileFromPak(GetPakFile(),probePath,false,probeAsset) &&
        hprobe::ValidateFile(probeAsset.Base(),probeAsset.TellPut(),probes,error,sizeof(error)) &&
        hprobe::ValidatePair(probes,file,error,sizeof(error));
    if (!paired) Msg("Hlight: no valid paired ambient probe asset (%s); retained modes are rebaked\n",error[0] ? error : "missing");
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
        m.identities.RemoveAll();
        const hprobe::ModeView *probeMode = paired ? hprobe::FindMode(probes,r.faceLump,r.lightingLump) : NULL;
        if (!probeMode) continue;
        m.record = r; m.active = true;
        m.identities.AddMultipleToTail(r.identityCount,v.identities);
        m.models.AddMultipleToTail(r.modelCount,v.models); m.faces.AddMultipleToTail(r.faceCount,v.faces);
        m.tiles.AddMultipleToTail(r.tileCount,v.tiles); m.pages.AddMultipleToTail(r.pageCount,v.pages);
        for (uint32 p = 0; p < r.pageCount; ++p)
        {
            m.pages[p].pixelsOffset = m.pixels.Count();
            m.pixels.AddMultipleToTail((int)v.pages[p].pixelsBytes,file.base+v.pages[p].pixelsOffset);
        }
        m.lights.AddMultipleToTail(manifest.mode[mi].lightCount,manifest.mode[mi].lights);
        m.visibility = *v.visibility.record;
        m.visibilityFaces.AddMultipleToTail(m.visibility.faceCount,v.visibility.faces);
        m.visibilityEntries.AddMultipleToTail(m.visibility.entryCount,v.visibility.entries);
        m.visibilityProps.AddMultipleToTail(m.visibility.propCount,v.visibility.props);
        m.unbakedFaces.AddMultipleToTail(m.visibility.unbakedFaceCount,v.visibility.unbakedFaces);
        m.unbakedLightIndices.AddMultipleToTail(m.visibility.unbakedLightIndexCount,v.visibility.unbakedLightIndices);
        m.visibilityMeshes.AddMultipleToTail(m.visibility.meshCount,v.visibility.meshes);
        m.visibilityPayload.AddMultipleToTail(int(m.visibility.payloadBytes),v.visibility.payload);
        m.faceSupport.AddMultipleToTail(int(m.visibility.faceSupportBytes),v.visibility.faceSupport);
        m.visibilityPropsCaptured = true;
        CopyProbeGrid(*probeMode,m.probes); m.probesCaptured = true;
    }
    return true;
}
// Previous enhanced assets are accepted solely as checked paired-rebake inputs.
// No old receiver pixels or visibility survive this import, and runtime validation
// remains strictly v2/v5. The old file CRC and exact native identities are checked.
bool ImportPreviousEnhanced(GameLumpHandle_t handle)
{
    const int manifestBytes = g_GameLumps.GameLumpSize(handle);
    if (manifestBytes < int(sizeof(hlight::ManifestDisk))) return Fail("previous manifest truncated");
    CUtlVector<byte> normalized; normalized.AddMultipleToTail(manifestBytes,static_cast<const byte *>(g_GameLumps.GetGameLump(handle)));
    hlight::ManifestDisk &old = *reinterpret_cast<hlight::ManifestDisk *>(normalized.Base());
    if (old.assetVersion != 1) return Fail("previous manifest asset version mismatch");
    old.assetVersion = hlight::kVersion;
    hlight::ManifestView manifest; char error[256];
    if (!hlight::ValidateManifest(normalized.Base(),normalized.Count(),g_LevelFlags,manifest,error,sizeof(error))) return Fail(error);
    CUtlBuffer asset;
    if (!ReadFileFromPak(GetPakFile(),old.assetPath,false,asset) || asset.TellPut() < 64) return Fail("previous asset missing/truncated");
    const byte *base = static_cast<const byte *>(asset.Base());
    const hlight::FileHeader &header = *reinterpret_cast<const hlight::FileHeader *>(base);
    if (header.magic != hlight::kMagic || header.version != 1 || header.headerBytes != 64 ||
        header.endian != hlight::kEndian || header.fileBytes != uint32(asset.TellPut()) ||
        !header.modeCount || header.modeCount > 4 || !header.density ||
        !hlight::ZeroBytes(base+44,20) || header.crc32 != hlight::FileCRC32(base,asset.TellPut()) ||
        header.modesOffset != 64 || uint64(header.modeCount)*112 > uint32(asset.TellPut())-64)
        return Fail("previous asset header/version/CRC mismatch");
    ModeStorage geometry; geometry.Clear(); GeometryIdentities(geometry);
    uint32 admitted = 0;
    for (int mi = 0; mi < 2; ++mi)
    {
        if (!manifest.mode[mi].runtime) continue;
        const hlight::ManifestModeDisk &mm = *manifest.mode[mi].record;
        if (mm.assetMode >= header.modeCount || (admitted & (1u<<mm.assetMode))) return Fail("previous asset mode duplicate/range");
        hlight::ModeDisk mode; memset(&mode,0,sizeof(mode));
        memcpy(&mode,base+64+mm.assetMode*112,112);
        const CUtlVector<byte> &rgb = mi ? dlightdataHDR : dlightdataLDR;
        const dface_t *faces = mi && numfaces_hdr ? dfaces_hdr : dfaces;
        const int faceCount = mi && numfaces_hdr ? numfaces_hdr : numfaces;
        if (mode.faceLump != mm.faceLump || mode.lightingLump != mm.lightingLump ||
            mode.faceCount != uint32(faceCount) || mode.faceBytes != uint64(faceCount)*sizeof(dface_t) ||
            mode.lightingBytes != uint32(rgb.Count()) || mm.lightingBytes != mode.lightingBytes ||
            mode.facesCRC32 != mm.facesCRC32 || mode.facesCRC32 != ShadowMap_CRC32(faces,faceCount*sizeof(dface_t)) ||
            mode.lightingCRC32 != mm.lightingCRC32 || mode.lightingCRC32 != ShadowMap_CRC32(rgb.Base(),rgb.Count()) ||
            mm.worldlightsCRC32 != ShadowMap_CRC32(mi ? dworldlightsHDR : dworldlightsLDR,
                (mi ? numworldlightsHDR : numworldlightsLDR)*sizeof(dworldlight_t)) ||
            mode.identityCount != 14 || mode.identitiesOffset > uint32(asset.TellPut()) ||
            uint64(mode.identityCount)*sizeof(hlight::LumpIdentity) > uint32(asset.TellPut())-mode.identitiesOffset)
            return Fail("previous asset/native mode identities mismatch");
        const hlight::LumpIdentity *ids = reinterpret_cast<const hlight::LumpIdentity *>(base+mode.identitiesOffset);
        for (int i = 0; i < geometry.identities.Count(); ++i)
        {
            uint32 j = 0; while (j < mode.identityCount && ids[j].lump != geometry.identities[i].lump) ++j;
            if (j == mode.identityCount || memcmp(&ids[j],&geometry.identities[i],sizeof(ids[j])))
                return Fail("previous asset effective geometry mismatch");
        }
        admitted |= 1u<<mm.assetMode;
    }
    if (admitted != (1u<<header.modeCount)-1) return Fail("previous asset unreferenced mode");
    V_strncpy(s_AssetPath,old.assetPath,sizeof(s_AssetPath));
    Msg("Hlight: checked v1/rshd v4 input; discarding old enhanced data for a complete hybrid rebake\n");
    return true;
}
}
bool ReSTIR_BakedLocalDirectFace(const ReSTIRScene &scene, const ReSTIRLightmapResult &result,
    const ReSTIRGpuFace &face, int styles[MAXLIGHTMAPS], int &styleCount, CUtlVector<Vector> &values)
{
    FaceDirectStorage direct;
    if (!BuildFaceDirect(scene,result,face,direct)) return false;
    styleCount = direct.count;
    for (int i = 0; i < direct.count; ++i) styles[i] = direct.styles[i];
    values.Swap(direct.values);
    return true;
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
            const int count = g_GameLumps.GameLumpSize(h);
            if (count < int(sizeof(hlight::ManifestDisk))) return Fail("enhanced manifest truncated");
            const hlight::ManifestDisk *disk = static_cast<const hlight::ManifestDisk *>(g_GameLumps.GetGameLump(h));
            if (disk->assetVersion == 2 || disk->assetVersion == 3)
            {
                if (!options.shadowMaps) return Fail("previous enhanced input requires a complete -restir_shadowmaps rebake");
                CUtlVector<byte> normalized; normalized.AddMultipleToTail(count,reinterpret_cast<const byte *>(disk));
                reinterpret_cast<hlight::ManifestDisk *>(normalized.Base())->assetVersion = hlight::kVersion;
                hlight::ManifestView manifest;
                if (!hlight::ValidateManifest(normalized.Base(),normalized.Count(),g_LevelFlags,manifest,error,sizeof(error)) ||
                    !LoadAsset(manifest,disk->assetVersion)) return false;
            }
            else
            {
                hlight::ManifestView manifest;
                if (!hlight::ValidateManifest(disk,count,g_LevelFlags,manifest,error,sizeof(error))) return Fail(error);
                if (!LoadAsset(manifest)) return false;
            }
        }
        else if (version == 4)
        {
            if (!options.shadowMaps) return Fail("v1 enhanced input requires a complete -restir_shadowmaps rebake");
            if (!ImportPreviousEnhanced(h)) return false;
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
    if (!data || bytes <= 0 || bytes == INT_MAX) return Fail("invalid staged pak member");
    AddBufferToPak(GetPakFile(),name,const_cast<void *>(data),bytes,false,compress ? IZip::eCompressionType_LZMA : IZip::eCompressionType_None);
    CUtlBuffer copied;
    copied.EnsureCapacity(bytes); // CUtlBuffer adds a terminator; bypass geometric growth.
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
    m.visibility.sunLightIndex = sun ? 0 : -1;
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
        if (f.dface != i || f.numStyles < 1 || f.numStyles > RESTIR_MAX_FACE_STYLES || f.styles[0] != 0 ||
            (f.numChannels != 1 && f.numChannels != 4) || out.highWidth != uint32(f.luxelW) || out.highHeight != uint32(f.luxelH))
            return Fail("dense face style/plane/grid mismatch");
        const int64 luxels = (int64)f.luxelW*f.luxelH;
        if (f.firstOutput < 0 || f.firstLuxel < 0 || (int64)f.firstOutput+luxels*f.numStyles*f.numChannels > result.radiance.Count() ||
            (int64)f.firstLuxel+luxels > result.luxelValid.Count() ||
            (int64)f.firstLuxel+luxels > scene.luxels.Count()) return Fail("dense result range mismatch");
        FaceDirectStorage direct;
        if (!BuildFaceDirect(scene,result,f,direct)) return false;
        if (direct.unbaked.Count())
        {
            if (uint64(m.unbakedLightIndices.Count())+direct.unbaked.Count() > hlight::kMaxFileBytes/sizeof(uint32))
                return Fail("unbaked local list exceeds signed file limit");
            hlight::UnbakedFaceDisk fallback = {uint32(i),uint32(m.unbakedLightIndices.Count()),uint32(direct.unbaked.Count())};
            m.unbakedFaces.AddToTail(fallback);
            m.unbakedLightIndices.AddMultipleToTail(direct.unbaked.Count(),direct.unbaked.Base());
        }
        const int receiverCount = direct.count;
        out.flags = hlight::kFaceHasLighting | hlight::kFaceHasBakedLocalDirect | (f.numChannels == 4 ? hlight::kFaceBumped : 0) |
            (f.flags & RESTIR_FACE_DISP ? hlight::kFaceDisplacement : 0) | (sun ? hlight::kFaceHasSun : 0);
        out.styleCount = receiverCount;
        for (int receiverSlot = 0; receiverSlot < receiverCount; ++receiverSlot)
        {
            const int s = direct.sourceSlots[receiverSlot];
            const int style = direct.styles[receiverSlot];
            out.styles[receiverSlot] = style;
            for (int p = 0; p < f.numChannels; ++p)
            {
                const uint32 w = f.luxelW+2, h = f.luxelH+2;
                if (x+w > pageSide) { x = 0; y += rowHeight; rowHeight = 0; }
                if (page < 0 || y+h > pageSide)
                {
                    if (uint64(m.pixels.Count())+pageBytes > hlight::kMaxFileBytes) return Fail("dense page payload exceeds BSP-pak signed limit");
                    if (!ReSTIR_EnsureByteCapacity(m.pixels,uint64(m.pixels.Count())+pageBytes)) return Fail("dense pixel byte capacity exceeds signed limit");
                    hlight::PageDisk record; memset(&record,0,sizeof(record));
                    record.width = record.height = pageSide; record.format = hlight::kRGBA16Float;
                    record.firstTile = m.tiles.Count(); record.pixelsOffset = m.pixels.Count(); record.pixelsBytes = pageBytes;
                    const int old = m.pixels.Count(); m.pixels.SetCount(old+(int)pageBytes); memset(m.pixels.Base()+old,0,(size_t)pageBytes);
                    page = m.pages.AddToTail(record); x = y = rowHeight = 0;
                }
                hlight::TileDisk tile = {uint32(i),uint32(receiverSlot),uint32(p),uint32(page),x+1,y+1,uint32(f.luxelW),uint32(f.luxelH)};
                out.tiles[receiverSlot*4+p] = m.tiles.AddToTail(tile); ++m.pages[page].tileCount;
                uint16 *pixels = reinterpret_cast<uint16 *>(m.pixels.Base()+m.pages[page].pixelsOffset);
                for (uint32 yy = 1; yy < h-1; ++yy)
                for (uint32 xx = 1; xx < w-1; ++xx)
                {
                    const int local = int(yy-1)*f.luxelW + int(xx-1);
                    Vector rgb = vec3_origin;
                    if (s >= 0 && scene.IsReceiverStyle(style))
                    {
                        rgb = result.radiance[f.firstOutput+(s*f.numChannels+p)*(int)luxels+local];
                        if (!rgb.IsValid() || rgb.x < 0 || rgb.y < 0 || rgb.z < 0) return Fail("nonfinite/negative dense transport RGB");
                        rgb = color(context,sf,local,p,rgb);
                    }
                    rgb = (rgb+direct.values[(receiverSlot*f.numChannels+p)*(int)luxels+local])/255.0f;
                    const float alpha = receiverSlot == 0 && p == 0 ? (sun ? result.sunVisibility[f.firstLuxel+local] : 1.0f) : 0.0f;
                    uint16 *dest = pixels+(uint64(y+yy)*pageSide+x+xx)*4;
                    if (alpha < 0 || alpha > 1 || !Half(rgb.x,dest[0]) || !Half(rgb.y,dest[1]) || !Half(rgb.z,dest[2]) || !Half(alpha,dest[3]))
                    {
                        Warning("Hlight: face=%d style=%d plane=%d dense=(%d,%d) RGBA=(%g,%g,%g,%g) is not representable\n",
                            i,style,p,local%f.luxelW,local/f.luxelW,rgb.x,rgb.y,rgb.z,alpha);
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
    if (!CaptureWorldVisibility(m,scene,result)) return false;
    Msg("Hlight: baked local direct fallback faces=%d faceLights=%d (%s)\n",
        m.unbakedFaces.Count(),m.unbakedLightIndices.Count(),g_bHDR ? "HDR" : "LDR");
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

bool ReSTIR_CaptureAmbientProbes(const ReSTIROptions &options, const ReSTIRScene &scene, CReSTIRVulkanDevice &device)
{
    if (!options.shadowMaps) return true;
    if (!s_Prepared) return Fail("probe capture without prepared storage");
    ModeStorage &m = s_Mode[SelectedMode()];
    if (!ReSTIR_BuildAmbientProbeGrid(options,scene,device,m.probes)) return false;
    m.probesCaptured = true; return true;
}
bool ReSTIR_ReusePairedAmbientProbes(const ReSTIROptions &options)
{
    if (!options.shadowMaps) return true;
    if (!s_Prepared || !s_Mode[0].probesCaptured) return Fail("paired probe reuse lacks the captured LDR grid");
    s_Mode[1].probes = s_Mode[0].probes; s_Mode[1].probesCaptured = true; return true;
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
        if (s_AssetPath[0]) RemoveAssets();
    }
    return true;
}
bool ReSTIR_MarkPairedVisibilityReuse()
{
    if (!s_Mode[0].active || !s_Mode[1].active) return Fail("paired visibility reuse lacks a captured mode");
    s_Mode[0].visibilityShared = s_Mode[1].visibilityShared = true;
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
        if (s_AssetPath[0]) RemoveAssets();
        s_Finalized = true; return true;
    }
    hlight::FileHeader header; memset(&header,0,sizeof(header));
    header.magic = hlight::kMagic; header.version = hlight::kVersion; header.headerBytes = sizeof(header);
    header.endian = hlight::kEndian; header.density = s_Density;
    int modeOrder[2]; for (int mi = 0; mi < 2; ++mi) if (mask & (1u<<mi)) { FinalIdentities(s_Mode[mi],mi); modeOrder[header.modeCount++] = mi; }
    if (header.modeCount == 2 && (s_Mode[modeOrder[0]].record.faceLump > s_Mode[modeOrder[1]].record.faceLump ||
        (s_Mode[modeOrder[0]].record.faceLump == s_Mode[modeOrder[1]].record.faceLump &&
         s_Mode[modeOrder[0]].record.lightingLump > s_Mode[modeOrder[1]].record.lightingLump))) V_swap(modeOrder[0],modeOrder[1]);
    // Reserve the complete canonical file once. Apart from eliminating copies,
    // exact EnsureCapacity bypasses CUtlMemory's signed 1-GiB doubling overflow.
    uint64 fileBytes = sizeof(header);
    if (!ReSTIR_AddByteSectionSize(fileBytes,uint64(header.modeCount)*sizeof(hlight::ModeDisk)))
        return Fail("asset exceeds signed BSP-pak buffer limit");
    for (uint32 i = 0; i < header.modeCount; ++i)
    {
        ModeStorage &m = s_Mode[modeOrder[i]];
        if (!CapturePropVisibility(m,modeOrder[i])) return false;
        if (!ReSTIR_AddByteSectionSize(fileBytes,uint64(m.identities.Count())*sizeof(hlight::LumpIdentity)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.models.Count())*sizeof(hlight::ModelDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.faces.Count())*sizeof(hlight::FaceDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.tiles.Count())*sizeof(hlight::TileDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.pages.Count())*sizeof(hlight::PageDisk)))
            return Fail("asset exceeds signed BSP-pak buffer limit");
        for (int p = 0; p < m.pages.Count(); ++p)
            if (!ReSTIR_AddByteSectionSize(fileBytes,m.pages[p].pixelsBytes))
                return Fail("asset exceeds signed BSP-pak buffer limit");
    }
    uint32 plannedSets = 1;
    if (header.modeCount == 2 && !s_Mode[modeOrder[1]].visibilityShared) ++plannedSets;
    if (!ReSTIR_AddByteSectionSize(fileBytes,uint64(plannedSets)*sizeof(hlight::VisibilitySetDisk)))
        return Fail("asset exceeds signed BSP-pak buffer limit");
    for (uint32 i = 0; i < plannedSets; ++i)
    {
        const ModeStorage &m = s_Mode[modeOrder[i]];
        if (!ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityFaces.Count())*sizeof(hlight::FaceVisibilityDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityEntries.Count())*sizeof(hlight::VisibilityEntryDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityProps.Count())*sizeof(hlight::PropVisibilityDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityMeshes.Count())*sizeof(hlight::PropMeshVisibilityDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,m.faceSupport.Count()) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.unbakedFaces.Count())*sizeof(hlight::UnbakedFaceDisk)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,uint64(m.unbakedLightIndices.Count())*sizeof(uint32)) ||
            !ReSTIR_AddByteSectionSize(fileBytes,m.visibilityPayload.Count()))
            return Fail("asset exceeds signed BSP-pak buffer limit");
    }
    CUtlVector<byte> file; file.EnsureCapacity(int(fileBytes));
    file.SetCount(sizeof(header)); memset(file.Base(),0,file.Count());
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
    hlight::VisibilitySetDisk visibilityRecords[2]; memset(visibilityRecords,0,sizeof(visibilityRecords));
    int visibilityModes[2];
    for (uint32 i = 0; i < header.modeCount; ++i)
    {
        ModeStorage &m = s_Mode[modeOrder[i]];
        m.visibility.entryCount = m.visibilityEntries.Count();
        m.visibility.payloadBytes = m.visibilityPayload.Count();
        m.visibility.payloadCRC32 = ShadowMap_CRC32(m.visibilityPayload.Base(),m.visibilityPayload.Count());
        m.visibility.faceSupportBytes = m.faceSupport.Count();
        m.visibility.unbakedFaceCount = m.unbakedFaces.Count();
        m.visibility.unbakedLightIndexCount = m.unbakedLightIndices.Count();
        bool shared = false;
        if (i && m.visibilityShared)
        {
            const ModeStorage &previous = s_Mode[visibilityModes[0]];
            shared = m.visibility.selectedLightsCRC32 == previous.visibility.selectedLightsCRC32 &&
                m.visibility.faceCount == previous.visibility.faceCount &&
                m.visibilityEntries.Count() == previous.visibilityEntries.Count() &&
                m.visibilityPayload.Count() == previous.visibilityPayload.Count() &&
                m.visibilityProps.Count() == previous.visibilityProps.Count() &&
                m.visibilityMeshes.Count() == previous.visibilityMeshes.Count() &&
                m.faceSupport.Count() == previous.faceSupport.Count() &&
                m.unbakedFaces.Count() == previous.unbakedFaces.Count() &&
                m.unbakedLightIndices.Count() == previous.unbakedLightIndices.Count() &&
                !memcmp(m.visibilityFaces.Base(),previous.visibilityFaces.Base(),m.visibilityFaces.Count()*sizeof(hlight::FaceVisibilityDisk)) &&
                !memcmp(m.visibilityEntries.Base(),previous.visibilityEntries.Base(),m.visibilityEntries.Count()*sizeof(hlight::VisibilityEntryDisk)) &&
                !memcmp(m.visibilityProps.Base(),previous.visibilityProps.Base(),m.visibilityProps.Count()*sizeof(hlight::PropVisibilityDisk)) &&
                !memcmp(m.visibilityMeshes.Base(),previous.visibilityMeshes.Base(),m.visibilityMeshes.Count()*sizeof(hlight::PropMeshVisibilityDisk)) &&
                !memcmp(m.faceSupport.Base(),previous.faceSupport.Base(),m.faceSupport.Count()) &&
                !memcmp(m.unbakedFaces.Base(),previous.unbakedFaces.Base(),m.unbakedFaces.Count()*sizeof(hlight::UnbakedFaceDisk)) &&
                !memcmp(m.unbakedLightIndices.Base(),previous.unbakedLightIndices.Base(),m.unbakedLightIndices.Count()*sizeof(uint32)) &&
                !memcmp(m.visibilityPayload.Base(),previous.visibilityPayload.Base(),m.visibilityPayload.Count());
            if (!shared) return Fail("paired equality visibility reuse disagrees with serialized inputs");
        }
        if (shared) records[i].visibilitySetIndex = 0;
        else
        {
            records[i].visibilitySetIndex = header.visibilitySetCount;
            visibilityModes[header.visibilitySetCount++] = modeOrder[i];
        }
    }
    if (!Append(file,visibilityRecords,uint64(header.visibilitySetCount)*sizeof(hlight::VisibilitySetDisk),header.visibilitySetsOffset)) return false;
    for (uint32 si = 0; si < header.visibilitySetCount; ++si)
    {
        ModeStorage &m = s_Mode[visibilityModes[si]]; hlight::VisibilitySetDisk &v = visibilityRecords[si]; v = m.visibility;
        if (!Append(file,m.visibilityFaces.Base(),uint64(m.visibilityFaces.Count())*sizeof(hlight::FaceVisibilityDisk),v.facesOffset) ||
            !Append(file,m.visibilityEntries.Base(),uint64(m.visibilityEntries.Count())*sizeof(hlight::VisibilityEntryDisk),v.entriesOffset) ||
            !Append(file,m.visibilityProps.Base(),uint64(m.visibilityProps.Count())*sizeof(hlight::PropVisibilityDisk),v.propsOffset) ||
            !Append(file,m.visibilityMeshes.Base(),uint64(m.visibilityMeshes.Count())*sizeof(hlight::PropMeshVisibilityDisk),v.meshesOffset) ||
            !Append(file,m.faceSupport.Base(),m.faceSupport.Count(),v.faceSupportOffset) ||
            !Append(file,m.unbakedFaces.Base(),uint64(m.unbakedFaces.Count())*sizeof(hlight::UnbakedFaceDisk),v.unbakedFacesOffset) ||
            !Append(file,m.unbakedLightIndices.Base(),uint64(m.unbakedLightIndices.Count())*sizeof(uint32),v.unbakedLightIndicesOffset) ||
            !Append(file,m.visibilityPayload.Base(),m.visibilityPayload.Count(),v.payloadOffset)) return false;
    }
    memcpy(file.Base()+header.visibilitySetsOffset,visibilityRecords,header.visibilitySetCount*sizeof(hlight::VisibilitySetDisk));
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
    CUtlVector<byte> probeFile; char probePath[256]; ProbePath(probePath);
    if (!BuildProbeAsset(verified,modeOrder,probeFile)) return false;
    if (!ReSTIR_StageReceiverPakFile(s_AssetPath,file.Base(),file.Count(),true) ||
        !ReSTIR_StageReceiverPakFile(probePath,probeFile.Base(),probeFile.Count(),true)) return false;
    DeleteManifest(); GameLumpHandle_t h = g_GameLumps.CreateGameLump(GAMELUMP_RESTIR_SHADOWMAPS,bytes.Count(),0,hlight::kManifestVersion);
    if (h == g_GameLumps.InvalidGameLump() || !g_GameLumps.GetGameLump(h)) return Fail("cannot allocate native selected-light manifest");
    memcpy(g_GameLumps.GetGameLump(h),bytes.Base(),bytes.Count()); s_Finalized = true;
    Msg("Hlight: checked pak asset %s (%d bytes), probes %s (%d bytes), rshd v5 (%d bytes), explicit mode mask=%u\n",s_AssetPath,file.Count(),probePath,probeFile.Count(),bytes.Count(),mask);
    return true;
}
