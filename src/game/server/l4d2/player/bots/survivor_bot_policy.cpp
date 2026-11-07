//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/player/bots/survivor_coordinator_actions.h"
#include "l4d2/player/bots/survivor_interaction.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/weapons/prop_mounted_gun.h"
#include "l4d2/content/terror_model_precache.h"
#include "tier0/memdbgon.h"

extern bool AreAllSurvivorsInFinale();
ConVar sb_enforce_proximity_range( "sb_enforce_proximity_range", "1500", 0x00004004 );
ConVar sb_enforce_proximity_lookat_timeout( "sb_enforce_proximity_lookat_timeout", "10", 0x00004004 );
ConVar sb_escort( "sb_escort", "0", 0x00004004 );
static ConVar sb_unstick( "sb_unstick", "1", 0x00004004 );
extern ConVar sb_move;
extern ConVar sb_use_button_range;
extern ConVar z_exploding_outer_radius;
extern CBaseEntity *FindScavengeItem( SurvivorBot *me, float range );

// S38B9E0, not the independent S399F70 global distress predicate.
bool SurvivorBot::IsNearCheckpoint() const
{
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
	if ( !checkpoint || checkpoint->HasLockedDoor() ) return false;
	TerrorNavArea *exit = checkpoint->GetLargestSpawnArea();
	CNavArea *current = GetLastKnownArea();
	return exit && current && exit->GetFlowDistance() - ToTerrorNavArea( current )->GetFlowDistance() < 1500.0f;
}

// S38B4D0's ordered scan result is -1 unless at least one living human is
// in a checkpoint. Bots are rejected by IsFakeClient, not a made-up readiness.
static int HumanCheckpointState()
{
	int humans = 0, inCheckpoint = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !ENTINDEX( player->edict() ) || !player->IsPlayer() ||
			!player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || player->IsFakeClient() || !player->IsAlive() ) continue;
		++humans;
		TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
		if ( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) ) ++inCheckpoint;
	}
	return inCheckpoint && humans ? inCheckpoint >= humans : -1;
}

// S399F70 is the global roster predicate. It is NOT the independent local
// S300FD0 distress-response timer owned by CTerrorPlayer/Life.
bool AreSurvivorsInDistress()
{
	int checkpoint = HumanCheckpointState();
	if ( checkpoint == 1 ) return false;
	if ( TheDirector->GetTankCount() > 0 ) return true;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && ENTINDEX( player->edict() ) && player->IsPlayer() && player->IsConnected() &&
			player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() && ( player->IsDominated() || player->IsIT() ) ) return true;
	}
	return false;
}

bool SurvivorBot::IsRushing() const
{
	if ( GetTeamSituation().AreAllTeammatesBots() ) return false;
	return TheDirector->IsFinale() || TheDirector->GetScenario()->IsFirstPanicEvent() ||
		TerrorGameRules()->IsPlayerControlledZombies() || sb_escort.GetBool() ||
		( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "cm_ShouldEscortHumanPlayers", 0 ) != 0 );
}

bool SurvivorBot::ShouldMoveEvenIfRushing() const
{
	return HumanCheckpointState() != -1;
}

bool SurvivorBot::ShouldRespondToRescueVehicle() const
{
	return !IsRushing() || GetTeamSituation().AreAllTeammatesBots() || CountHumansNearRescueVehicle() > 0;
}

// S3994D0. This is the original guarded relocation, not a generic teleport
// recovery. A human merely looking our way delays it without a new LOS test.
void SurvivorBot::EnforceHumanProximity()
{
	if ( GetTeamNumber() == TEAM_L4D1_SURVIVOR ) return;
	if ( !CanPerformBackpackAction() ) { ClearImmobileStatus(); return; }
	if ( TheDirector->GetScenario()->AreSurvivorsInFinaleAreaBeforePanic() || AreAllSurvivorsInFinale() ||
		TerrorGameRules()->IsSurvivalMode() || GetTimeSinceEnemyInjury() < 1.5f || !IsImmobile() ||
		GetImmobileDuration() < 5.0f || TerrorGameRules()->IsPlayerControlledZombies() ||
		( TheDirector->GetScenario()->IsSacrificeFinale() && TheDirector->IsRescueVehicleReady() ) ) return;
	SurvivorTeamSituation &team = GetTeamSituation();
	CTerrorPlayer *human = team.GetNearestHuman();
	if ( !human ) return;
	if ( team.GetNearestHumanRange() <= sb_enforce_proximity_range.GetFloat() )
	{
		m_humanProximityLookAtTimer.Invalidate();
		return;
	}
	if ( !m_humanProximityLookAtTimer.HasStarted() ||
		m_humanProximityLookAtTimer.GetElapsedTime() < sb_enforce_proximity_lookat_timeout.GetFloat() )
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->edict() && ENTINDEX( player->edict() ) && player->IsPlayer() && player->IsConnected() &&
				player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() && !player->IsBot() && player->FInViewCone( this ) )
			{
				if ( !m_humanProximityLookAtTimer.HasStarted() ) m_humanProximityLookAtTimer.Start();
				return;
			}
		}
		m_humanProximityLookAtTimer.Invalidate();
	}
	DevMsg( "%3.2f: SurvivorBot %s was too far away and was forcibly relocated\n", gpGlobals->curtime, GetDebugIdentifier() );
	Vector forward;
	human->EyeVectors( &forward );
	forward.z = 0.0f;
	Vector origin = human->GetAbsOrigin() - forward * 10.0f;
	if ( human->IsHangingFromLedge() ) origin = human->m_hangStandPos;
	AddEffects( EF_NOINTERP );
	SetAbsOrigin( origin );
	GetIntentionInterface()->Reset();
}

class SurvivorUnclearedAreaCollector
{
public:
	SurvivorUnclearedAreaCollector() : farthest( NULL ), nearest( NULL ) {}
	bool operator()( CNavArea *base )
	{
		TerrorNavArea *area = ToTerrorNavArea( base );
		if ( !area->IsCleared( NULL ) && !area->HasSpawnAttributes( 0x1C00 ) ) areas.AddToTail( area );
		float flow = area->GetFlowDistance();
		if ( flow > -9999.0f )
		{
			if ( !farthest || flow > farthest->GetFlowDistance() ) farthest = area;
			if ( !nearest || nearest->GetFlowDistance() > flow ) nearest = area;
		}
		return true;
	}
	CUtlVector< CNavArea * > areas;
	TerrorNavArea *farthest;
	TerrorNavArea *nearest;
};

// S39CE80 appends, including duplicates from previous entered areas.
void SurvivorBot::OnNavAreaChanged( CNavArea *enteredArea, CNavArea *leftArea )
{
	SurvivorUnclearedAreaCollector collector;
	if ( enteredArea ) enteredArea->ForAllCompletelyVisibleAreas( collector );
	m_unclearedAreas.AddVectorToTail( collector.areas );
	m_farthestVisibleArea = collector.farthest;
	m_nearestVisibleArea = collector.nearest;
	UpdateEscapePath();
	BaseClass::OnNavAreaChanged( enteredArea, leftArea );
}

bool SurvivorBot::IsEnemy( const CBaseEntity *subject ) const
{
	if ( !subject ) return false;
	bool subjectSurvivor = subject->GetTeamNumber() == TEAM_SURVIVOR || subject->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	bool actorSurvivor = GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	return ( !subjectSurvivor || !actorSurvivor ) && INextBot::IsEnemy( subject );
}

const char *SurvivorBot::GetDebugIdentifier() const { return GetPlayerName(); }

int GetTerrorEntityZombieClass( CBaseEntity *entity )
{
	if ( entity->GetTeamNumber() != TEAM_INFECTED ) return 9;
	if ( entity->IsPlayer() ) return ToTerrorPlayer( entity )->GetZombieClass();
	Infected *infected = entity->MyInfectedPointer();
	return infected ? infected->GetZombieClass() : 9;
}

const char *GetSurvivorBotSubjectName( CBaseEntity *entity )
{
	if ( !entity ) return "";
	if ( entity->GetTeamNumber() == TEAM_INFECTED )
	{
		if ( entity->IsPlayer() || entity->MyInfectedPointer() ) return GetZombieClassName( GetTerrorEntityZombieClass( entity ) );
	}
	else if ( ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && entity->IsPlayer() )
		return ToTerrorPlayer( entity )->GetResponseCharacterName();
	return FClassnameIs( entity, "pipe_bomb_projectile" ) ? "PIPE_BOMB" : "";
}

// S38B910: preserve both same-area and equal-flow entindex tie breaks.
bool IsSurvivorBehind( CTerrorPlayer *first, CTerrorPlayer *second )
{
	TerrorNavArea *from = ToTerrorNavArea( first->GetLastKnownArea() );
	TerrorNavArea *to = ToTerrorNavArea( second->GetLastKnownArea() );
	if ( !from || !to ) return false;
	if ( from == to ) return first->entindex() < second->entindex();
	if ( to->GetFlowDistance() > from->GetFlowDistance() ) return true;
	return to->GetFlowDistance() == from->GetFlowDistance() && first->entindex() < second->entindex();
}

// S:262B30. The frame-owned ordered pipe-bomb vector is the only source.
CBaseEntity *CDirector::GetNearestPipeBomb( const Vector &position ) const
{
	CBaseEntity *nearest = NULL;
	float nearestRange = FLT_MAX;
	FOR_EACH_VEC( m_pipeBombs, i )
	{
		CBaseEntity *bomb = m_pipeBombs[i].Get();
		if ( !bomb ) continue;
		float range = ( bomb->GetAbsOrigin() - position ).LengthSqr();
		if ( nearestRange > range )
		{
			nearestRange = range;
			nearest = bomb;
		}
	}
	return nearest;
}

// S:38EB50. This is the danger radius, not the dangerous-threat predicate.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
float SurvivorBot::GetThreatDangerRange( CBaseEntity *threat ) const
{
	if ( !threat ) return 0.0f;
	switch ( GetTerrorEntityZombieClass( threat ) )
	{
	case 0:
		{
			CBaseCombatWeapon *active = GetActiveWeapon();
			if ( ( !active || !active->m_bInReload ) &&
				m_vision->GetKnownCount( TEAM_INFECTED, 500.0f ) < 5 ) return 0.0f;
			return 125.0f;
		}
	case 1: case 3: case 4: case 5: case 6:
		return !ToTerrorPlayer( threat )->m_stunTimer.IsElapsed() ? 0.0f : 125.0f;
	case 2:
		{
			if ( !m_vision->IsLineOfSightClearToEntity( threat ) ) return 450.0f;
			CTerrorPlayer *boomer = ToTerrorPlayer( threat );
			CBaseAbility *ability = boomer->m_customAbility.Get();
			if ( ability && boomer->m_stunTimer.IsElapsed() && ability->IsReadyToActivate() )
			{
				if ( !boomer->IsNearShoveLimit() && IsRangeLessThan( boomer, 100.0f ) ) return 0.0f;
				return 500.0f;
			}
			return z_exploding_outer_radius.GetFloat() * 1.1f;
		}
	case 7: return 300.0f;
	case 8: return 450.0f;
	default: return FClassnameIs( threat, "pipe_bomb_projectile" ) ? 425.0f : 0.0f;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:39CA40. Preserve the greedy cycle marker, the 2000-unit pre-step limit,
// then the distinct 1000-unit bounded search only when no greedy step worked.
void SurvivorBot::UpdateEscapePath()
{
	if ( m_escapePathTimer.GetRemainingTime() > 0.0f ||
		m_locomotor->IsAscendingOrDescendingLadder() || m_locomotor->IsClimbingOrJumping() ) return;
	m_escapePathTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	TerrorNavArea *start = ToTerrorNavArea( GetLastKnownArea() );
	if ( !start )
	{
		m_escapePath.Invalidate();
		if ( IsDebugging( NEXTBOT_PATH ) )
			DevMsg( "%3.2f: %s: UpdateEscapePath failed: No last known area.\n", gpGlobals->curtime, GetDebugIdentifier() );
		return;
	}
	if ( m_escapePath.IsValid() && m_escapePath.FirstSegment() && m_escapePath.FirstSegment()->area == start ) return;
	if ( IsDebugging( NEXTBOT_PATH ) )
		DevMsg( "%3.2f: %s: UpdateEscapePath REPATH.\n", gpGlobals->curtime, GetDebugIdentifier() );
	CNavArea::MakeNewMarker();
	start->Mark();
	TerrorNavArea *goal = start;
	float travel = 0.0f;
	for ( TerrorNavArea *next = start->GetNextHighestFlowConnectedArea( NULL ); next;
		next = goal->GetNextHighestFlowConnectedArea( NULL ) )
	{
		if ( next->IsMarked() || travel >= 2000.0f || !m_locomotor->IsAreaTraversable( next ) ) break;
		next->Mark();
		travel += ( next->GetCenter() - goal->GetCenter() ).Length();
		goal = next;
	}
	if ( goal == start )
	{
		struct EscapeGoal
		{
			SurvivorLocomotion *locomotor;
			TerrorNavArea *area;
			float flow;
			bool operator()( TerrorNavArea *candidate )
			{
				if ( !locomotor->IsAreaTraversable( candidate ) ) return false;
				if ( candidate->GetFlowDistance() > flow )
				{
					flow = candidate->GetFlowDistance();
					area = candidate;
				}
				return true;
			}
		} find = { m_locomotor, NULL, 0.0f };
		CollectSurroundingTerrorAreas( start, GetAbsOrigin(), find, 1000.0f, 0, GetTeamNumber() );
		goal = find.area ? find.area : start;
	}
	SurvivorBotPathCost cost( this );
	if ( !m_escapePath.Compute( this, goal->GetCenter(), cost, 0.0f ) )
	{
		if ( IsDebugging( NEXTBOT_PATH ) )
			DevMsg( "%3.2f: %s: UpdateEscapePath failed: Pathfind failed.\n", gpGlobals->curtime, GetDebugIdentifier() );
		CTerrorPlayer *human = m_teamSituation.GetLeadingHuman();
		if ( human )
		{
			if ( !m_escapePath.Compute( this, human->GetAbsOrigin(), cost, 0.0f ) )
			{
				m_escapePath.Invalidate();
				if ( IsDebugging( NEXTBOT_PATH ) )
					DevMsg( "%3.2f: %s: UpdateEscapePath failed: Pathfind to leader human also failed.\n", gpGlobals->curtime, GetDebugIdentifier() );
			}
		}
		else if ( IsDebugging( NEXTBOT_PATH ) )
			DevMsg( "%3.2f: %s: UpdateEscapePath failed: Pathfind to failed, but using it anyway.\n", gpGlobals->curtime, GetDebugIdentifier() );
	}
	if ( sb_debug_team_avoidance.GetBool() )
	{
		NDebugOverlay::Line( GetAbsOrigin(), goal->GetCenter(), 0, 255, 0, true, m_escapePathTimer.GetRemainingTime() );
		goal->Draw();
	}
}

static bool IsSurvivorUnstickPointClear( const Vector &position, CBaseEntity *ignore )
{
	trace_t trace;
	UTIL_TraceHull( position, position, VEC_HULL_MIN, VEC_HULL_MAX, 33571339u, ignore, COLLISION_GROUP_NONE, &trace );
	return !trace.startsolid && !trace.allsolid;
}

// S:38CC90. These are the original narrow relocation attempts, not a
// general-purpose rescue teleport or a replacement for locomotion recovery.
void SurvivorBot::ResolveStuckSituation( PathFollower *path )
{
	if ( !sb_unstick.GetBool() ) return;
	if ( !CanPerformBackpackAction() || !sb_move.GetBool() )
	{
		PressJumpButton();
		return;
	}
	if ( GetTimeSinceEnemyInjury() < 1.0f )
	{
		DevMsg( "%3.2f: ResolveStuckSituation: SurvivorBot( %s ) at at ( %3.2f, %3.2f, %3.2f ) unable to teleport due to recent damage.\n",
			gpGlobals->curtime, GetDebugIdentifier(), GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
		return;
	}
	m_resetIntention = true;
	if ( TerrorGameRules()->IsSurvivalMode() || ( TheDirector->GetScenario()->IsSacrificeFinale() &&
		TheDirector->IsRescueVehicleReady() ) ) return;
	if ( path->IsValid() )
	{
		const Path::Segment *goal = path->GetCurrentGoal();
		for ( int i = 0; goal && i < 3; ++i, goal = path->NextSegment( goal ) )
		{
			if ( IsRangeLessThan( goal->pos, 50.0f ) ) continue;
			Vector position = goal->pos + Vector( 0, 0, 18.0f );
			if ( !IsSurvivorUnstickPointClear( position, this ) ) continue;
			Teleport( &position, NULL, NULL );
			m_locomotor->Reset();
			m_locomotor->ClearStuckStatus( "Teleported farther along the path" );
			DevMsg( "%3.2f: ERROR: SurvivorBot( %s ) stuck here ( %3.2f, %3.2f, %3.2f ) - forced to next path goal position\n",
				gpGlobals->curtime, GetDebugIdentifier(), GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
			return;
		}
	}
	CUtlVectorFixedGrowable< CHandle< CTerrorPlayer >, 4 > friends;
	for ( int i = 0; i < m_teamSituation.GetTeammateCount(); ++i ) friends.AddToTail( m_teamSituation.GetTeammate( i ) );
	for ( int i = 0; i < friends.Count() - 1; ++i )
	{
		int j = RandomInt( i, friends.Count() - 1 );
		V_swap( friends[i], friends[j] );
	}
	FOR_EACH_VEC( friends, i )
	{
		CTerrorPlayer *friendPlayer = friends[i].Get();
		if ( !IsSurvivorUnstickPointClear( friendPlayer->GetAbsOrigin(), friendPlayer ) ) continue;
		Vector forward;
		friendPlayer->EyeVectors( &forward );
		forward.z = 0.0f;
		Vector position = friendPlayer->GetAbsOrigin() - forward * 10.0f;
		if ( friendPlayer->m_isHangingFromLedge ) position = friendPlayer->m_hangStandPos;
		AddEffects( EF_NOINTERP );
		SetAbsOrigin( position );
		m_locomotor->Reset();
		m_locomotor->ClearStuckStatus( "Teleported to nearby friend's spot" );
		DevMsg( "%3.2f: ERROR: SurvivorBot( %s ) stuck here ( %3.2f, %3.2f, %3.2f ) - forced to nearby friend position\n",
			gpGlobals->curtime, GetDebugIdentifier(), GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
		return;
	}
	Warning( "%3.2f: ResolveStuckSituation ERROR: Cannot resolve SurvivorBot( %s ) stuck situation at ( %3.2f, %3.2f, %3.2f )\n",
		gpGlobals->curtime, GetDebugIdentifier(), GetAbsOrigin().x, GetAbsOrigin().y, GetAbsOrigin().z );
}

// S:3978D0: no regular scavenging cooldown, checkpoint exception, or
// leader-separation policy belongs to the L4D1-specific selection.
ActionResult< SurvivorBot > SurvivorBot::TryScavengeForL4D1Survivor()
{
	if ( !GetLastKnownArea() ) return ActionResult< SurvivorBot >();
	CBaseEntity *item = FindScavengeItem( this, 1500.0f );
	if ( !item ) return ActionResult< SurvivorBot >();
	TerrorNavArea *area = TheDirector->GetItemManager()->GetItemNavArea( item );
	if ( !area ) return ActionResult< SurvivorBot >();
	float flow = ToTerrorNavArea( GetLastKnownArea() )->GetFlowDistance();
	if ( area->GetFlowDistance() > flow + 250.0f || flow - 750.0f > area->GetFlowDistance() )
		return ActionResult< SurvivorBot >();
	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( item->MyCombatWeaponPointer() );
	if ( weapon && weapon->GetTerrorWpnData().m_WeaponType == TERROR_WEAPONTYPE_HEALING )
		return ActionResult< SurvivorBot >();
	SurvivorCollectObject *action = new SurvivorCollectObject( item );
	if ( action->ShouldGiveUp( this ) )
	{
		delete action;
		return ActionResult< SurvivorBot >();
	}
	return ActionResult< SurvivorBot >( SUSPEND_FOR, action, "Scavenging something" );
}

// S:39A790. Native occupancy3DA850 is GetUser(), not a permissive mounted-gun
// readiness query. Both waits use the single actor-owned minigun timer.
ActionResult< SurvivorBot > SurvivorBot::TryUseMinigun()
{
	if ( !IsSafeForCombatAction() || m_minigunTimer.GetRemainingTime() > 0.0f )
		return ActionResult< SurvivorBot >();
	m_minigunTimer.Start( RandomFloat( 3.0f, 5.0f ) );
	CPropMountedGun *gun = dynamic_cast< CPropMountedGun * >(
		gEntList.FindEntityByClassnameNearest( "prop_mounted_machine_gun", GetAbsOrigin(), sb_use_button_range.GetFloat() ) );
	if ( !gun || !IsReachable( gun ) || gun->GetUser() ) return ActionResult< SurvivorBot >();
	m_minigunTimer.Start( RandomFloat( 20.0f, 30.0f ) );
	return ActionResult< SurvivorBot >( SUSPEND_FOR, new SurvivorApproachMinigun( gun ), "Trying to approach and use a minigun" );
}

// S3054F0 owns the unknown gate/remap; S303E20 is its first-match functor loop.
CTerrorPlayer *FindSurvivorByCharacter( SurvivorCharacterType character )
{
	if ( character == SurvivorCharacter_Unknown )
		return NULL;
	const int mappedCharacter = MapTerrorSurvivorCharacter( character );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->edict() || base->entindex() == 0 || !base->IsPlayer() || !base->IsConnected() )
			continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		const int team = player->GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			player->GetSurvivorCharacter() == mappedCharacter )
			return player;
	}
	return NULL;
}

// S:28FF20/260260: this is the shared player selector, not a NAV lookup or
// button reachability test. Keep its two independently controlled exclusions.
CTerrorPlayer *FindClosestSurvivorPlayer( const Vector &position, bool includeImmobile, bool excludeRescueVehicle )
{
	// S28FF20 snapshots the input before the candidate callbacks.
	const Vector queryPosition = position;
	CTerrorPlayer *closest = NULL;
	float rangeSquared = 100000000.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->edict() || base->entindex() == 0 || !base->IsPlayer() ||
			!static_cast< const CBasePlayer * >( base )->IsConnected() )
			continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		const bool immobile = player->IsIncapacitated() || player->IsBeingPounced() ||
			player->IsTongueVictim() || player->IsPummelVictim() || player->IsJockeyVictim();
		if ( !includeImmobile && immobile )
			continue;
		if ( excludeRescueVehicle )
		{
			TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
			if ( area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
				continue;
		}
		const Vector delta = player->WorldSpaceCenter() - queryPosition;
		const float distance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
		if ( rangeSquared > distance )
		{
			rangeSquared = distance;
			closest = player;
		}
	}
	return closest && closest->IsPlayer() ? closest : NULL;
}
