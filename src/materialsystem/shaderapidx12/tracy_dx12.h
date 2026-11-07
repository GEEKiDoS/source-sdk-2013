//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Tracy zone gating for the DX12 shader API.
//
//=============================================================================//

#ifndef TRACY_DX12_H
#define TRACY_DX12_H
#pragma once

#include "tier0/threadtools.h"
#include "tracy/Tracy.hpp"

namespace shaderapidx12
{
// Nonzero only while the profiler is started and a client is connected. Refreshed once per
// presented frame, so a disconnected per-draw zone costs one plain load instead of
// profiler-lifetime and connection queries.
extern CInterlockedInt g_bTracyZonesActiveDX12;

inline bool TracyZonesActiveDX12()
{
	return g_bTracyZonesActiveDX12 != 0;
}

void RefreshTracyZonesDX12();
} // namespace shaderapidx12

#define DX12_ZONES_ACTIVE ( ::shaderapidx12::TracyZonesActiveDX12() )

// Zones on per-draw paths cost about 3% frame rate even while no profiler is connected (scope objects in the
// hottest functions), so they are compiled only into profiling builds that define SHADERAPIDX12_DRAW_ZONES.
// Per-frame zones stay runtime-gated by DX12_ZONES_ACTIVE.
#ifdef SHADERAPIDX12_DRAW_ZONES
#define DX12_DRAW_ZONES_ACTIVE DX12_ZONES_ACTIVE
#else
#define DX12_DRAW_ZONES_ACTIVE false
#endif

#endif // TRACY_DX12_H
