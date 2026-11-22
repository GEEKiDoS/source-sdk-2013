#pragma once
#include <atomic>
#include "tier0/platform.h"

namespace shaderapidx12
{
// Cumulative, not stats-block means: console snapshots can bracket a single mob.
struct FirstUseStatsDX12
{
	std::atomic<uint64_t> decodes{}, decodeUs{}, uploads{}, textures{}, creates{}, libraryHits{};
};
inline FirstUseStatsDX12 g_FirstUse;
}
