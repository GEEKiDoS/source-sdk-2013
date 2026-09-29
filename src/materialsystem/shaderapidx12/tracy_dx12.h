#pragma once
#include "tracy/Tracy.hpp"
#include <atomic>

namespace shaderapidx12
{
// True only while the profiler is started and a client is connected. Refreshed once per
// presented frame, so a disconnected per-draw zone costs one relaxed load instead of
// profiler-lifetime and connection queries.
extern std::atomic<bool> g_tracyZonesActiveDX12;
inline bool TracyZonesActiveDX12() { return g_tracyZonesActiveDX12.load(std::memory_order_relaxed); }
void RefreshTracyZonesDX12();
}

#define DX12_ZONES_ACTIVE (::shaderapidx12::TracyZonesActiveDX12())

// Zones on per-draw paths cost about 3% frame rate even while no profiler is connected (scope objects in the
// hottest functions), so they are compiled only into profiling builds that define SHADERAPIDX12_DRAW_ZONES.
// Per-frame zones stay runtime-gated by DX12_ZONES_ACTIVE.
#ifdef SHADERAPIDX12_DRAW_ZONES
#define DX12_DRAW_ZONES_ACTIVE DX12_ZONES_ACTIVE
#else
#define DX12_DRAW_ZONES_ACTIVE false
#endif
