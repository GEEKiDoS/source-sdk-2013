//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_POLICY_H
#define TERROR_GLOW_POLICY_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

class C_BasePlayer;
class C_TerrorPlayer;
class ConVar;

extern ConVar cl_glow_brightness;
extern ConVar cl_colorblind;
extern ConVar spec_keep_team_glows;
extern ConVar cl_glow_it_timer_ratio_reduction;
void TerrorGlow_GetAbilityColor( Vector &color );

// Actual user0 raw player; observer resolution differs for each color owner.
C_BasePlayer *TerrorGlow_ResolveRenderPlayer( C_BasePlayer *pActiveSlotPlayer );
C_BasePlayer *TerrorGlow_ResolveItemColorPlayer( C_BasePlayer *pActiveSlotPlayer );
C_BasePlayer *TerrorGlow_ResolveTeamColorPlayer( C_BasePlayer *pActiveSlotPlayer, bool bPlayerControlledZombies );
bool TerrorGlow_IsSurvivorTeam( int nTeam );

int TerrorGlow_GetSurvivorHealthLevel( const C_TerrorPlayer &player );
void TerrorGlow_GetSurvivorHealthColor( const C_TerrorPlayer &player, Vector &color );

// Outline countdown ratio C10270480, not material fade C10253C80.
float TerrorGlow_GetITCompositeScale( const C_TerrorPlayer &player );
float TerrorGlow_GetDistanceAlpha( float flDistance, double flFOVRatio, float flCurrentAlpha );

#endif // TERROR_GLOW_POLICY_H
