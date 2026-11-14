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
#include "bsp21_format.h"
#include "tier1/utlbuffer.h"
#include "tier1/lzmaDecoder.h"
#include "zip_utils.h"
#include "gamebspfile.h"
#include <windows.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

extern int numworldlightsHDR;
extern dworldlight_t dworldlightsHDR[MAX_MAP_WORLDLIGHTS];

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
    bool visibilityPropsCaptured;
    ReSTIRAmbientProbeGrid probes; bool probesCaptured;
    void Clear(bool preserveProbes = false)
    {
        active = false;
        memset(&record,0,sizeof(record)); identities.Purge(); models.Purge(); faces.Purge();
        tiles.Purge(); pages.Purge(); pixels.Purge(); lights.Purge();
        memset(&visibility,0,sizeof(visibility)); visibility.sunLightIndex = -1;
        visibilityFaces.Purge(); visibilityEntries.Purge(); visibilityProps.Purge(); visibilityMeshes.Purge();
        unbakedFaces.Purge(); unbakedLightIndices.Purge();
        visibilityPayload.Purge(); faceSupport.Purge(); visibilityPropsCaptured = false;
        if (!preserveProbes) { probes.Purge(); probesCaptured = false; }
    }
};
ModeStorage s_Storage; // the single HDR mode of the .light
uint32 s_Density = 0;
char s_AssetPath[256];
bool s_Prepared = false;
// The map's own HDR worldlights as loaded, before the ambient stage edits their flags: the selected lights are matched
// against these records, and the manifest binds to the CRC of the lump bytes exactly as the engine loads them.
CUtlVector<dworldlight_t> s_InputWorldlights;
uint32 s_InputWorldlightsCRC32 = 0;
bool Fail(const char *why)
{
    Warning("Hlight: %s\n",why); return false;
}
// maps/<name>.hprobe: the ambient probe grids paired to the .hlight asset. s_AssetPath is a manifest-validated or
// generated maps/<name>.hlight, so the suffix swap cannot fail.
void ProbePath(char (&out)[256])
{
    hprobe::ProbeAssetPath(s_AssetPath,out,sizeof(out));
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
void FinalIdentities(ModeStorage &m)
{
    m.identities.RemoveAll(); GeometryIdentities(m);
    m.record.faceLump = LUMP_FACES_HDR; m.record.lightingLump = LUMP_LIGHTING_HDR;
    m.record.faceVersion = LUMP_FACES_VERSION; m.record.lightingVersion = LUMP_LIGHTING_VERSION;
    m.record.faceBytes = numfaces_hdr*sizeof(dface_t); m.record.lightingBytes = dlightdataHDR.Count();
    m.record.facesCRC32 = ShadowMap_CRC32(dfaces_hdr,(uint32)m.record.faceBytes);
    m.record.lightingCRC32 = ShadowMap_CRC32(dlightdataHDR.Base(),dlightdataHDR.Count());
    Identity(m,m.record.faceLump,dfaces_hdr,(uint32)m.record.faceBytes,m.record.faceVersion);
    Identity(m,m.record.lightingLump,dlightdataHDR.Base(),dlightdataHDR.Count(),m.record.lightingVersion);
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
// The grid section is written straight from the vectors: one aligned section, no staging copy of the grid.
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
    memcpy(at,g.validity.Base(),g.validity.Count()); at += g.validity.Count();
    memcpy(at,g.freeDistance.Base(),g.freeDistance.Count()*sizeof(uint32));
    offset = start; return true;
}
// The .hprobe pak member: the grid of the single mode, bound to that exact .hlight by its CRC and the mode's identity.
bool BuildProbeAsset(const hlight::FileView &lightmaps, CUtlVector<byte> &file)
{
    const ModeStorage &m = s_Storage;
    if (!m.probesCaptured) return Fail("ambient probe grid was not captured");
    uint64 fileBytes = sizeof(hprobe::FileHeader);
    if (!ReSTIR_AddByteSectionSize(fileBytes,sizeof(hprobe::ModeDisk)) ||
        !ReSTIR_AddByteSectionSize(fileBytes,hprobe::GridBytes(m.probes.grid)))
        return Fail("probe asset exceeds the signed pak member limit");
    hprobe::FileHeader header; memset(&header,0,sizeof(header));
    header.magic = hprobe::kMagic; header.version = hprobe::kVersion; header.headerBytes = sizeof(header);
    header.endian = hprobe::kEndian; header.hlightCRC32 = lightmaps.header->crc32; header.modeCount = 1;
    file.EnsureCapacity(int(fileBytes));
    file.SetCount(sizeof(header)); memset(file.Base(),0,file.Count());
    hprobe::ModeDisk record; memset(&record,0,sizeof(record));
    if (!Append(file,&record,sizeof(record),header.modesOffset)) return false;
    const hlight::ModeDisk &identity = *lightmaps.mode[0].record;
    record.faceLump = identity.faceLump; record.lightingLump = identity.lightingLump;
    record.facesCRC32 = identity.facesCRC32; record.lightingCRC32 = identity.lightingCRC32;
    record.lightingBytes = identity.lightingBytes; record.brickCount = m.probes.grid.brickCount;
    if (!AppendProbeGrid(file,m.probes,record.gridOffset)) return false;
    memcpy(file.Base()+header.modesOffset,&record,sizeof(record));
    header.fileBytes = file.Count(); memcpy(file.Base(),&header,sizeof(header));
    reinterpret_cast<hprobe::FileHeader *>(file.Base())->crc32 = hprobe::FileCRC32(file.Base(),file.Count());
    hprobe::FileView probes; char error[256];
    if (!hprobe::ValidateFile(file.Base(),file.Count(),probes,error,sizeof(error)) ||
        !hprobe::ValidatePair(probes,lightmaps,error,sizeof(error))) return Fail(error);
    return true;
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
            Warning("Hlight: receiver style overflow face=%d styles=%d,%d,%d,%d + %d\n",
                face.dface,out.styles[0],out.styles[1],out.styles[2],out.styles[3],face.styles[s]);
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
bool CapturePropVisibility(ModeStorage &m)
{
    if (m.visibilityPropsCaptured) return true;
    const ReSTIRPropVisibilityData *data = ReSTIR_GetStaticPropVisibility();
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
// One lump of a v19-v21 BSP file exactly as the engine's lump loader hands it to its consumers: the stored bytes,
// LZMA-decoded when the lump is compressed. Version-21 worldlights stay 100-byte records here.
bool ReadEngineLump(const char *path, int lump, CUtlVector<byte> &out)
{
    out.RemoveAll();
    FILE *file = fopen(path,"rb");
    if (!file) return Fail("cannot reopen the input BSP");
    Bsp21Header header; bool read = fread(&header,sizeof(header),1,file) == 1;
    uint32 offset = 0, stored = 0, length = 0;
    if (read && header.ident == BSP21_IDENT && header.version == BSP21_VERSION)
    {
        offset = header.lumps[lump].offset; stored = header.lumps[lump].length; length = header.lumps[lump].uncompressedLength;
    }
    else if (read && header.ident == BSP21_IDENT && header.version >= 19 && header.version <= 20)
    {
        const lump_t &l = reinterpret_cast<const dheader_t &>(header).lumps[lump];
        offset = uint32(l.fileofs); stored = uint32(l.filelen); length = uint32(l.uncompressedSize);
    }
    else read = false;
    CUtlVector<byte> bytes; bytes.SetCount(int(MIN(stored,uint32(INT_MAX))));
    read = read && stored <= uint32(INT_MAX) && length <= uint32(INT_MAX) && !fseek(file,long(offset),SEEK_SET) &&
        (!stored || fread(bytes.Base(),stored,1,file) == 1);
    fclose(file);
    if (!read) return Fail("unreadable input BSP lump");
    if (!length) { out.Swap(bytes); return true; }
    if (stored < sizeof(lzma_header_t) || !CLZMA::IsCompressed(bytes.Base()) || CLZMA::GetActualSize(bytes.Base()) != length)
        return Fail("malformed compressed input BSP lump");
    out.SetCount(int(length));
    if (CLZMA::Uncompress(bytes.Base(),out.Base()) != length) return Fail("input BSP lump LZMA decode failed");
    return true;
}
// Record indices of the map's own HDR worldlights that carry a selected light. The selected records were built from
// the same entities, so the complete worldlight identity is available; a selected light without a counterpart removes nothing.
void MatchRemovedWorldlights(const CUtlVector<ShadowMapLightDisk> &lights, CUtlVector<uint32> &removed)
{
    removed.RemoveAll();
    CUtlVector<bool> used; used.SetCount(s_InputWorldlights.Count());
    for (int i = 0; i < used.Count(); ++i) used[i] = false;
    for (int li = 0; li < lights.Count(); ++li)
    {
        const dworldlight_t &want = lights[li].light;
        int match = -1;
        for (int i = 0; i < s_InputWorldlights.Count() && match < 0; ++i)
        {
            const dworldlight_t &have = s_InputWorldlights[i];
            if (used[i] || have.type != want.type || have.style != want.style) continue;
            const float intensityScale = MAX(1.0f, want.intensity.LengthSqr());
            if (have.origin.DistToSqr(want.origin) <= 0.25f &&
                have.normal.DistToSqr(want.normal) <= 1e-6f &&
                have.intensity.DistToSqr(want.intensity) <= 1e-8f * intensityScale)
                match = i;
        }
        if (match < 0)
        {
            Warning("Hlight: selected light %d (type %d, style %d) has no counterpart in the BSP's worldlights\n",li,want.type,want.style);
            continue;
        }
        used[match] = true;
    }
    for (int i = 0; i < used.Count(); ++i) if (used[i]) removed.AddToTail(uint32(i));
    Msg("Hlight: %d of %d selected lights matched to BSP worldlights (dropped from the engine's lump at load)\n",removed.Count(),lights.Count());
}
// <map>.light: a BSP v20 container holding only LUMP_MAP_FLAGS, the rshd game lump and the pak of the two assets.
// Written to a temporary sibling and renamed over the previous file, so a failed bake leaves the old one in place.
bool WriteLightFile(const char *path, const CUtlVector<byte> &manifest, const CUtlVector<byte> &asset,
    const char *probePath, const CUtlVector<byte> &probes)
{
    IZip *zip = IZip::CreateZip(NULL,true);
    zip->AddBufferToZip(s_AssetPath,const_cast<byte *>(asset.Base()),asset.Count(),false,IZip::eCompressionType_LZMA);
    zip->AddBufferToZip(probePath,const_cast<byte *>(probes.Base()),probes.Count(),false,IZip::eCompressionType_LZMA);
    CUtlBuffer pak; zip->SaveToBuffer(pak);
    IZip::ReleaseZip(zip);
    dheader_t header; memset(&header,0,sizeof(header));
    header.ident = IDBSPHEADER; header.version = 20; header.mapRevision = g_MapRevision;
    const uint32 flags = LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR;
    const int gameLumpBytes = int(sizeof(int)+sizeof(dgamelump_t));
    header.lumps[LUMP_MAP_FLAGS].fileofs = sizeof(header); header.lumps[LUMP_MAP_FLAGS].filelen = sizeof(flags);
    header.lumps[LUMP_GAME_LUMP].fileofs = header.lumps[LUMP_MAP_FLAGS].fileofs+sizeof(flags);
    header.lumps[LUMP_GAME_LUMP].filelen = gameLumpBytes+manifest.Count();
    const int64 pakOffset = (int64(header.lumps[LUMP_GAME_LUMP].fileofs)+header.lumps[LUMP_GAME_LUMP].filelen+3)&~int64(3);
    if (pakOffset+pak.TellPut() > INT_MAX) return Fail(".light exceeds the 2 GiB BSP limit");
    header.lumps[LUMP_PAKFILE].fileofs = int(pakOffset); header.lumps[LUMP_PAKFILE].filelen = pak.TellPut();
    dgamelump_t entry; memset(&entry,0,sizeof(entry));
    entry.id = GAMELUMP_RESTIR_SHADOWMAPS; entry.version = (unsigned short)hlight::kManifestVersion;
    entry.fileofs = header.lumps[LUMP_GAME_LUMP].fileofs+gameLumpBytes; entry.filelen = manifest.Count();
    char temporary[MAX_PATH*2]; V_snprintf(temporary,sizeof(temporary),"%s.restir.tmp",path);
    FILE *file = fopen(temporary,"wb");
    if (!file) return Fail("cannot create the temporary .light file");
    const int count = 1; const uint32 zero = 0;
    const int padding = int(pakOffset-(header.lumps[LUMP_GAME_LUMP].fileofs+header.lumps[LUMP_GAME_LUMP].filelen));
    bool written = fwrite(&header,sizeof(header),1,file) == 1 && fwrite(&flags,sizeof(flags),1,file) == 1 &&
        fwrite(&count,sizeof(count),1,file) == 1 && fwrite(&entry,sizeof(entry),1,file) == 1 &&
        fwrite(manifest.Base(),manifest.Count(),1,file) == 1 && (!padding || fwrite(&zero,padding,1,file) == 1) &&
        fwrite(pak.Base(),pak.TellPut(),1,file) == 1;
    written = !fclose(file) && written;
    if (!written || !MoveFileExA(temporary,path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileA(temporary);
        return Fail("cannot write the .light file");
    }
    Msg("Hlight: wrote %s (%s %d bytes, %s %d bytes, rshd v%u %d bytes, file %lld bytes)\n",path,s_AssetPath,asset.Count(),
        probePath,probes.Count(),hlight::kManifestVersion,manifest.Count(),(long long)(pakOffset+pak.TellPut()));
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
    s_Prepared = false; s_Density = 0; memset(s_AssetPath,0,sizeof(s_AssetPath));
    s_Storage.Clear(); s_InputWorldlights.Purge(); s_InputWorldlightsCRC32 = 0;
    // Enhanced lighting lives only in <map>.light; a BSP rewritten by an older vrad_restir is not a valid input.
    if (g_GameLumps.GetGameLumpHandle(GAMELUMP_RESTIR_SHADOWMAPS) != g_GameLumps.InvalidGameLump() ||
        (g_LevelFlags & (LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR|LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR)))
        return Fail("the input BSP carries an in-BSP enhanced bake of an older vrad_restir; bake from the original BSP");
    if (options.shadowMaps)
    {
        CUtlVector<byte> raw;
        if (!ReadEngineLump(options.mapPath.String(),LUMP_WORLDLIGHTS_HDR,raw)) return false;
        const int recordBytes = raw.Count() == numworldlightsHDR*int(BSP21_WORLDLIGHT_BYTES) ? int(BSP21_WORLDLIGHT_BYTES) : int(sizeof(dworldlight_t));
        if (raw.Count() != numworldlightsHDR*recordBytes) return Fail("loaded HDR worldlights do not match the BSP lump");
        s_InputWorldlights.CopyArray(dworldlightsHDR,numworldlightsHDR);
        s_InputWorldlightsCRC32 = ShadowMap_CRC32(raw.Base(),raw.Count());
        s_Density = options.highresDensity;
    }
    s_Prepared = true; return true;
}

bool ReSTIR_InvalidateShadowMapTopology(const ReSTIROptions &options)
{
    (void)options;
    if (!s_Prepared) return Fail("storage not prepared before topology change");
    s_Storage.Clear(); return true;
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
    if (!options.shadowMaps)
    {
        s_Storage.Clear();
        return true;
    }
    // The gather stage has already captured the probes. Receiver capture
    // replaces only RGB/visibility storage, not the ambient grid.
    ModeStorage &m = s_Storage; m.Clear(true); m.active = true; m.lights.AddVectorToTail(scene.shadowLights);
    const int faceCount = numfaces_hdr;
    if (nummodels <= 0 || nummodels > MAX_MAP_MODELS || faceCount <= 0 || faceCount > MAX_MAP_FACES ||
        g_pFaces != dfaces_hdr || scene.dfaceToFace.Count() != faceCount || !color) return Fail("invalid model/face capture domain");
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
    Msg("Hlight: baked local direct fallback faces=%d faceLights=%d\n",m.unbakedFaces.Count(),m.unbakedLightIndices.Count());
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
    if (!ReSTIR_BuildAmbientProbeGrid(options,scene,device,s_Storage.probes)) return false;
    s_Storage.probesCaptured = true; return true;
}

bool ReSTIR_WriteLightSidecar(const ReSTIROptions &options)
{
    ModeStorage &m = s_Storage;
    if (!s_Prepared || !m.active) return Fail("sidecar write without captured lighting");
    FinalIdentities(m);
    if (!CapturePropVisibility(m)) return false;
    hlight::FileHeader header; memset(&header,0,sizeof(header));
    header.magic = hlight::kMagic; header.version = hlight::kVersion; header.headerBytes = sizeof(header);
    header.endian = hlight::kEndian; header.density = s_Density; header.modeCount = 1; header.visibilitySetCount = 1;
    m.visibility.entryCount = m.visibilityEntries.Count();
    m.visibility.payloadBytes = m.visibilityPayload.Count();
    m.visibility.payloadCRC32 = ShadowMap_CRC32(m.visibilityPayload.Base(),m.visibilityPayload.Count());
    m.visibility.faceSupportBytes = m.faceSupport.Count();
    m.visibility.unbakedFaceCount = m.unbakedFaces.Count();
    m.visibility.unbakedLightIndexCount = m.unbakedLightIndices.Count();
    // Reserve the complete canonical file once. Apart from eliminating copies,
    // exact EnsureCapacity bypasses CUtlMemory's signed 1-GiB doubling overflow.
    uint64 fileBytes = sizeof(header);
    bool fits = ReSTIR_AddByteSectionSize(fileBytes,sizeof(hlight::ModeDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.identities.Count())*sizeof(hlight::LumpIdentity)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.models.Count())*sizeof(hlight::ModelDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.faces.Count())*sizeof(hlight::FaceDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.tiles.Count())*sizeof(hlight::TileDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.pages.Count())*sizeof(hlight::PageDisk));
    for (int p = 0; fits && p < m.pages.Count(); ++p) fits = ReSTIR_AddByteSectionSize(fileBytes,m.pages[p].pixelsBytes);
    fits = fits && ReSTIR_AddByteSectionSize(fileBytes,sizeof(hlight::VisibilitySetDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityFaces.Count())*sizeof(hlight::FaceVisibilityDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityEntries.Count())*sizeof(hlight::VisibilityEntryDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityProps.Count())*sizeof(hlight::PropVisibilityDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.visibilityMeshes.Count())*sizeof(hlight::PropMeshVisibilityDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,m.faceSupport.Count()) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.unbakedFaces.Count())*sizeof(hlight::UnbakedFaceDisk)) &&
        ReSTIR_AddByteSectionSize(fileBytes,uint64(m.unbakedLightIndices.Count())*sizeof(uint32)) &&
        ReSTIR_AddByteSectionSize(fileBytes,m.visibilityPayload.Count());
    if (!fits) return Fail("asset exceeds the signed pak member limit");
    CUtlVector<byte> file; file.EnsureCapacity(int(fileBytes));
    file.SetCount(sizeof(header)); memset(file.Base(),0,file.Count());
    hlight::ModeDisk record = m.record;
    if (!Append(file,&record,sizeof(record),header.modesOffset) ||
        !Append(file,m.identities.Base(),uint64(m.identities.Count())*sizeof(hlight::LumpIdentity),record.identitiesOffset) ||
        !Append(file,m.models.Base(),uint64(m.models.Count())*sizeof(hlight::ModelDisk),record.modelsOffset) ||
        !Append(file,m.faces.Base(),uint64(m.faces.Count())*sizeof(hlight::FaceDisk),record.facesOffset) ||
        !Append(file,m.tiles.Base(),uint64(m.tiles.Count())*sizeof(hlight::TileDisk),record.tilesOffset) ||
        !Append(file,m.pages.Base(),uint64(m.pages.Count())*sizeof(hlight::PageDisk),record.pagesOffset)) return false;
    for (int p = 0; p < m.pages.Count(); ++p)
    {
        hlight::PageDisk page = m.pages[p];
        if (!Append(file,m.pixels.Base()+page.pixelsOffset,page.pixelsBytes,page.pixelsOffset)) return false;
        memcpy(file.Base()+record.pagesOffset+p*sizeof(page),&page,sizeof(page));
    }
    hlight::VisibilitySetDisk visibility = m.visibility;
    if (!Append(file,&visibility,sizeof(visibility),header.visibilitySetsOffset) ||
        !Append(file,m.visibilityFaces.Base(),uint64(m.visibilityFaces.Count())*sizeof(hlight::FaceVisibilityDisk),visibility.facesOffset) ||
        !Append(file,m.visibilityEntries.Base(),uint64(m.visibilityEntries.Count())*sizeof(hlight::VisibilityEntryDisk),visibility.entriesOffset) ||
        !Append(file,m.visibilityProps.Base(),uint64(m.visibilityProps.Count())*sizeof(hlight::PropVisibilityDisk),visibility.propsOffset) ||
        !Append(file,m.visibilityMeshes.Base(),uint64(m.visibilityMeshes.Count())*sizeof(hlight::PropMeshVisibilityDisk),visibility.meshesOffset) ||
        !Append(file,m.faceSupport.Base(),m.faceSupport.Count(),visibility.faceSupportOffset) ||
        !Append(file,m.unbakedFaces.Base(),uint64(m.unbakedFaces.Count())*sizeof(hlight::UnbakedFaceDisk),visibility.unbakedFacesOffset) ||
        !Append(file,m.unbakedLightIndices.Base(),uint64(m.unbakedLightIndices.Count())*sizeof(uint32),visibility.unbakedLightIndicesOffset) ||
        !Append(file,m.visibilityPayload.Base(),m.visibilityPayload.Count(),visibility.payloadOffset)) return false;
    memcpy(file.Base()+header.visibilitySetsOffset,&visibility,sizeof(visibility));
    memcpy(file.Base()+header.modesOffset,&record,sizeof(record));
    header.fileBytes = file.Count(); memcpy(file.Base(),&header,sizeof(header));
    reinterpret_cast<hlight::FileHeader *>(file.Base())->crc32 = hlight::FileCRC32(file.Base(),file.Count());
    hlight::FileView verified; char error[256];
    if (!hlight::ValidateFile(file.Base(),file.Count(),verified,error,sizeof(error))) return Fail(error);

    CUtlVector<uint32> removed; MatchRemovedWorldlights(m.lights,removed);
    hlight::ManifestDisk manifest; memset(&manifest,0,sizeof(manifest));
    manifest.runtimeModeMask = 1u << SHADOWMAP_MODE_HDR; manifest.assetVersion = hlight::kVersion;
    V_strncpy(manifest.assetPath,s_AssetPath,sizeof(manifest.assetPath));
    manifest.mode[SHADOWMAP_MODE_LDR].sunLightIndex = -1;
    hlight::ManifestModeDisk &mm = manifest.mode[SHADOWMAP_MODE_HDR];
    const uint64 lightBytes = uint64(m.lights.Count())*sizeof(ShadowMapLightDisk), removedBytes = uint64(removed.Count())*sizeof(uint32);
    CUtlVector<byte> bytes; bytes.SetCount(int(sizeof(manifest)+lightBytes+removedBytes)); memset(bytes.Base(),0,bytes.Count());
    mm.faceLump = record.faceLump; mm.lightingLump = record.lightingLump; mm.worldlightsLump = LUMP_WORLDLIGHTS_HDR;
    mm.lightingBytes = uint32(record.lightingBytes); mm.lightingCRC32 = record.lightingCRC32; mm.facesCRC32 = record.facesCRC32;
    mm.worldlightsCRC32 = s_InputWorldlightsCRC32;
    mm.lightsOffset = sizeof(manifest); mm.lightCount = m.lights.Count();
    mm.sunLightIndex = mm.lightCount && m.lights[0].light.type == emit_skylight ? 0 : -1;
    mm.removedWorldlightsOffset = uint32(sizeof(manifest)+lightBytes); mm.removedWorldlightCount = removed.Count();
    if (lightBytes) memcpy(bytes.Base()+mm.lightsOffset,m.lights.Base(),size_t(lightBytes));
    if (removedBytes) memcpy(bytes.Base()+mm.removedWorldlightsOffset,removed.Base(),size_t(removedBytes));
    manifest.byteSize = bytes.Count(); memcpy(bytes.Base(),&manifest,sizeof(manifest));
    hlight::ManifestView mv;
    if (!hlight::ValidateManifest(bytes.Base(),bytes.Count(),LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR,mv,error,sizeof(error)) ||
        !hlight::ValidateManifestAsset(verified,mv,error,sizeof(error))) return Fail(error);
    CUtlVector<byte> probeFile; char probePath[256]; ProbePath(probePath);
    if (!BuildProbeAsset(verified,probeFile)) return false;
    char lightPath[MAX_PATH*2], basename[MAX_PATH]; V_FileBase(options.mapPath.String(),basename,sizeof(basename));
    if (options.lightDir.Length()) V_ComposeFileName(options.lightDir.String(),basename,lightPath,sizeof(lightPath));
    else V_strncpy(lightPath,options.mapPath.String(),sizeof(lightPath));
    V_SetExtension(lightPath,".light",sizeof(lightPath));
    return WriteLightFile(lightPath,bytes,file,probePath,probeFile);
}
