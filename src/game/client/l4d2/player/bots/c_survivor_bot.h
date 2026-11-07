//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_SURVIVOR_BOT_H
#define C_SURVIVOR_BOT_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/c_terrorplayer.h"

class C_SurvivorBot : public C_TerrorPlayer
{
public:
	DECLARE_CLASS( C_SurvivorBot, C_TerrorPlayer );
	DECLARE_CLIENTCLASS();

	virtual bool IsBot( void ) const OVERRIDE;
	virtual bool ShouldDrawSpeechVoice() OVERRIDE;
	int GetHumanSpectatorUserID() const { return m_humanSpectatorUserID; }
	int GetHumanSpectatorEntIndex() const { return m_humanSpectatorEntIndex; }

private:
	int m_humanSpectatorUserID;
	int m_humanSpectatorEntIndex;
};

#endif // C_SURVIVOR_BOT_H
