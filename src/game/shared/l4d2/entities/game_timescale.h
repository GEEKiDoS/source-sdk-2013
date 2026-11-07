#ifndef GAME_TIMESCALE_H
#define GAME_TIMESCALE_H
#ifdef _WIN32
#pragma once
#endif
#include "igamesystem.h"
class CGameTimescale : public CAutoGameSystemPerFrame
{
public:
	CGameTimescale();
	bool Init() OVERRIDE;
	void Shutdown() OVERRIDE;
	void LevelInitPostEntity() OVERRIDE;
	void LevelShutdownPostEntity() OVERRIDE;
#ifdef CLIENT_DLL
	void Update( float ) OVERRIDE;
#else
	void FrameUpdatePostEntityThink() OVERRIDE;
#endif
	void SetCurrentTimescale( float scale );
	void SetDesiredTimescale( float scale, float acceleration, float minBlendRate, float blendDeltaMultiplier );
private:
	void ResetDefaults();
	void UpdateTimescale();
	float m_flDesiredTimescale;
	float m_flCurrentTimescale;
	float m_flBlendRate;
	float m_flAcceleration;
	float m_flMinBlendRate;
	float m_flBlendDeltaMultiplier;
	float m_flLastRealTime;
};
CGameTimescale *GameTimescale();
#endif
