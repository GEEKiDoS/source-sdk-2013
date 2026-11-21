//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef GPU_OPTIMIZED_SETTINGS_H
#define GPU_OPTIMIZED_SETTINGS_H
#ifdef _WIN32
#pragma once
#endif

namespace GpuOptimizedSettings
{
enum Preset_t
{
	PRESET_LOW = 0,
	PRESET_MEDIUM,
	PRESET_HIGH,
	PRESET_ULTRA,
	PRESET_UNKNOWN
};

enum Source_t
{
	SOURCE_DATABASE,
	SOURCE_ESTIMATE,
	SOURCE_INTEGRATED,
	SOURCE_UNKNOWN
};

struct Recommendation_t
{
	char m_szAdapter[512];
	char m_szEntry[128];
	Preset_t m_Preset;
	Source_t m_Source;
};

bool GetRecommendation( Recommendation_t &out );
bool ApplyRecommended();
const char *PresetToken( Preset_t preset );

void ApplyAdapterQualityTier();
void OnMenuReady();
}
#endif
