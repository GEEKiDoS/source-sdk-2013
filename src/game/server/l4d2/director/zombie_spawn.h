//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_ZOMBIE_SPAWN_H
#define L4D2_ZOMBIE_SPAWN_H
#ifdef _WIN32
#pragma once
#endif

class TerrorNavArea;
class CBaseEntity;
class CBasePlayer;
class ITraceFilter;

// S350C20, shared actual command name mapper; existing body in infected_commands.cpp.
int ZombieClassFromName( const char *name );

// Native 28F4C0/28F980/28F6D0. Flags are independent bits: 1 ignores
// FOV, 2 suppresses the lazy NAV lookup only when bit 4 is also set,
// and 4 ignores the OBSCURED distance restriction. They do not skip traces.
bool IsTerrorSpawnPointVisibleToTeam( const Vector &position, int team,
	unsigned char flags = 0, float maxRange = 0.0f, TerrorNavArea *area = NULL,
	CBaseEntity *ignore = NULL );
bool IsTerrorSpawnHullVisibleToTeam( const Vector &position, int team,
	unsigned char flags, TerrorNavArea *area, float width, float height );
bool IsTerrorSpawnEntityVisibleToTeam( CBaseEntity *entity, int team,
	unsigned char flags = 0, float maxRange = 0.0f, TerrorNavArea *area = NULL );

// Native23B9D0: one500-down hull trace, even when blocked.
Vector DropSpawnToGround( CBaseEntity *entity, const Vector &position,
	const Vector &mins, const Vector &maxs );

// Native 23BAA0. The original player-hull clearance search; this does not
// search NAV, repair a spawn queue, or accept a blocked initial position.
bool FindPassableTerrorSpawnPosition( CBaseEntity *entity, const Vector &position,
	Vector *result, bool dropToGround, unsigned int mask,
	ITraceFilter *filter = NULL, float hullBloat = 0.0f );

// Native 358A40. A center-first attempt does not consume the area's random
// point draws. All subsequent candidates use TerrorNavArea::GetRandomSpawnPoint.
bool FindZombieSpawnPosition( TerrorNavArea *area, int tries,
	Vector *position, bool centerFirst );

// Native 107A67A8, read by the real player EyePosition/EyeAngles overrides.
// Spawn visibility sets false before each player's query and then TRUE, even
// when it was already false; this is deliberately not a save/restore scope.
extern bool g_bAllowTerrorObserverEyePosition;

// Native35AF70: the single actual command/script materializer.
bool SpawnZombie( int zombieClass, Vector position, QAngle angles,
	int wound, bool ragdoll, bool inArea );
#endif
