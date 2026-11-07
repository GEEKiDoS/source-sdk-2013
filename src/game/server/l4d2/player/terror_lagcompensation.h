//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_LAGCOMPENSATION_H
#define TERROR_LAGCOMPENSATION_H
#ifdef _WIN32
#pragma once
#endif

#include "mathlib/vector.h"

class CBasePlayer;
class CBaseEntity;

enum TerrorLagCompensationType
{
	TERROR_LAG_COMPENSATE_BOUNDS = 0,
	TERROR_LAG_COMPENSATE_HITBOXES,
	TERROR_LAG_COMPENSATE_HITBOXES_ALONG_RAY
};

// Uses the existing game-side lagcompensation instance. The native engine/game
// interface and its user-command envelope are not changed.
void StartTerrorLagCompensation( CBasePlayer *pPlayer, TerrorLagCompensationType type,
	const Vector &weaponPosition, const QAngle &weaponAngles, float weaponRange );

// S187950/S1879E0: the existing manager's serial-checked extra-entity registry.
void AddTerrorLagCompensatedEntity( CBaseEntity *pEntity );
void RemoveTerrorLagCompensatedEntity( CBaseEntity *pEntity );

#endif // TERROR_LAGCOMPENSATION_H
