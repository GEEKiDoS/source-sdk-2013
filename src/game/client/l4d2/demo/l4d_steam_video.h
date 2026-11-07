//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_L4D_STEAM_VIDEO_H
#define L4D2_L4D_STEAM_VIDEO_H
#ifdef _WIN32
#pragma once
#endif

#include "igamesystem.h"
#include "GameEventListener.h"

class L4DSteamVideo : public CAutoGameSystem, public CGameEventListener
{
public:
	L4DSteamVideo();
	virtual bool Init() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
private:
	bool m_enabled;
};

#endif // L4D2_L4D_STEAM_VIDEO_H
