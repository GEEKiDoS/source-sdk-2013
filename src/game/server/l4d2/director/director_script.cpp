//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/director/info_director.h"
#include "entitylist.h"
#include "fogcontroller.h"
#include "team.h"
#include "vscript_server.h"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "NextBot/NextBotInterface.h"
#include <xmmintrin.h>
#endif
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// Native5936B0; this is the actual original option, not a scripted default.
ConVar z_fog_spawn( "z_fog_spawn", "0", 0x00004004, "Set to 1 to use fogged areas as valid spawn areas" );
extern bool AreAllSurvivorsInFinale(); // Existing native2628F0/25BB10 owner.

namespace
{
class WarpSurvivorToArea
{
public:
	explicit WarpSurvivorToArea( CNavArea *area ) : m_Area( area ) {}
	bool operator()( CBasePlayer *base )
	{
		if ( !base->IsAlive() || base->GetTeamNumber() != TEAM_SURVIVOR ) return true;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		Vector position = m_Area->GetCenter();
		position.z += 18.0f;
		Vector result = position;
		// Native ignores failure and teleports using the already initialized
		// result; no nearest-area search, retry policy or unstuck substitute.
		FindPassableTerrorSpawnPosition( player, position, &result, true, 33570827u, NULL, 0.0f );
		player->Teleport( &result, &vec3_angle, &vec3_origin );
		if ( player->IsIncapacitated() ) player->OnRevived();
		return true;
	}
private:
	CNavArea *m_Area;
};
class WarpSurvivorToCheckpoint
{
public:
	explicit WarpSurvivorToCheckpoint( CTerrorCheckpoint *checkpoint ) : m_Checkpoint( checkpoint ) {}
	bool operator()( CBasePlayer *base )
	{
		if ( !base->IsAlive() || base->GetTeamNumber() != TEAM_SURVIVOR ) return true;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		Vector position = vec3_origin;
		QAngle angles = vec3_angle;
		m_Checkpoint->GetSpawnPosition( &position, &angles, NULL );
		player->Teleport( &position, &angles, &vec3_origin );
		if ( player->IsIncapacitated() ) player->OnRevived();
		return true;
	}
private:
	CTerrorCheckpoint *m_Checkpoint;
};
class IsOutsideSurvivorFogRanges
{
public:
	explicit IsOutsideSurvivorFogRanges( const Vector &position ) : m_Position( position ) {}
	bool operator()( CBasePlayer *player )
	{
		int team = player->GetTeamNumber();
		if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) return true;
		fogparams_t fog;
		player->GetFogParams( &fog );
		const Vector delta = player->GetAbsOrigin() - m_Position;
		const float distanceSquared = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		// Native266A00 does NOT test alive, fog.enable, changed-return, FOV,
		// fog.start or maxdensity. Equality is outside, and NaN does not reject.
		return !( fog.end * fog.end > distanceSquared );
	}
private:
	const Vector &m_Position;
};
void FireDirectorUserEvent( int index )
{
	CInfoDirector *director = dynamic_cast< CInfoDirector * >( gEntList.FindEntityByClassname( NULL, "info_director" ) );
	if ( director ) director->m_OnUserDefinedScriptEvent[index].FireOutput( director, director, 0.0f );
}
}

void CDirector::ScriptResetMobTimer()
{
	m_MobSpawnTimer.Start( 0.0f ); // S262E90, real660 timer; not26AAA0 policy.
}
void CDirector::ResetSpecialTimers()
{
	for ( int index = 0; index < 6; ++index ) m_BaseSpecialSpawnTimers[index].Start( 0.0f );
	FOR_EACH_VEC( m_SpecialSpawnTimers, index ) m_SpecialSpawnTimers[index].Start( 0.0f );
}
void CDirector::PlayMegaMobWarningSounds()
{
	m_pScenario->WarnMegaMobIncoming();
}
const char *CDirector::ScriptGetMapName() const
{
	const char *map = STRING( gpGlobals->mapname );
	return map ? map : "";
}
bool CDirector::ScriptIsL4D1Campaign() const
{
	return TerrorGameRules()->IsL4D1Campaign();
}
bool CDirector::IsOfflineSession() const
{
	// One explicit session owner for native25BED0, including vote/script callers.
	return m_bOfflineSession;
}
float CDirector::ScriptGetHoldoutCooldownEndTime() const
{
	return TerrorGameRules()->GetHoldoutCooldownEndTime();
}
void CDirector::ScriptSetHoldoutCooldownEndTime( float time )
{
	m_pScenario->SetCooldown( time );
}
void CDirector::ScriptForceNextStage()
{
	m_pScenario->AdvanceStage();
}
void CDirector::WarpAllSurvivorsToCheckpoint()
{
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
	if ( !checkpoint ) { DevMsg( "No exit checkpoint could be found.\n" ); return; }
	WarpSurvivorToCheckpoint warp( checkpoint );
	ForEachPlayer( warp );
}
void CDirector::WarpAllSurvivorsToBattlefield()
{
	FOR_EACH_VEC( TheNavAreas, index )
	{
		TerrorNavArea *area = static_cast< TerrorNavArea * >( TheNavAreas[index] );
		if ( area->HasSpawnAttributes( TERROR_NAV_BATTLEFIELD ) )
		{
			WarpSurvivorToArea warp( area );
			ForEachPlayer( warp );
			return;
		}
	}
	DevMsg( "No battlefield could be found.\n" );
}
void CDirector::WarpAllSurvivorsToFinale()
{
	TerrorNavArea *area = TheTerrorNavMesh()->GetGoalArea();
	if ( !area || TheTerrorNavMesh()->GetExitCheckpoint() )
	{
		DevMsg( "No finale radio could be found.\n" );
		return;
	}
	WarpSurvivorToArea warp( area );
	ForEachPlayer( warp );
}
bool CDirector::AreAllSurvivorsInFinaleArea() const
{
	return AreAllSurvivorsInFinale();
}
bool CDirector::ScriptIsAnySurvivorInStartArea() const
{
	return GetLowestSurvivorFlow( 0 ) == 0.0f;
}
bool CDirector::ScriptAreAllSurvivorsInBattlefield() const
{
	return m_pScenario->AreSurvivorsInFinaleAreaBeforePanic();
}
HSCRIPT CDirector::ScriptGetClosestCharacterWhoIsIT( HSCRIPT script ) const
{
	CBaseEntity *entity = script ? ToEnt( script ) : NULL;
	CBaseCombatCharacter *character = dynamic_cast< CBaseCombatCharacter * >( entity );
	return character ? ToHScript( FindClosestCharacterWhoIsIT( character ) ) : NULL;
}
bool CDirector::IsSpawnPositionInFog( const Vector &position ) const
{
	if ( !GetDirectorOptionInt( "ZombieSpawnInFog", z_fog_spawn.GetBool() ) ) return false;
	IsOutsideSurvivorFogRanges outside( position );
	return ForEachPlayer( outside );
}
float CDirector::ScriptGetTimeSinceSpecialZombieKilled( int zombieClass ) const
{
	// Native60640 takes raw class INTEGER; no name mapping or added bounds.
	return m_ClassSpawnIntervals[zombieClass].GetElapsedTime();
}
HSCRIPT CDirector::ScriptGetClosestActivePipeBomb( const Vector &position ) const
{
	return ToHScript( GetNearestPipeBomb( position ) );
}
int CDirector::ScriptGetPendingMobCount() const
{
	return TheZombieManager->GetPendingMobCount();
}
void CDirector::ScriptUserDefinedEvent1() { FireDirectorUserEvent( 0 ); }
void CDirector::ScriptUserDefinedEvent2() { FireDirectorUserEvent( 1 ); }
void CDirector::ScriptUserDefinedEvent3() { FireDirectorUserEvent( 2 ); }
void CDirector::ScriptUserDefinedEvent4() { FireDirectorUserEvent( 3 ); }

#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#include "l4d2/director/director_script_console_server_callbacks.inc"
