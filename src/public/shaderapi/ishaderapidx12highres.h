//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef ISHADERAPIDX12HIGHRES_H
#define ISHADERAPIDX12HIGHRES_H
#ifdef _WIN32
#pragma once
#endif
#include "tier0/platform.h"
#include "tier1/interface.h"

#define SHADERAPIDX12_HIGHRES_INTERFACE_VERSION "ShaderAPIDX12HighresLightmaps_003"

enum DX12HighresMapState
{
    DX12_HIGHRES_ORDINARY,
    DX12_HIGHRES_PENDING,
    DX12_HIGHRES_READY,
    DX12_HIGHRES_REJECTED
};
struct DX12HighresMapStatus
{
    DX12HighresMapState state;
    uint32 density;
    uint64 nativeMapGeneration, layoutGeneration;
    uint64 gpuBytes, assetBytes;
    uint32 faceCount, pageCount;
    uint32 faceLump, lightingLump; // actual independently selected native domain, including inactive ordinary modes
    // Ambient probe grid of the selected mode. probeBrickCount is the validated asset's (0 when the asset is absent or
    // invalid); probeBytes are the committed GPU bytes (0 until the first PBR probe consumer draws). probeError holds the
    // missing/invalid reason (empty when a grid is available or the map has no enhanced domain); it never rejects the map.
    uint32 probeBrickCount;
    uint64 probeBytes;
    char probeError[160];
};
class IShaderAPIDX12HighresLightmaps
{
public:
    // Synchronous controlling-thread admission. Does not attach to an already loaded
    // native map or invent metadata when its startup bridge was unavailable.
    virtual bool RequireMap(const char *mapName, DX12HighresMapStatus &out,
        char *error, int errorBytes) = 0;
    // Observational status; errors are not consumed. Rejection never latches Present.
    virtual void GetStatus(DX12HighresMapStatus &out, char *error, int errorBytes) = 0;
    // Pair before/after the complete client level shutdown, before entity/leaf teardown.
    // Already queued rendering drains with its old resources intact before retirement.
    virtual void BeginClientLevelShutdown() = 0;
    virtual void EndClientLevelShutdown() = 0;
    // Frame-start resource readmission: drain old work before changing material
    // generations/snapshots, without retiring the native domain or dynamic data.
    // On success, pair with End to restore exact queue permission/mode/service.
    virtual bool BeginClientResourceReadmission() = 0;
    virtual void EndClientResourceReadmission() = 0;
protected:
    virtual ~IShaderAPIDX12HighresLightmaps() = default;
};
#endif
