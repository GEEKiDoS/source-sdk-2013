//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/content/terror_model_precache.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_speech.h"
#include "l4d2/sound/terror_expresser.h"
#include "l4d2/player/transitioning_player.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "l4d2/gamerules/terror_checkpoint_respawn.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_path.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/entities/info_changelevel.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/stats/player_reporter.h"
#include "team.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "datacache/imdlcache.h"
#include "tier0/icommandline.h"
#include "tier0/memdbgon.h"

extern bool g_bTerrorPressDemo; // Same native107F7735 DLLInit-owned cache.

// S09E020, exported from the real CommentarySystem owner by the integration
// packet. This aborts only its active node; it does not shut down commentary.
extern void AbortCurrentCommentary();

namespace
{
CTerrorPlayer *GetPlayerForTransitionPass( int index )
{
	// S2B64B0/6540/65E0/6670 check the edict, nonzero index, IsPlayer and
	// connection-state!=PlayerDisconnected(2). IsConnected deliberately
	// includes a disconnect in progress. Takeover can change the live roster.
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( index ) );
	return player && player->edict() && player->entindex() && player->IsPlayer() && player->IsConnected() ? player : NULL;
}
bool IsSurvivorTeam( int team ) { return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR; }
const char *const s_upgradeNames[] = { "INCENDIARY_AMMO", "EXPLOSIVE_AMMO", "LASER_SIGHT" };
void SaveVector( KeyValues *data, const char *key, const Vector &value )
{
	data->SetString( key, CFmtStr( "%f %f %f", value.x, value.y, value.z ) );
}
Vector LoadVector( KeyValues *data, const char *key )
{
	Vector value = vec3_origin;
	sscanf( data->GetString( key, "" ), "%f %f %f", &value.x, &value.y, &value.z );
	return value;
}
QAngle LoadAngles( KeyValues *data )
{
	Vector value = LoadVector( data, "angles" );
	return QAngle( value.x, value.y, value.z );
}
void SaveWeapon( KeyValues *data, int id, CTerrorWeapon *weapon )
{
	// S2B6880: repeated children named weapon, not a map indexed by ID.
	KeyValues *saved = new KeyValues( "weapon" );
	data->AddSubKey( saved );
	saved->SetInt( "weaponID", id );
	saved->SetInt( "weaponskin", weapon->m_nSkin );
	if ( id == TERROR_WEAPON_MELEE )
	{
		CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( weapon );
		if ( melee && melee->GetMeleeInfo() )
		{
			saved->SetInt( "meleeWeapon", 1 );
			saved->SetString( "meleeWeaponName", melee->GetMeleeInfo()->m_szName );
		}
	}
	saved->SetInt( "weaponSlot", weapon->GetSlot() );
	saved->SetInt( "currentMagazine", weapon->Clip1() );
	for ( int i = 0; i < ARRAYSIZE( s_upgradeNames ); ++i )
		if ( weapon->HasUpgrade( i ) ) saved->SetInt( CFmtStr( "UPGRADE_%s", s_upgradeNames[i] ), 1 );
	saved->SetInt( "upgradedAmmoCount", (unsigned char)weapon->m_nUpgradedPrimaryAmmoLoaded );
	const int ammo = weapon->GetPrimaryAmmoType();
	saved->SetInt( "extraAmmo", ammo == -1 ? 0 : weapon->GetOwner()->GetAmmoCount( ammo ) );
	CTerrorGun *gun = weapon->GetTerrorGun();
	if ( gun )
	{
		saved->SetInt( "dual", gun->IsDualWielding() );
		saved->SetInt( "reloaded", !gun->m_inInitialPickup );
	}
}
void RestoreWeapon( CTerrorPlayer *player, int id, KeyValues *data )
{
	// S2B7970: the give operation, including its native failure, is authoritative.
	if ( !id ) return;
	CFmtStr ordinaryName( "weapon_%s", WeaponIDToAlias( id ) );
	const char *name = data->GetInt( "meleeWeapon", 0 ) ? data->GetString( "meleeWeaponName", "" ) : ordinaryName.Access();
	CWeaponCSBase *weapon = dynamic_cast< CWeaponCSBase * >( player->GiveNamedItemInternal( name, data->GetInt( "reloaded", 0 ), true, NULL ) );
	if ( !weapon ) { Warning( "Couldn't give unknown weapon %d\n", id ); return; }
	weapon->m_nSkin = data->GetInt( "weaponskin", 0 );
	CTerrorGun *gun = weapon->GetTerrorGun();
	if ( gun )
	{
		if ( data->GetInt( "dual", 0 ) ) gun->AcquireDualWeapons();
		if ( data->GetInt( "reloaded", 0 ) ) gun->m_inInitialPickup = false;
		for ( int i = 0; i < ARRAYSIZE( s_upgradeNames ); ++i )
			if ( data->GetInt( CFmtStr( "UPGRADE_%s", s_upgradeNames[i] ), 0 ) ) gun->AddUpgrade( i );
		gun->m_nUpgradedPrimaryAmmoLoaded = (unsigned char)data->GetInt( "upgradedAmmoCount", 0 );
	}
	weapon->m_iClip1 = data->GetInt( "currentMagazine", 0 );
	if ( weapon->GetPrimaryAmmoType() != -1 ) player->SetAmmoCount( data->GetInt( "extraAmmo", 0 ), weapon->GetPrimaryAmmoType() );
}
void ChangeSavedTeam( KeyValues *data, int team )
{
	// S2B62F0 mutates the saved record and removes ALL repeated weapon keys.
	data->SetInt( "teamNumber", team );
	data->SetInt( "state", STATE_OBSERVER_MODE );
	data->SetInt( "health", 0 );
	data->SetFloat( "healthBuffer", 0.0f );
	data->SetInt( "fumes", 0 );
	data->SetInt( "incapacitated", 0 );
	KeyValues *weapon;
	while ( ( weapon = data->FindKey( "weapon" ) ) != NULL ) { data->RemoveSubKey( weapon ); weapon->deleteThis(); }
}
void FirePlayerEvent( const char *name, CTerrorPlayer *player )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( event ) { event->SetInt( "userid", player->GetUserID() ); gameeventmanager->FireEvent( event ); }
}
void SendTransitionMessage( CTerrorPlayer *player, const char *name )
{
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, name );
	MessageEnd();
}
void RemoveActiveBackpackForSave( CTerrorPlayer *player, bool bot )
{
	CBaseBackpackItem *item = dynamic_cast< CBaseBackpackItem * >( player->GetActiveWeapon() );
	if ( item && item->IsPerformingAction() )
	{
		item->StopAction();
		if ( bot || item->IsPerformingAction() ) { player->Weapon_Drop( item, NULL, NULL ); UTIL_Remove( item ); }
	}
}
// S2B6120. The two hull traces have distinct team masks and use debris-trigger
// collision group8. The second trace must hit ground within128 units.
bool IsNearbySpawnClear( const Vector &position, bool ducked, int team )
{
	const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
	const Vector &mins = ducked ? vectors->m_vDuckHullMin : vectors->m_vHullMin;
	const Vector &maxs = ducked ? vectors->m_vDuckHullMax : vectors->m_vHullMax;
	const unsigned int mask = IsSurvivorTeam( team ) ? 33636875u : 33702411u;
	trace_t trace;
	UTIL_TraceHull( position, position, mins, maxs, mask, NULL, COLLISION_GROUP_DEBRIS_TRIGGER, &trace );
	if ( trace.startsolid || trace.fraction < 1.0f ) return false;
	UTIL_TraceHull( position, position - Vector( 0, 0, 128 ), mins, maxs, mask, NULL, COLLISION_GROUP_DEBRIS_TRIGGER, &trace );
	return !trace.startsolid && trace.fraction < 1.0f;
}
class CCollectTransitionSpawnAreas
{
public:
	CCollectTransitionSpawnAreas( int team ) : m_team( team ) {}
	bool operator()( CNavArea *base )
	{
		TerrorNavArea *area = ToTerrorNavArea( base );
		if ( area->HasSpawnAttributes( TERROR_NAV_STOP_SCAN ) ) return false;
		if ( ( !IsSurvivorTeam( m_team ) || area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) &&
			( m_team != TEAM_INFECTED || !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) ) m_areas.AddToTail( area );
		return true;
	}
	CUtlVector< TerrorNavArea * > m_areas;
private:
	int m_team;
};
}
bool FindNearbyTransitionSpawn( CTerrorPlayer *player, Vector *position, int team, bool ducked, float range )
{
	// S2B8400: grid iteration order,100 random draws WITH replacement, strict
	// travelDistance<range, and the failed-path -1 acceptance bug are retained.
	Extent extent;
	extent.lo = *position - Vector( range, range, range );
	extent.hi = *position + Vector( range, range, range );
	CCollectTransitionSpawnAreas collect( team );
	TheNavMesh->ForAllAreasOverlappingExtent( collect, extent );
	CNavArea *start = TheNavMesh->GetNearestNavArea( *position, false, 10000.0f, false, true );
	if ( !collect.m_areas.Count() ) return false;
	for ( int attempt = 0; attempt < 100; ++attempt )
	{
		TerrorNavArea *area = collect.m_areas[RandomInt( 0, collect.m_areas.Count() - 1 )];
		if ( ( team != TEAM_INFECTED || ( player && player->IsGhost() ) || !area->IsBlocked( TEAM_SURVIVOR ) ) &&
			!area->IsBlocked( team ) && !area->IsDamaging() && range > ( start ? GetTerrorNavTravelDistance( start, area ) : 0.0f ) )
		{
			*position = area->GetRandomPoint();
			if ( IsNearbySpawnClear( *position, ducked, team ) ) return true;
		}
	}
	return false;
}
namespace
{
Vector GetSafeTransitionSpawn( CTerrorPlayer *player, const Vector &ideal, bool ducked, int team, QAngle *angles )
{
	Vector result = ideal;
	if ( TheDirector->IsRestoringByCharacter() )
	{
		CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
		if ( checkpoint ) checkpoint->GetSpawnPosition( &result, angles, NULL );
	}
	CNavArea *area = TheNavMesh->GetNearestNavArea( ideal, false, 10000.0f, false, true );
	const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
	if ( team == TEAM_INFECTED && area && area->IsBlocked( TEAM_SURVIVOR ) &&
		( IsTerrorSpawnPointVisibleToTeam( ideal, TEAM_SURVIVOR, 0, 0.0f, NULL, NULL ) ||
		  IsTerrorSpawnPointVisibleToTeam( ideal + vectors->m_vView, TEAM_SURVIVOR, 0, 0.0f, NULL, NULL ) ) )
	{
		if ( FindNearbyTransitionSpawn( player, &result, team, ducked, 1000.0f ) ||
			FindNearbyTransitionSpawn( player, &result, team, ducked, 2000.0f ) ||
			FindNearbyTransitionSpawn( player, &result, team, ducked, 3000.0f ) ||
			FindNearbyTransitionSpawn( player, &result, team, ducked, 10000.0f ) ) return result;
		return vec3_origin;
	}
	trace_t trace;
	const Vector &mins = ducked ? vectors->m_vDuckHullMin : vectors->m_vHullMin;
	const Vector &maxs = ducked ? vectors->m_vDuckHullMax : vectors->m_vHullMax;
	const unsigned int mask = player ? player->PhysicsSolidMaskForEntity() : 33570827u;
	UTIL_TraceHull( result, result, mins, maxs, mask, player, COLLISION_GROUP_NONE, &trace );
	if ( ( result.x <= -0.01f || result.x >= 0.01f || result.y <= -0.01f || result.y >= 0.01f || result.z <= -0.01f || result.z >= 0.01f ) && trace.fraction == 1.0f && !trace.allsolid && !trace.startsolid ) return result;
	if ( FindNearbyTransitionSpawn( player, &result, team, ducked, 150.0f ) || FindNearbyTransitionSpawn( player, &result, team, ducked, 1000.0f ) ) return result;
	return team == TEAM_INFECTED ? vec3_origin : ideal;
}
}

void CTerrorPlayer::SaveIdleStateForTransition()
{
	// S2FD230 only ever SETS16352; it does not clear a previous true value.
	if ( GetTeamNumber() == TEAM_SURVIVOR && !IsBot() )
	{
		if ( m_wasIdle ) m_isIdle = true;
	}
	else if ( dynamic_cast< SurvivorBot * >( GetObserverTarget() ) && GetTeamNumber() == TEAM_SPECTATOR )
		m_isIdle = true;
}

void CTerrorGameRules::PrepareForChangeLevel( const char *targetMap )
{
	// S2B8140. Four independent live enumerations, not an all-in-one pass:
	// takeover changes actors/teams before action stopping and virtual saves.
	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	AbortCurrentCommentary();
	DevMsg( "Preparing player entities for changelevel\n" );
	g_SavedTerrorPlayerStates.ClearSavedPlayers();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = GetPlayerForTransitionPass( i );
		if ( player && !player->IsBot() ) player->SaveIdleStateForTransition();
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = GetPlayerForTransitionPass( i );
		if ( !player || player->IsBot() ) continue;
		player->SetRestoringFromTransition( true );
		player->TakeOverBot( false );
		player->SetRestoringFromTransition( false );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = GetPlayerForTransitionPass( i );
		if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		CBaseBackpackItem *backpack = dynamic_cast< CBaseBackpackItem * >( player->GetActiveWeapon() );
		if ( backpack && backpack->IsPerformingAction() ) backpack->StopAction();
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = GetPlayerForTransitionPass( i );
		if ( player ) player->SaveForTransition( targetMap );
	}
	// Native NextBotManager callback here is nullsub_34: no invented cleanup.
	TheDirector->OnLevelShutdown();
}

// S2B6C40. The caller owns the returned KV. It contains native serialization,
// not the separate destructive live Capture325E60/Apply31B7B0 state.
KeyValues *CTerrorPlayer::SaveTransitionData( const char *recordName )
{
	KeyValues *data = new KeyValues( recordName );
	data->SetInt( "userID", GetUserID() );
	data->SetInt( "reportid", GetStatsPlayerID() );
	data->SetInt( "restoreState", 0 );
	data->SetString( "name", GetPlayerName() );
	data->SetString( "idealName", IsFakeClient() ? GetPlayerName() : engine->GetClientConVarValue( entindex(), "name" ) );
	SaveVector( data, "pos", GetAbsOrigin() );
	SaveVector( data, "vel", GetAbsVelocity() );
	QAngle angles = GetAbsAngles();
	angles.y = EyeAngles().y;
	SaveVector( data, "angles", Vector( angles.x, angles.y, angles.z ) );
	data->SetInt( "positionFixedUp", 0 );
	data->SetInt( "duckedFlag", ( GetFlags() & FL_DUCKING ) != 0 );
	data->SetInt( "ducked", m_Local.m_bDucked );
	data->SetInt( "ducking", m_Local.m_bDucking );
	const bool restarting = TheDirector->IsInTransition() && g_bTerrorRestartAfterDeath;
	data->SetInt( "teamNumber", GetTeamNumber() );
	data->SetInt( "playerClass", restarting ? 0 : m_iClass.Get() );
	data->SetString( "modelName", restarting && !IsSurvivorTeam( GetTeamNumber() ) ? "" : STRING( GetModelName() ) );
	data->SetString( "zombieClass", GetZombieClassName( restarting ? 9 : GetZombieClass() ) );
	data->SetInt( "skin", m_nSkin );
	data->SetInt( "state", restarting ? STATE_OBSERVER_MODE : State_Get() );
	data->SetInt( "health", restarting ? 1 : GetHealth() );
	data->SetFloat( "healthBuffer", restarting ? 0.0f : GetHealthBuffer() );
	data->SetInt( "fumes", restarting ? 0 : m_isGoingToDie.Get() );
	data->SetInt( "flashlight", restarting ? 0 : FlashlightIsOn() != 0 );
	data->SetInt( "incapacitated", restarting ? 0 : IsIncapacitated() );
	data->SetInt( "character", GetSurvivorCharacter() );
	data->SetInt( "survivorSet", GetCurrentTerrorSurvivorSet() );
	data->SetInt( "hasUsedFlashlight", m_hasUsedFlashlightOutsideSafeArea );
	data->SetInt( "kills", FragCount() );
	data->SetInt( "deaths", DeathCount() );
	data->SetInt( "survivorKills", m_survivorKillScore );
	data->SetInt( "score", m_score );
	data->SetInt( "currentReviveCount", m_currentReviveCount );
	data->SetInt( "thirdStrike", m_bIsOnThirdStrike );
	data->SetInt( "missionReviveCount", m_totalReviveCount );
	data->SetInt( "wantsToPlay", m_wantsToPlay );
	for ( int id = 1; id < 56; ++id )
	{
		CTerrorWeapon *weapon = GetWeaponOfType( id );
		CBaseBackpackItem *backpack = dynamic_cast< CBaseBackpackItem * >( weapon );
		if ( weapon && ( weapon->GetSlot() != 3 || !backpack || !backpack->IsPerformingAction() ) ) SaveWeapon( data, id, weapon );
	}
	CWeaponCSBase *active = dynamic_cast< CWeaponCSBase * >( GetActiveWeapon() );
	data->SetString( "activeWeapon", WeaponIDToAlias( active ? active->GetWeaponID() : 0 ) );
	data->SetInt( "lastPrimaryWeaponId", m_lastPrimaryWeaponId );
	data->SetInt( "lastPrimaryWeaponAmmo", m_lastPrimaryWeaponAmmo );
	data->SetInt( "lastSecondaryWeaponId", m_lastSecondaryWeaponId );
	data->SetInt( "lastHadElites", m_lastHadElites );
	CWeaponCSBase *hidden = dynamic_cast< CWeaponCSBase * >( GetHiddenWeapon() );
	if ( hidden )
	{
		CTerrorMeleeWeapon *melee = hidden->GetWeaponID() == TERROR_WEAPON_MELEE ? dynamic_cast< CTerrorMeleeWeapon * >( hidden ) : NULL;
		data->SetString( "restoreSecondaryWeaponName", melee ? melee->GetMeleeInfo()->m_szName : hidden->GetClassname() );
	}
	for ( int i = 0; i < 9; ++i )
	{
		data->SetFloat( CFmtStr( "%s_timestamp", GetZombieClassName( i ) ), gpGlobals->curtime - m_zombieClassDeathTimestamp[i] );
		data->SetInt( CFmtStr( "%s_spawns", GetZombieClassName( i ) ), m_classSpawnCount[i] );
	}
	data->SetFloat( "minDeadDuration", m_minDeadDuration );
	data->SetFloat( "maxDeadDuration", m_maxDeadDuration );
	data->SetFloat( "totalDeadDuration", m_totalDeadDuration );
	data->SetInt( "deadCount", m_deadCount );
	CBaseAbility *ability = m_customAbility.Get();
	const float remaining = ability ? ability->GetActivationTimerRemaining() : 0.0f;
	const float duration = ability ? ability->GetRechargeDuration() : 0.0f;
	const bool retainTimer = remaining > 0.0f && remaining < 60.0f && duration > 0.0f && duration < 60.0f;
	data->SetFloat( "abilityTimerTime", retainTimer ? ability->GetActivationTimerRemaining() : 0.0f );
	data->SetFloat( "abilityTimerDuration", retainTimer ? ability->GetRechargeDuration() : 0.0f );
	for ( int i = 0; i < 9; ++i ) data->SetInt( CFmtStr( "%s_missionZombieKills", GetZombieClassName( i ) ), m_missionZombieKills[i] );
	for ( int i = 0; i < 149; ++i ) if ( m_missionAwardCounts[i] ) data->SetInt( CFmtStr( "award_%d", i ), m_missionAwardCounts[i] );
	data->SetInt( "tankTickets", m_totalSurvivorDamage );
	data->SetInt( "missionSurvivorDamage", m_missionSurvivorDamage );
	CTerrorExpresser *saveExpresser = static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() );
	if ( saveExpresser ) data->SetInt( "hasSaidScenarioJoin", saveExpresser->GetSurvivorSpeech()->HasJoinedScenario() );
	data->SetInt( "missionIDSpawnedOn", m_missionIDSpawnedOn );
	data->SetInt( "teamSpawnedOn", m_teamWhenMissionStarted );
	data->SetInt( "idle", m_isIdle );
	return data;
}
void CTerrorPlayer::SaveForTransition( const char *targetMap )
{
	// S2B7E20. Versus never saves AND never freezes through this wrapper.
	if ( TerrorGameRules()->IsVersusMode() ) return;
	if ( !IsBot() )
	{
		RemoveActiveBackpackForSave( this, false );
		UpdateMissionStats();
		if ( !g_bTerrorRestartAfterDeath && !CommandLine()->FindParm( "-makedevshots" ) ) SendTransitionMessage( this, "ShowStats" );
		KeyValues *data = SaveTransitionData( "player" );
		g_SavedTerrorPlayerStates.AddTransitionRecord( data, targetMap );
		data->deleteThis();
	}
	AddFlag( FL_FROZEN );
}
void SurvivorBot::SaveForTransition( const char *targetMap )
{
	// S2B7FE0 is a separate virtual override and a separate saved vector.
	RemoveActiveBackpackForSave( this, true );
	if ( GetTeamNumber() != TEAM_SURVIVOR ) return;
	UpdateMissionStats();
	KeyValues *data = SaveTransitionData( "survivorbot" );
	g_SavedTerrorPlayerStates.AddSurvivorBotRecord( data, targetMap );
	AddFlag( FL_FROZEN );
	data->deleteThis();
}
void CTerrorPlayer::RestoreTransitionData( KeyValues *data )
{
	// S2B8B60 first attempts the real associated bot. This successful early
	// return deliberately does NOT promote restoreState here.
	if ( TakeOverBot( true ) )
	{
		GetPlayerReporter().OnPlayerTeamChanged( this );
		FirePlayerEvent( "player_spawn", this );
		m_missionIDSpawnedOn = data->GetInt( "missionIDSpawnedOn", 0 );
		m_teamWhenMissionStarted = data->GetInt( "teamSpawnedOn", 0 );
		data->SetInt( "tookOverBot", 1 );
		if ( data->GetInt( "idle", 0 ) ) m_idleTimer.Start( 5.0f );
		return;
	}
	if ( !TheDirector->IsInTransition() )
	{
		static ConVarRef survivor_limit( "survivor_limit" ), z_max_player_zombies( "z_max_player_zombies" );
		if ( data->GetInt( "teamNumber", 0 ) == TEAM_SURVIVOR && TheDirector->CountPlayersOnTeam( TEAM_SURVIVOR, false ) >= survivor_limit.GetInt() )
			ChangeSavedTeam( data, TheDirector->CountPlayersOnTeam( TEAM_INFECTED, false ) >= z_max_player_zombies.GetInt() ? TEAM_SPECTATOR : TEAM_INFECTED );
		else if ( data->GetInt( "teamNumber", 0 ) == TEAM_INFECTED && TheDirector->CountPlayersOnTeam( TEAM_INFECTED, false ) >= z_max_player_zombies.GetInt() )
			ChangeSavedTeam( data, TheDirector->CountPlayersOnTeam( TEAM_SURVIVOR, false ) >= survivor_limit.GetInt() ? TEAM_SPECTATOR : TEAM_SURVIVOR );
	}
	if ( data->GetInt( "teamNumber", 0 ) == TEAM_INFECTED ) ChangeSavedTeam( data, TEAM_INFECTED );
	int character = GetTerrorSurvivorCharacterForModel( data->GetString( "ModelName", "" ) );
	const int currentSurvivorSet = GetCurrentTerrorSurvivorSet();
	if ( data->GetInt( "SurvivorSet", currentSurvivorSet ) != currentSurvivorSet )
	{
		static const int otherSet[] = { 4, 5, 7, 6, 0, 1, 3, 2 };
		if ( (unsigned int)character < ARRAYSIZE( otherSet ) ) character = otherSet[character];
		data->SetString( "ModelName", GetTerrorSurvivorModel( character ) );
	}
	ChangeTeam( data->GetInt( "teamNumber", 0 ) );
	m_iClass = data->GetInt( "playerClass", 0 );
	m_wantsToPlay = data->GetInt( "wantsToPlay", 0 ) != 0;
	const int savedState = data->GetInt( "state", 0 );
	if ( data->GetInt( "teamNumber", 0 ) == TEAM_SURVIVOR && savedState != STATE_ACTIVE )
	{
		if ( TheDirector->IsInTransition() ) { State_Transition( STATE_OBSERVER_MODE ); m_lifeState = LIFE_DEAD; }
		else CheckpointRespawn( TheTerrorNavMesh()->GetTransitionCheckpoint() );
	}
	else
	{
		if ( GetTeamNumber() == TEAM_SURVIVOR && savedState == STATE_ACTIVE ) { State_Transition( STATE_ACTIVE ); Spawn(); }
		else { State_Transition( STATE_OBSERVER_MODE ); m_lifeState = LIFE_DEAD; }
		if ( data->GetInt( "after_finale", 0 ) ) { data->SetInt( "health", 100 ); data->SetFloat( "healthBuffer", 0.0f ); }
		if ( !IsGhost() )
		{
			SetHealth( data->GetInt( "health", 0 ) );
			SetHealthBuffer( data->GetFloat( "healthBuffer", 0.0f ) );
			m_isIncapacitated = data->GetInt( "incapacitated", 0 ) != 0;
			m_isGoingToDie = data->GetInt( "fumes", 0 ) != 0;
		}
	}
	RestoreTransitionModel( data );
	if ( !g_bTerrorRestartAfterDeath && IsAlive() )
	{
		m_Local.m_bDucked = data->GetInt( "ducked", 0 ) != 0;
		m_Local.m_bDucking = data->GetInt( "ducking", 0 ) != 0;
		const bool ducked = data->GetInt( "duckedFlag", 0 ) != 0;
		if ( ducked ) AddFlag( FL_DUCKING );
		const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
		UTIL_SetSize( this, ducked ? vectors->m_vDuckHullMin : vectors->m_vHullMin, ducked ? vectors->m_vDuckHullMax : vectors->m_vHullMax );
		if ( !IsGhost() )
		{
			Vector offset = GetTerrorTransitionOffset();
			Vector position = LoadVector( data, "pos" );
			QAngle angles = LoadAngles( data );
			if ( !data->GetInt( "positionFixedUp", 0 ) )
			{
				position += offset;
				FixupTerrorTransitionPosition( position, angles );
				SaveVector( data, "pos", position );
				data->SetInt( "positionFixedUp", 1 );
			}
			if ( data->GetInt( "after_finale", 0 ) )
			{
				position = vec3_origin;
				if ( GetTeamNumber() == TEAM_SURVIVOR ) TerrorGameRules()->GetPlayerSpawnPosition( GetSurvivorCharacter(), &position, NULL, NULL );
			}
			position = GetSafeTransitionSpawn( this, position, ducked, data->GetInt( "teamNumber", 0 ), &angles );
			if ( IsSurvivorTeam( data->GetInt( "teamNumber", 0 ) ) ) SaveVector( data, "vel", vec3_origin );
			Vector velocity = LoadVector( data, "vel" );
			Teleport( &position, &angles, &velocity );
			SnapEyeAngles( angles );
		}
	}
	if ( !IsAlive() ) { CreateViewModel( 1 ); CreateViewModel( 0 ); }
	ResetFragCount();
	IncrementFragCount( data->GetInt( "kills", 0 ) );
	ResetDeathCount();
	IncrementDeathCount( data->GetInt( "deaths", 0 ) );
	m_survivorKillScore = data->GetInt( "survivorKills", 0 );
	m_currentReviveCount = data->GetInt( "currentReviveCount", 0 );
	m_totalReviveCount = data->GetInt( "missionReviveCount", 0 );
	m_bIsOnThirdStrike = data->GetInt( "thirdStrike", 0 ) != 0;
	// The count query observes this writer BEFORE inventory restoration. Do not
	// move it below weapons, nor treat this helper as a full-completion event.
	g_SavedTerrorPlayerStates.MarkRestoreHealthApplied( data );
	m_lastPrimaryWeaponId = data->GetInt( "lastPrimaryWeaponId", 0 );
	m_lastPrimaryWeaponAmmo = data->GetInt( "lastPrimaryWeaponAmmo", 0 );
	m_lastSecondaryWeaponId = data->GetInt( "lastSecondaryWeaponId", 1 );
	m_lastHadElites = data->GetInt( "lastHadElites", 0 ) != 0;
	if ( !g_bTerrorRestartAfterDeath && IsAlive() && !IsGhost() )
	{
		RemoveAllItems( false );
		for ( KeyValues *weapon = data->GetFirstSubKey(); weapon; weapon = weapon->GetNextKey() )
			if ( !Q_stricmp( weapon->GetName(), "weapon" ) ) RestoreWeapon( this, weapon->GetInt( "weaponID", 0 ), weapon );
		const char *secondary = data->GetString( "restoreSecondaryWeaponName", NULL );
		if ( secondary )
		{
			CBaseCombatWeapon *current = Weapon_GetSlot( 1 );
			if ( current ) { Weapon_Detach( current ); UTIL_Remove( current ); }
			GiveNamedItemInternal( secondary, 0, true, NULL );
		}
		CTerrorWeapon *active = GetWeaponOfType( AliasToWeaponID( data->GetString( "activeWeapon", "" ) ) );
		if ( active ) { Weapon_Switch( active, 0 ); if ( data->GetInt( "incapacitated", 0 ) ) active->Holster( NULL ); }
		if ( data->GetInt( "flashlight", 0 ) ) FlashlightTurnOn(); else FlashlightTurnOff();
	}
	m_hasUsedFlashlightOutsideSafeArea = data->GetInt( "hasUsedFlashlight", 0 ) != 0;
	for ( int i = 0; i < 9; ++i )
	{
		m_zombieClassDeathTimestamp[i] = gpGlobals->curtime - data->GetFloat( CFmtStr( "%s_timestamp", GetZombieClassName( i ) ), 0.0f );
		m_classSpawnCount.Set( i, data->GetInt( CFmtStr( "%s_spawns", GetZombieClassName( i ) ), 0 ) );
	}
	m_deathTimestamp = 0.0f;
	m_minDeadDuration = data->GetFloat( "minDeadDuration", 0.0f );
	m_maxDeadDuration = data->GetFloat( "maxDeadDuration", 0.0f );
	m_totalDeadDuration = data->GetFloat( "totalDeadDuration", 0.0f );
	m_deadCount = data->GetInt( "deadCount", 0 );
	if ( m_customAbility.Get() ) m_customAbility->SetRechargeTime( data->GetFloat( "abilityTimerTime", 0.0f ), data->GetFloat( "abilityTimerDuration", 0.0f ) );
	CNavArea *spawnArea = TheNavMesh->GetNearestNavArea( WorldSpaceCenter(), false, 1000.0f, true, true );
	if ( !spawnArea || !ToTerrorNavArea( spawnArea )->HasSpawnAttributes( TERROR_NAV_PLAYER_START ) )
	{
		for ( int i = 0; i < 9; ++i ) m_missionZombieKills.Set( i, data->GetInt( CFmtStr( "%s_missionZombieKills", GetZombieClassName( i ) ), 0 ) );
		for ( int i = 0; i < 149; ++i ) m_missionAwardCounts.Set( i, data->GetInt( CFmtStr( "award_%d", i ), 0 ) );
		m_totalSurvivorDamage = data->GetInt( "tankTickets", 0 );
		m_missionSurvivorDamage = data->GetInt( "missionSurvivorDamage", 0 );
	}
	CTerrorExpresser *restoreExpresser = static_cast< CTerrorExpresser * >( GetMultiplayerExpresser() );
	if ( restoreExpresser ) restoreExpresser->GetSurvivorSpeech()->SetHasJoinedScenario( data->GetInt( "hasSaidScenarioJoin", 0 ) != 0 );
	m_missionIDSpawnedOn = data->GetInt( "missionIDSpawnedOn", 0 );
	m_teamWhenMissionStarted = data->GetInt( "teamSpawnedOn", 0 );
	if ( m_isIncapacitated )
	{
		OnRevived();
	}
	SendTransitionMessage( this, "TransitionRestore" );
	FirePlayerEvent( "player_transitioned", this );
	if ( data->GetInt( "idle", 0 ) ) m_idleTimer.Start( 5.0f );
}
KeyValues *CTerrorPlayer::RestorePendingTransition()
{
	// S2BA070: a character match includes completed records, deliberately no
	// targetMap/state eligibility strengthening. The bot archive is second.
	if ( !m_bAwaitingTransitionRestore ) return NULL;
	KeyValues *data = NULL;
	if ( TheDirector->IsRestoringByCharacter() )
	{
		if ( IsSurvivorTeam( GetTeamNumber() ) ) data = g_SavedTerrorPlayerStates.FindPlayerRestore( GetUserID(), GetTeamNumber(), GetSurvivorCharacter(), true );
	}
	else data = g_SavedTerrorPlayerStates.FindPlayerRestore( GetUserID(), 0, 0, false );
	if ( data )
	{
		SetRestoringFromTransition( true );
		m_statsPlayerID = data->GetInt( "reportid", 0 );
		RestoreTransitionData( data );
		SetRestoringFromTransition( false );
		if ( GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsTempHealthOnly() ) { GiveHealthBuffer( 100.0f, (float)GetMaxHealth() ); SetReviveCount( 0 ); }
	}
	m_bAwaitingTransitionRestore = false;
	return data;
}
bool CTerrorPlayer::OnEndChangeLevel()
{
	// S2BA320: reset checkpoint statistics even if there are no saved records.
	ResetCheckpointStatistics();
	KeyValues *data = g_SavedTerrorPlayerStates.BeginPlayerRestore( GetUserID(), STRING( gpGlobals->mapname ) );
	if ( !data ) return false;
	m_bAwaitingTransitionRestore = true;
	InitialSpawn();
	TheDirector->MarkTransitionEntitiesRestored();
	data = RestorePendingTransition();
	if ( GetTeamNumber() == TEAM_SURVIVOR )
	{
		const bool tookOver = data && data->GetInt( "tookOverBot", 0 );
		if ( TheDirector->IsInTransition() && !IsAlive() && !tookOver )
		{
			CheckpointRespawn( TheTerrorNavMesh()->GetTransitionCheckpoint() );
			if ( data ) m_nSkin = data->GetInt( "skin", 0 );
		}
		TheDirector->UpdateHumanSurvivorCount();
	}
	return true;
}


ConVar sb_transition( "sb_transition", "1", 0x00000006,
	"If true, survivor bots will be used as placeholders for survivors who are still changing levels" );
static int s_transitionPlayerBeingCreated = -1; // native107AB030
int GetTransitionPlayerBeingCreated() { return s_transitionPlayerBeingCreated; }

void CDirector::RestoreSavedPlayers()
{
	// S2BA658 is the first state write, before even checking for saved records.
	// Drop only old-map registry membership, not actors, storage or pending KVs.
	CTransitioningPlayer::ResetTransitioningPlayerRegistry();
	for ( int i = 0; i < g_SavedTerrorPlayerStates.GetPlayerCount(); ++i )
	{
		KeyValues *data = g_SavedTerrorPlayerStates.GetPlayerData( i );
		if ( data->GetInt( "restoreState", 0 ) || Q_stricmp( data->GetString( "targetMap", "" ), STRING( gpGlobals->mapname ) ) ) continue;
		SurvivorCharacterType character = SurvivorCharacter_Unknown;
		SurvivorBot *bot = NULL;
		if ( data->GetInt( "teamNumber", 0 ) == TEAM_SURVIVOR )
		{
			character = (SurvivorCharacterType)data->GetInt( "character", 0 );
			if ( sb_transition.GetBool() && !AreSurvivorBotsDisabled() )
			{
				s_transitionPlayerBeingCreated = i;
				bot = NextBotCreatePlayerBot< SurvivorBot >( GetSurvivorBotName( character ) );
				if ( !bot ) s_transitionPlayerBeingCreated = -1;
			}
		}
		if ( bot )
		{
			bot->HandleCommand_JoinTeam( TEAM_SURVIVOR, character, true );
			MarkTransitionEntitiesRestored();
			char name[32];
			Q_strncpy( name, data->GetString( "idealName", "" ), sizeof( name ) );
			data->SetString( "idealName", GetSurvivorBotName( character ) );
			data->SetInt( "sb_transition", 1 );
			bot->RestoreTransitionData( data );
			data->SetString( "idealName", name );
			if ( IsInTransition() && !bot->IsAlive() )
			{
				bot->CheckpointRespawn( TheTerrorNavMesh()->GetTransitionCheckpoint() );
				bot->m_nSkin = data->GetInt( "skin", 0 );
			}
			bot->SetHumanSpectator( data->GetInt( "userID", 0 ), name );
			s_transitionPlayerBeingCreated = -1;
			continue;
		}
		// This is the original BaseFlex observer actor, never a fake player.
		CTransitioningPlayer *actor = static_cast< CTransitioningPlayer * >( CBaseEntity::Create( "info_transitioning_player", vec3_origin, vec3_angle ) );
		const int team = data->GetInt( "teamNumber", 0 );
		bool alive = data->GetInt( "state", 0 ) == STATE_ACTIVE;
		int health = data->GetInt( "health", 0 );
		if ( team == TEAM_SURVIVOR && !alive ) { alive = true; health = g_bTerrorRestartAfterDeath ? 100 : 50; }
		actor->Initialize( data->GetString( "idealName", "" ), team == TEAM_SURVIVOR ? data->GetInt( "score", 0 ) : 0,
			data->GetInt( "deaths", 0 ), data->GetInt( "userID", 0 ), team, health,
			(int)data->GetFloat( "healthBuffer", 0.0f ), alive, data->GetInt( "fumes", 0 ) != 0,
			data->GetInt( "incapacitated", 0 ) != 0, false, character );
		Vector position = GetTerrorTransitionOffset() + LoadVector( data, "pos" );
		QAngle angles = LoadAngles( data );
		FixupTerrorTransitionPosition( position, angles );
		if ( data->GetInt( "state", 0 ) != STATE_ACTIVE )
		{
			CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
			if ( checkpoint )
			{
				checkpoint->GetSpawnPosition( &position, &angles, NULL );
				SaveVector( data, "angles", Vector( angles.x, angles.y, angles.z ) );
			}
			else position = GetSafeTransitionSpawn( NULL, vec3_origin, false, team, &angles );
		}
		else position = GetSafeTransitionSpawn( NULL, position, false, team, &angles );
		SaveVector( data, "pos", position );
		data->SetInt( "positionFixedUp", 1 );
		// S23B9D0 is one downward hull trace, no retry/clear-space policy.
		trace_t trace;
		const CViewVectors *vectors = TerrorGameRules()->GetViewVectors();
		UTIL_TraceHull( position, position - Vector( 0, 0, 500 ), vectors->m_vHullMin, vectors->m_vHullMax,
			33570827u, NULL, COLLISION_GROUP_NONE, &trace );
		actor->SetAbsOrigin( trace.endpos );
		actor->SetAbsAngles( angles );
		int primary = 0, secondary = 1;
		bool molotov = false, grenade = false, kit = false, pills = false;
		for ( KeyValues *weapon = data->GetFirstSubKey(); weapon; weapon = weapon->GetNextKey() )
		{
			if ( Q_strcmp( weapon->GetName(), "weapon" ) ) continue;
			const int id = weapon->GetInt( "weaponID", 0 );
			switch ( id )
			{
			case 13: molotov = true; break;
			case 14: grenade = true; break;
			case 12: kit = true; break;
			case 15: pills = true; break;
			default:
				if ( weapon->GetInt( "weaponSlot", -1 ) == 0 ) primary = id;
				else if ( weapon->GetInt( "weaponSlot", -1 ) == 1 ) secondary = id;
				break;
			}
		}
		actor->SetWeapons( primary, secondary, molotov, grenade, kit, pills );
	}
}
void CDirector::RestoreSavedSurvivorBots()
{
	// S2B9CF0 does NOT filter by targetMap or restoreState.
	for ( int i = 0; i < g_SavedTerrorPlayerStates.GetSurvivorBotCount(); ++i )
	{
		KeyValues *data = g_SavedTerrorPlayerStates.GetSurvivorBotData( i );
		if ( GetGlobalTeam( TEAM_SURVIVOR )->GetNumPlayers() >= 4 || AreSurvivorBotsDisabled() ) continue;
		const SurvivorCharacterType character = (SurvivorCharacterType)data->GetInt( "character", 0 );
		SurvivorBot *bot = NextBotCreatePlayerBot< SurvivorBot >( GetSurvivorBotName( character ) );
		if ( !bot ) continue;
		if ( !g_bTerrorRestartAfterDeath ) MarkTransitionEntitiesRestored();
		bot->SetRestoringFromTransition( true );
		bot->m_statsPlayerID = data->GetInt( "reportid", 0 );
		bot->HandleCommand_JoinTeam( TEAM_SURVIVOR, character, false );
		bot->RestoreTransitionData( data );
		engine->SetFakeClientConVarValue( bot->edict(), "name", data->GetString( "idealName", "" ) );
		bot->SetRestoringFromTransition( false );
		if ( IsInTransition() && !bot->IsAlive() )
		{
			bot->CheckpointRespawn( TheTerrorNavMesh()->GetTransitionCheckpoint() );
			bot->m_nSkin = data->GetInt( "skin", 0 );
		}
		if ( bot->GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsTempHealthOnly() )
		{
			bot->GiveHealthBuffer( 100.0f, (float)bot->GetMaxHealth() );
			bot->SetReviveCount( 0 );
		}
	}
	g_SavedTerrorPlayerStates.ArchiveRestoredSurvivorBots();
}
bool CDirector::ProcessTransitionRestoration()
{
	// S264C90: pending human records and bot records have independent ownership.
	int all, restored;
	if ( g_SavedTerrorPlayerStates.GetSurvivorBotCount() )
	{
		g_SavedTerrorPlayerStates.GetRestoringPlayerCounts( TEAM_SURVIVOR, &all, &restored );
		bool restoreBots = all == 0 || restored > 0;
		if ( !restoreBots && TerrorGameRules()->IsPlayerControlledZombies() )
		{
			g_SavedTerrorPlayerStates.GetRestoringPlayerCounts( TEAM_INFECTED, &all, &restored );
			restoreBots = all == 0 || restored > 0;
		}
		if ( restoreBots ) RestoreSavedSurvivorBots();
	}
	if ( !IsChangingMapOrMission() || !m_bTransitionInProgress ) return false;
	int survivorAll, survivorRestored, infectedAll = 0, infectedRestored = 0;
	g_SavedTerrorPlayerStates.GetRestoringPlayerCounts( TEAM_SURVIVOR, &survivorAll, &survivorRestored );
	int staleSpectators = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsBot() ) continue;
		SurvivorBot *bot = dynamic_cast< SurvivorBot * >( player );
		if ( bot && bot->GetHumanSpectatorUserID() > 0 && !ToTerrorPlayer( UTIL_PlayerByUserId( bot->GetHumanSpectatorUserID() ) ) ) ++staleSpectators;
	}
	const int survivors = GetGlobalTeam( TEAM_SURVIVOR )->GetNumPlayers() - staleSpectators;
	int infected = 0;
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		g_SavedTerrorPlayerStates.GetRestoringPlayerCounts( TEAM_INFECTED, &infectedAll, &infectedRestored );
		infected = GetGlobalTeam( TEAM_INFECTED )->GetNumPlayers();
	}
	if ( ( survivorRestored == survivorAll && infectedRestored == infectedAll ) || ( survivors == 4 && infected == 4 ) )
	{
		m_bTransitionInProgress = false;
		m_bGamePlaying = true;
		if ( !m_bTransitionEntitiesRestored ) TheTerrorNavMesh()->OnRoundRestart();
		if ( !survivorRestored ) survivorRestored = 1;
		// Native264E4B reads the same DLLInit-owned107F7735 pressdemo cache.
		// It is NOT a single-player/dedicated/maxClients predicate.
		if ( g_bTerrorPressDemo )
			for ( int i = 0; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
				if ( player && player->edict() && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR ) player->SetHealth( 100 );
			}
	}
	if ( survivorRestored || infectedRestored )
	{
		if ( !TerrorGameRules()->IsPlayerControlledZombies() ) RestoreTerrorTransitionEntities();
		// S28F340 consumes a real RandomInt even though only non-null matters.
		CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
		int living = 0;
		for ( int i = 0; i < team->GetNumPlayers(); ++i ) if ( team->GetPlayer( i )->IsAlive() ) ++living;
		if ( living )
		{
			int selected = RandomInt( 0, living - 1 );
			for ( int i = 0; i < team->GetNumPlayers(); ++i )
			{
				CBasePlayer *player = team->GetPlayer( i );
				if ( player->IsAlive() && !selected-- ) { if ( player->IsPlayer() ) m_bTransitionEntitiesRestored = true; break; }
			}
		}
	}
	return true;
}
void CDirector::UpdateHumanSurvivorCount()
{
	// S25DFA0 counts LIVING humans, not the transition-ready writer25C1C0.
	m_nHumanSurvivorCount = CountPlayersOnTeam( TEAM_SURVIVOR, true );
	static ConVarRef director_debug( "director_debug" );
	if ( director_debug.GetBool() )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( 1 );
		if ( player ) ClientPrint( player, HUD_PRINTCENTER, CFmtStr( "SurvivorTeamCount = %d", m_nHumanSurvivorCount ) );
	}
}

int CDirector::CountPlayersOnTeam( int team, bool aliveOnly ) const
{
	// S2511E0 intentionally has no edict/IsConnected eligibility gate.
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && !player->IsBot() && player->GetTeamNumber() == team && ( !aliveOnly || player->IsAlive() ) ) ++count;
	}
	return count;
}
bool CDirector::AreSurvivorBotsDisabled() const
{
	// S25D430 ->275830: only an active cm_NoSurvivorBots>0 overrides
	// the real convar. A false/absent option does NOT override convartrue.
	if ( GetScriptedMode()->IsEnabled() && GetDirectorOptionInt( "cm_NoSurvivorBots", 0 ) > 0 ) return true;
	static ConVarRef director_no_survivor_bots( "director_no_survivor_bots" );
	return director_no_survivor_bots.GetBool();
}


#include "l4d2/player/terrorplayer_transition_console_server_callbacks.inc"
