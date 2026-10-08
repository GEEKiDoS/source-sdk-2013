//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DX12STATICPROPVISIBILITY_H
#define DX12STATICPROPVISIBILITY_H
#ifdef _WIN32
#pragma once
#endif
#include "tier0/platform.h"
#include <cstring>

// FNV1a64 over exact little-endian f32 origin/angles/lightingOrigin and u32
// authored flags. This is the baker's poseIdentity, not a proximity comparison.
inline uint64 DX12StaticPropPoseIdentity(const float origin[3], const float angles[3],
    const float lightingOrigin[3], uint32 flags)
{
    uint64 hash = 14695981039346656037ull;
    for (unsigned word = 0; word != 10; ++word)
    {
        uint32 bits = flags;
        if (word < 9)
        {
            const float *values = word < 3 ? origin : word < 6 ? angles : lightingOrigin;
            std::memcpy(&bits, values + word % 3, sizeof(bits));
        }
        for (unsigned byte = 0; byte != 4; ++byte)
        {
            hash ^= (bits >> (byte * 8)) & 255u;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

// Exact hardware mesh identity from IMDLCache::GetHardwareData. The token is
// compared, never dereferenced. Every field is copied before render-queue replay.
struct DX12StaticPropMeshIdentity
{
    uint64 meshToken;
    uint32 meshOrdinal;
    uint32 lod;
    uint32 vertexCount;
    uint32 vertexOrderCRC32;
};
struct DX12StaticPropReceiver
{
    uint32 staticPropOrdinal;
    uint32 modelChecksum;
    uint64 poseIdentity;
    float modelToWorld[12]; // authored rigid pose; studio boundary rejects a moved instance
    uint32 meshCount;
    const DX12StaticPropMeshIdentity *meshes;
    const char *modelName; // copied diagnostic label only; never receiver identity
    uint32 skin; // authored skin for diagnostics only
};

// Synchronous hardware-cache metadata. Labels are copied; the token is never
// dereferenced. modelName == NULL unregisters a token; token == 0 also clears
// all model metadata/detail rows when the client invalidates its hardware cache.
struct DX12ModelMeshMetadata
{
    uint64 meshToken;
    const char *modelName;
    uint32 modelChecksum, bodyPart, subModel, lod, studioMesh, stripGroup;
    bool staticPropModel; // model occurs in authored prop registry; not proof this draw is a static prop
};
enum DX12StaticPropVisibilityReason
{
    DX12_PROP_VISIBILITY_MAPPED,
    DX12_PROP_VISIBILITY_NON_STATIC_MODEL,
    DX12_PROP_VISIBILITY_UNKNOWN_MODEL,
    DX12_PROP_VISIBILITY_STATIC_MESH_UNREGISTERED,
    DX12_PROP_VISIBILITY_POSE_MISMATCH,
    DX12_PROP_VISIBILITY_AMBIGUOUS,
    DX12_PROP_VISIBILITY_IDENTITY_PROOF,
    DX12_PROP_VISIBILITY_UNSUPPORTED_TOPOLOGY,
    DX12_PROP_VISIBILITY_POSITION_LAYOUT,
    DX12_PROP_VISIBILITY_MISSING_POSITION_STREAM,
    DX12_PROP_VISIBILITY_INSTANCED_POSITION_STREAM,
    DX12_PROP_VISIBILITY_DYNAMIC_POSITION_STREAM,
    DX12_PROP_VISIBILITY_TOPOLOGY_PROOF,
    DX12_PROP_VISIBILITY_GEOMETRY_STAGE,
    DX12_PROP_VISIBILITY_MISSING_MESH_TOKEN,
    DX12_PROP_VISIBILITY_DIRECT_METADATA,
    DX12_PROP_VISIBILITY_RESIDENCY_FAILURE,
    DX12_PROP_VISIBILITY_REASON_COUNT
};
inline const char *DX12StaticPropVisibilityReasonName(DX12StaticPropVisibilityReason reason)
{
    static const char *const names[] = {"mapped", "nonStaticModel", "unknownModel",
        "staticMeshUnregistered", "poseMismatch", "ambiguous", "identityProof",
        "unsupportedTopology", "positionLayout", "missingPositionStream",
        "instancedPositionStream", "dynamicPositionStream", "topologyProof",
        "geometryStage", "missingMeshToken", "directMetadata", "residencyFailure"};
    return unsigned(reason) < DX12_PROP_VISIBILITY_REASON_COUNT ? names[reason] : "invalid";
}
enum DX12StaticPropDrawDiagnosticFlags
{
    DX12_PROP_DRAW_AUTHORED_STATIC_MODEL = 1u << 0,
    DX12_PROP_DRAW_MODEL_METADATA = 1u << 1,
    DX12_PROP_DRAW_TRANSLUCENT = 1u << 2,
    DX12_PROP_DRAW_ALPHA_TEST = 1u << 3,
    DX12_PROP_DRAW_AUXILIARY_KNOWN = 1u << 4,
    DX12_PROP_DRAW_COLOR_PRESENT = 1u << 5,
    DX12_PROP_DRAW_FLEX_PRESENT = 1u << 6,
    DX12_PROP_DRAW_DYNAMIC_MESH = 1u << 7,
    DX12_PROP_DRAW_POSITION_STREAM_KNOWN = 1u << 8,
    DX12_PROP_DRAW_DYNAMIC_POSITION_STREAM = 1u << 9,
    DX12_PROP_DRAW_REGISTERED_RECEIVER = 1u << 10
};
enum { DX12_STATIC_PROP_VISIBILITY_MAX_DETAILS = 4096 };
// Borrowed labels valid only during the sink callback. UINT_MAX marks unknown
// skin/model nesting/position slot. Pass is the actual backend snapshot token.
struct DX12StaticPropVisibilityDetail
{
    uint64 meshToken, materialToken, draws;
    const char *modelName, *vertexShader, *pixelShader, *materialName;
    uint32 modelChecksum, bodyPart, subModel, lod, studioMesh, stripGroup, skin;
    uint32 pass, flags, primitive, positionSlot, positionFormat, positionRepetitions;
    DX12StaticPropVisibilityReason reason;
};
class IDX12StaticPropVisibilityDetailsSink
{
public:
    virtual void OnStaticPropVisibilityDetail(const DX12StaticPropVisibilityDetail &detail) = 0;
protected:
    virtual ~IDX12StaticPropVisibilityDetailsSink() {}
};
// b1 space2. Zero triangle count explicitly disables the prop domain.
struct DX12StaticPropDrawConstants
{
    uint32 entriesAndTriangles[4]; // firstEntry, entryCount, primitiveBase, primitiveCount
    float modelToWorld[3][4];
    uint32 direct[4]; // RGB byte offset (UINT_MAX unavailable), angular-plane stride, unbaked ID offset/count
    float styles[4]; // frozen view values for the baked four-style palette, zero for unused slots
};
// t1034 space2: actual draw topology, indices relative to the baked mesh block.
struct DX12StaticPropTriangleGpu
{
    uint32 vertexIndices[4]; // xyz mesh-local vertices; w absolute map mesh-directory index
    float positions[3][4];
};
COMPILE_TIME_ASSERT(sizeof(DX12StaticPropDrawConstants) == 96);
COMPILE_TIME_ASSERT(sizeof(DX12StaticPropTriangleGpu) == 64);
#endif
