//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_EXPRESSER_H
#define TERROR_EXPRESSER_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_speech.h"
#include "l4d2/player/bots/survivor_speech.h"
#include "l4d2/mem_zero_on_new.h"

class CTerrorPlayer;

// A specialization of the ONE CBaseMultiplayerPlayer expresser. It is not a
// second CAI_ExpresserHost, a second scene player, or a direct-sound fallback.
// CAward/CFriendlyFireAwardTemplate owns its separate native event lifecycle.
// Native speech shared the zeroed player allocation; this separate owner
// reuses that one allocator domain, not constructor resets or native RTTI.
class CTerrorExpresser : public CMultiplayer_Expresser, public CMemZeroOnNew
{
public:
	explicit CTerrorExpresser( CTerrorPlayer *player );
	CSurvivorSpeech *GetSurvivorSpeech() { return &m_SurvivorSpeech; }
private:
	CSurvivorSpeech m_SurvivorSpeech;
};

#endif // TERROR_EXPRESSER_H
