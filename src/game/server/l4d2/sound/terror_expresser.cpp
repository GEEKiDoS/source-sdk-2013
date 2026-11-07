//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

CTerrorExpresser::CTerrorExpresser( CTerrorPlayer *player ) : CMultiplayer_Expresser( player ), m_SurvivorSpeech( player ) {}
