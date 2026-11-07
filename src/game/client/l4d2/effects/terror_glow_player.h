//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_PLAYER_H
#define TERROR_GLOW_PLAYER_H
#ifdef _WIN32
#pragma once
#endif
#include "util_shared.h"

// C_TerrorPlayer+10256+24*slot in the 32-bit original. The two native
// network-capable IntervalTimers are retained, not replaced by one fade timer.
struct TerrorGlowPlayerState_t
{
	float m_flAlpha;
	bool m_bAbilityHighlight;
	IntervalTimer m_NotVisibleTimer;
	IntervalTimer m_VisibleTimer;
};

class C_BaseEntity;
class C_BasePlayer;
class C_TerrorPlayer;
class ConVar;
extern ConVar sv_disable_glow_survivors;
extern ConVar sv_disable_glow_faritems;
extern ConVar spec_keep_team_glows;

C_BaseEntity *TerrorGlow_FindRescueModel( const C_TerrorPlayer *pPlayer );
bool TerrorGlow_IsIT( const C_TerrorPlayer &player );
#endif
