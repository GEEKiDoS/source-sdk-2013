//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/nav/terror_nav.h"
#include "gamerules.h"

#include "tier0/memdbgon.h"

bool g_bAllowTerrorObserverEyePosition = true;
extern ConVar nav_obscure_range;

namespace
{
const unsigned int s_spawnVisibilityMask = 0x02006041;
const unsigned int s_playerSpawnMask = 0x0202420B;

// S:28F4C0. The area and lazy-lookup flag are shared across players (and,
// for the hull query, across all five points). No PVS approximation is used.
bool IsVisibleToPlayer( const Vector &position, CBasePlayer *player, int team,
	unsigned char flags, float maxRange, CBaseEntity *ignore,
	TerrorNavArea **area, bool *areaLookupAttempted )
{
	g_bAllowTerrorObserverEyePosition = false;
	bool visible = false;
	if ( player->IsAlive() && ( team == TEAM_ANY || player->GetTeamNumber() == team ) )
	{
		Vector eye = player->EyePosition();
		Vector delta = eye - position;
		float rangeSquared = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
		if ( !( maxRange > 0.0f && rangeSquared > maxRange * maxRange ) &&
			( ( flags & 1 ) || player->FInViewCone( position ) ) &&
			!player->IsHiddenByFog( position ) )
		{
			if ( !*areaLookupAttempted && !*area && ( !( flags & 4 ) || !( flags & 2 ) ) )
			{
				*area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( position,
					false, 100.0f, false, true, TEAM_ANY ) );
				*areaLookupAttempted = true;
			}
			if ( !( !( flags & 4 ) && *area && ( *area )->HasSpawnAttributes( TERROR_NAV_OBSCURED ) &&
				rangeSquared > nav_obscure_range.GetFloat() * nav_obscure_range.GetFloat() ) )
			{
				CTraceFilterNoNPCsOrPlayer filter( ignore, COLLISION_GROUP_NONE );
				trace_t trace;
				UTIL_TraceLine( eye, position, s_spawnVisibilityMask, &filter, &trace );
				visible = trace.fraction >= 1.0f && !trace.allsolid && !trace.startsolid;
			}
		}
	}
	g_bAllowTerrorObserverEyePosition = true;
	return visible;
}

bool IsClear( const Vector &position, const Vector &mins, const Vector &maxs,
	unsigned int mask, ITraceFilter *filter )
{
	trace_t trace;
	UTIL_TraceHull( position, position, mins, maxs, mask, filter, &trace );
	return trace.fraction == 1.0f && trace.allsolid != 1 && trace.startsolid != 1;
}

}
Vector DropSpawnToGround( CBaseEntity *entity, const Vector &position,
	const Vector &mins, const Vector &maxs )
{
	unsigned int mask = 0x0200400B;
	if ( entity && entity->IsPlayer() )
		mask = static_cast< CBasePlayer * >( entity )->PlayerSolidMask();
	CTraceFilterSimple filter( entity, COLLISION_GROUP_NONE );
	trace_t trace;
	UTIL_TraceHull( position, position - Vector( 0, 0, 500 ), mins, maxs, mask, &filter, &trace );
	return trace.endpos;
}

bool IsTerrorSpawnPointVisibleToTeam( const Vector &position, int team,
	unsigned char flags, float maxRange, TerrorNavArea *area, CBaseEntity *ignore )
{
	bool areaLookupAttempted = false;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && IsVisibleToPlayer( position, player, team, flags, maxRange,
			ignore, &area, &areaLookupAttempted ) )
			return true;
	}
	return false;
}

bool IsTerrorSpawnHullVisibleToTeam( const Vector &position, int team,
	unsigned char flags, TerrorNavArea *area, float width, float height )
{
	bool areaLookupAttempted = false;
	float halfWidth = width * 0.5f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player )
			continue;
		Vector eye = player->EyePosition();
		Vector toPosition( position.x - eye.x, position.y - eye.y, 0 );
		toPosition.NormalizeInPlace();
		Vector side( toPosition.y * halfWidth, -toPosition.x * halfWidth, 0 );
		Vector points[5];
		points[0] = position + side;
		points[1] = position - side;
		points[2] = position + Vector( 0, 0, height * 0.5f );
		points[3] = points[0] + Vector( 0, 0, height );
		points[4] = points[1] + Vector( 0, 0, height );
		for ( int j = 0; j < 5; ++j )
		{
			if ( IsVisibleToPlayer( points[j], player, team, flags, 0.0f, NULL,
				&area, &areaLookupAttempted ) )
				return true;
		}
	}
	return false;
}

bool IsTerrorSpawnEntityVisibleToTeam( CBaseEntity *entity, int team,
	unsigned char flags, float maxRange, TerrorNavArea *area )
{
	if ( entity->GetTeamNumber() == team )
		return true;
	if ( !area )
	{
		CBaseCombatCharacter *character = entity->MyCombatCharacterPointer();
		if ( character )
			area = ToTerrorNavArea( character->GetLastKnownArea() );
	}
	if ( IsTerrorSpawnPointVisibleToTeam( entity->WorldSpaceCenter(), team, flags,
		maxRange, area, entity ) )
		return true;
	if ( IsTerrorSpawnPointVisibleToTeam( entity->GetAbsOrigin(), team, flags,
		maxRange, area, entity ) )
		return true;
	return IsTerrorSpawnPointVisibleToTeam( entity->EyePosition(), team, flags,
		maxRange, area, entity );
}

bool FindPassableTerrorSpawnPosition( CBaseEntity *entity, const Vector &position,
	Vector *result, bool dropToGround, unsigned int mask, ITraceFilter *filter,
	float hullBloat )
{
	CTraceFilterSimple defaultFilter( entity, COLLISION_GROUP_NONE );
	if ( !filter )
		filter = &defaultFilter;
	Vector mins;
	Vector maxs;
	if ( entity )
	{
		mins = entity->CollisionProp()->OBBMins();
		maxs = entity->CollisionProp()->OBBMaxs();
	}
	else
	{
		const CViewVectors *view = g_pGameRules->GetViewVectors();
		mins = view->m_vHullMin;
		maxs = view->m_vHullMax;
	}
	if ( hullBloat > 0.0f )
	{
		mins -= Vector( hullBloat, hullBloat, hullBloat );
		maxs += Vector( hullBloat, hullBloat, hullBloat );
	}
	if ( IsClear( position, mins, maxs, mask, filter ) )
	{
		*result = dropToGround ? DropSpawnToGround( entity, position, mins, maxs ) : position;
		return true;
	}
	Vector size = maxs - mins;
	for ( int step = 0; step < 16; ++step )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			float offset = size[axis] * static_cast< float >( step );
			for ( int sign = -1; sign < 3; sign += 2 )
			{
				Vector candidate = position;
				candidate[axis] += static_cast< float >( sign ) * offset;
				if ( !IsClear( candidate, mins, maxs, mask, filter ) )
					continue;
				trace_t trace;
				UTIL_TraceLine( position, candidate, mask, filter, &trace );
				if ( trace.fraction == 1.0f )
				{
					*result = dropToGround ? DropSpawnToGround( entity, candidate, mins, maxs ) : candidate;
					return true;
				}
			}
		}
	}
	return false;
}

bool FindZombieSpawnPosition( TerrorNavArea *area, int tries, Vector *position,
	bool centerFirst )
{
	for ( int i = 0; i < tries; ++i )
	{
		Vector candidate;
		if ( i == 0 && centerFirst )
			candidate = area->GetCenter() + Vector( 0, 0, 10 );
		else
			candidate = area->GetRandomSpawnPoint();
		if ( FindPassableTerrorSpawnPosition( NULL, candidate, position, false,
			s_playerSpawnMask, NULL, 0.03125f ) )
			return true;
	}
	return false;
}
