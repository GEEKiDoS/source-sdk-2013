//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef STATICPROP_VISIBILITY_DX12_H
#define STATICPROP_VISIBILITY_DX12_H
#pragma once
#include "shaderapi/dx12staticpropvisibility.h"
#include "hlight_bsp.h"
#include <cstring>
#include <cmath>
#include <vector>
#include <array>
namespace shaderapidx12
{
// Asset spans must already have passed hlight::ValidateFile. Unknown identity is
// not asset corruption and deliberately has no baked domain.
inline bool ResolveStaticPropMeshDX12(const DX12StaticPropReceiver &receiver, uint64 token,
    const hlight::PropVisibilityDisk *props, uint32 propCount,
    const hlight::PropMeshVisibilityDisk *meshes, uint32 meshCount,
    uint32 directory[4], uint32 &meshIndex)
{
    if (!token || receiver.staticPropOrdinal>=propCount || !props || !meshes ||
        (receiver.meshCount && !receiver.meshes)) return false;
    const auto &prop=props[receiver.staticPropOrdinal];
    if (prop.staticPropOrdinal!=receiver.staticPropOrdinal || prop.modelChecksum!=receiver.modelChecksum ||
        prop.poseIdentity!=receiver.poseIdentity || receiver.meshCount>prop.meshCount ||
        prop.firstMesh>meshCount || prop.meshCount>meshCount-prop.firstMesh) return false;
    const DX12StaticPropMeshIdentity *selected=nullptr;
    for (uint32 i=0;i<receiver.meshCount;++i)
    {
        const auto &identity=receiver.meshes[i];
        if (identity.meshOrdinal>=prop.meshCount || !identity.meshToken ||
            (i && identity.meshOrdinal<=receiver.meshes[i-1].meshOrdinal)) return false;
        const auto &baked=meshes[prop.firstMesh+identity.meshOrdinal];
        if (baked.meshOrdinal!=identity.meshOrdinal || baked.lod!=identity.lod ||
            baked.vertexCount!=identity.vertexCount || baked.vertexOrderCRC32!=identity.vertexOrderCRC32) return false;
        if (identity.meshToken==token) { if (selected) return false; selected=&identity; }
    }
    if (!selected) return false;
    meshIndex=prop.firstMesh+selected->meshOrdinal;
    const auto &mesh=meshes[meshIndex];
    directory[0]=mesh.firstEntry; directory[1]=mesh.entryCount; directory[2]=mesh.vertexCount;
    directory[3]=mesh.directPayloadBytes?hlight::kPropMeshHasBakedLocalDirect:0;
    return true;
}
// Read only from an admitted visibility set. Check the CPU-to-shader address
// conversion as well: absence is a valid full-runtime domain, malformed blocks
// are not an unshadowed fallback.
inline bool ReadStaticPropDirectMetadataDX12(const hlight::VisibilityView &visibility,
    const hlight::PropMeshVisibilityDisk &mesh,uint32 direct[4],uint32 styles[4],uint32 &styleCount)
{
    direct[0]=hlight::kMissing; direct[1]=direct[2]=direct[3]=0;
    memset(styles,0,sizeof(uint32)*4); styleCount=0;
    if (!mesh.directPayloadBytes) return !mesh.directPayloadByteOffset;
    if (!visibility.record || !visibility.payload || mesh.directPayloadByteOffset>visibility.record->payloadBytes ||
        mesh.directPayloadBytes>visibility.record->payloadBytes-mesh.directPayloadByteOffset ||
        mesh.directPayloadBytes<sizeof(hlight::PropDirectDisk) || (mesh.directPayloadByteOffset&3)) return false;
    const auto block=hlight::GetPropDirect(visibility,mesh);
    const auto &record=*block.record;
    if (record.flags!=hlight::kPropMeshHasBakedLocalDirect || !record.styleCount || record.styleCount>4 ||
        !mesh.vertexCount || record.vertexCount!=mesh.vertexCount ||
        record.angularPlaneCount!=hlight::kPropDirectAngularPlanes ||
        (record.unbakedLightCount && record.styleCount!=4)) return false;
    const uint64 planeStride=uint64(mesh.vertexCount)*8;
    const uint64 radiance=uint64(record.styleCount)*hlight::kPropDirectAngularPlanes*planeStride;
    const uint64 radianceOffset=uint64(mesh.directPayloadByteOffset)+sizeof(record);
    const uint64 unbakedOffset=radianceOffset+radiance;
    if (record.radianceBytes!=radiance || sizeof(record)+radiance+uint64(record.unbakedLightCount)*4!=mesh.directPayloadBytes ||
        radianceOffset>=hlight::kMissing || planeStride>=hlight::kMissing || unbakedOffset>=hlight::kMissing)
        return false;
    for (uint32 i=0;i<4;++i)
    {
        if (i>=record.styleCount) { if (record.styles[i]!=255) return false; continue; }
        if (record.styles[i]>=hlight::kLightstyleCount) return false;
        for (uint32 j=0;j<i;++j) if (record.styles[j]==record.styles[i]) return false;
        styles[i]=record.styles[i];
    }
    if (record.styles[0] || record.reserved[0] || record.reserved[1]) return false;
    direct[0]=uint32(radianceOffset); direct[1]=uint32(planeStride);
    direct[2]=uint32(unbakedOffset); direct[3]=record.unbakedLightCount; styleCount=record.styleCount;
    return true;
}
inline bool StaticPropPoseMatchesDX12(const DX12StaticPropReceiver &receiver,const float actual[12])
{
    if (!actual) return false;
    for (uint32 i=0;i<12;++i) if (!std::isfinite(actual[i]) || !std::isfinite(receiver.modelToWorld[i]) ||
        actual[i]!=receiver.modelToWorld[i]) return false;
    return true;
}
using StaticPropInstanceKeyDX12 = std::array<uint64,7>;
inline bool StaticPropInstanceKeyForDrawDX12(uint64 meshToken,const float matrix[12],StaticPropInstanceKeyDX12 &key)
{
    if (!meshToken || !matrix) return false;
    key={}; key[0]=meshToken;
    uint32 words[12];
    for (uint32 i=0;i<12;++i)
    {
        if (!std::isfinite(matrix[i])) return false;
        const float value=matrix[i]==0.f?0.f:matrix[i]; // numeric exactness includes either signed zero
        memcpy(words+i,&value,4);
    }
    memcpy(key.data()+1,words,sizeof(words));
    return true;
}
// The index is built only from exact mesh-token / numeric-exact rigid-pose keys.
// Multiple coincident registered instances cannot be disambiguated by a draw.
template <class Instances>
inline typename Instances::value_type UniqueStaticPropInstanceDX12(const Instances &instances)
{
    return instances.size()==1?instances.front():typename Instances::value_type{};
}
struct StaticPropTopologyDX12
{
    const unsigned char *vertices=nullptr, *indices=nullptr;
    size_t vertexBytes=0, indexBytes=0, vertexOffset=0, positionOffset=0, indexOffset=0;
    uint32 stride=0, vertexCount=0, firstVertex=0, indexSize=0, firstIndex=0, indexCount=0;
    bool strip=false;
};
// Keep every assembled primitive, including degenerate strip connectors: the
// rasterizer's SV_PrimitiveID counts them, and alternating winding is not reset.
inline bool BuildStaticPropTrianglesDX12(const StaticPropTopologyDX12 &draw, uint32 bakedVertexCount,
    uint32 meshIndex, std::vector<DX12StaticPropTriangleGpu> &triangles)
{
    if (!draw.vertices || !draw.indices || (draw.indexSize!=2 && draw.indexSize!=4) ||
        draw.indexCount<3 || (!draw.strip && draw.indexCount%3) || !draw.stride ||
        draw.positionOffset>draw.stride || 12>draw.stride-draw.positionOffset ||
        draw.indexOffset>draw.indexBytes || uint64(draw.firstIndex)+draw.indexCount>(draw.indexBytes-draw.indexOffset)/draw.indexSize)
        return false;
    const uint32 count=draw.strip?draw.indexCount-2:draw.indexCount/3;
    triangles.resize(count);
    for (uint32 primitive=0;primitive<count;++primitive)
    {
        auto &triangle=triangles[primitive]; triangle={}; triangle.vertexIndices[3]=meshIndex;
        for (uint32 corner=0;corner<3;++corner)
        {
            uint32 element=draw.strip?primitive+corner:primitive*3+corner;
            if (draw.strip && (primitive&1) && corner<2) element=primitive+1-corner;
            const auto *source=draw.indices+draw.indexOffset+size_t(draw.firstIndex+element)*draw.indexSize;
            uint32 index=0; if (draw.indexSize==2) { uint16 value; memcpy(&value,source,2); index=value; } else memcpy(&index,source,4);
            if (index<draw.firstVertex || uint64(index)>=uint64(draw.firstVertex)+draw.vertexCount || index>=bakedVertexCount) return false;
            const uint64 offset=uint64(draw.vertexOffset)+uint64(index)*draw.stride+draw.positionOffset;
            if (offset>draw.vertexBytes || 12>draw.vertexBytes-offset) return false;
            triangle.vertexIndices[corner]=index;
            memcpy(triangle.positions[corner],draw.vertices+size_t(offset),12);
            for (uint32 axis=0;axis<3;++axis) if (!std::isfinite(triangle.positions[corner][axis])) return false;
            triangle.positions[corner][3]=1.f;
        }
    }
    return true;
}
}
#endif
