//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HLIGHT_ENGINE_BRIDGE_H
#define HLIGHT_ENGINE_BRIDGE_H
#ifdef _WIN32
#pragma once
#endif

#include "hlight_bsp.h"
#include "shaderapi/ishaderapi.h"
#include <array>
#include <memory>
#include <vector>

class IMaterialSystem;

namespace shaderapidx12
{
struct HlightNativeFace
{
    int32 mins[2] = {};
    uint32 extents[2] = {};
    uint8 styles[4] = {};
    uint32 flags = 0; // hlight::kFaceBumped / kFaceDisplacement only; not arbitrary native flags
};
struct HlightNativeDomain
{
    uint64 mapGeneration = 0;
    char mapName[260] = {};
    uint32 faceLump = 0, lightingLump = 0;
    std::array<hlight::LumpIdentity, HEADER_LUMPS> identities = {};
    std::array<bool, HEADER_LUMPS> identityPresent = {};
    std::vector<HlightNativeFace> faces;
};
struct HlightNativePage
{
    ShaderAPITextureHandle_t handle = (ShaderAPITextureHandle_t)-1;
    uint32 width = 0, height = 0;
};
struct HlightNativePlacement
{
    int32 page = -1; // negative native special pages are explicit, never fabricated owners
    uint32 origin[2] = {}; // native interior origin, excluding the one-texel border
    uint32 planeCount = 0; // 1 or 4 for allocated pages; native stride = extentS + 3
};
struct HlightNativeModelRange
{
    uint32 firstFace = 0, faceCount = 0;
};
struct HlightNativeAtlas
{
    std::shared_ptr<const HlightNativeDomain> domain;
    uint64 layoutGeneration = 0;
    std::vector<HlightNativePage> pages;
    std::vector<HlightNativePlacement> placements; // indexed by exact native face ordinal
    std::vector<HlightNativeModelRange> models;
};

// All data is copied/owned. No engine surface, helper-buffer, vector, or overlay pointer
// crosses this boundary. Lifecycle notifications execute on the controlling thread after
// queued work drains. Dynamic publication executes on the actual native build thread.
class IHlightNativeSink
{
public:
    virtual ~IHlightNativeSink() = default;
    // Called after face construction, before initial native lightmap builds. Return true
    // only when an enhanced asset needs dynamic capture. An ordinary map returns false.
    virtual bool OnNativeDomain(std::shared_ptr<const HlightNativeDomain> domain) = 0;
    virtual void OnNativeAtlas(std::shared_ptr<const HlightNativeAtlas> atlas) = 0;
    // planeMask selects actual executed native planes. Each non-null span is packed RGB,
    // width*height triples, valid only during this call. mask==0 publishes whole-face zero.
    virtual void OnNativeDynamic(uint64 mapGeneration, uint32 faceOrdinal, uint32 planeMask,
        uint32 width, uint32 height, const float *const rgb[4]) = 0;
    // Retire associations after CPU queue drain; GPU resources retain fence ownership.
    virtual void OnNativeRetire(uint64 mapGeneration) = 0;
    virtual void OnNativeResourceRelease(uint64 mapGeneration) = 0;
    // Explicit error channel, including worker errors; never use the old Present latch.
    virtual void OnNativeFailure(const char *reason) = 0;
};

namespace HlightEngineBridge
{
// Unsupported profiles return false and expose a reason; ordinary maps remain operational.
// Install before the first map load. Hook enable/disable is all-or-nothing.
bool Initialize(IMaterialSystem *materials, IHlightNativeSink *sink);
void Shutdown();
bool Supported();
const char *CompatibilityError();
// Pair around owned-client level shutdown BEFORE entity/leaf teardown. Nested native
// transactions preserve prior queue permission, ideal mode, and service-thread selection.
void BeginClientLevelShutdown();
void EndClientLevelShutdown();
// Resource readmission uses the same drain but retains native domain/atlas/dynamics.
bool BeginClientResourceReadmission();
void EndClientResourceReadmission();
// Material-thread quiescence without a native domain (used to refresh loaded materials on any map). Main thread
// only; nests with the transactions above. Returns false when the bridge is unsupported or the queue did not drain.
bool BeginMaterialTransaction();
void EndMaterialTransaction();
}
}
#endif
