//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss_zombie_player_bot.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/NextBotUtil.h"
#include "tier1/fmtstr.h"

#include "tier0/memdbgon.h"

extern bool IsInCommentaryMode();
extern ConVar nb_blind;
extern ConVar NextBotPlayerStop;
static ConVar z_boss_crouch( "z_boss_crouch", "0", 0x00004004, "Forces boss zombie bots to crouch" );
static ConVar z_look_at_local_player( "z_look_at_local_player", "0", 0x00004004, "For demo purposes" );

static bool IsMobileBossVictim( CTerrorPlayer *player )
{
	return !player->IsIncapacitated() && !player->IsBeingPounced() && !player->IsTongueVictim()
		&& !player->IsPummelVictim() && !player->IsJockeyVictim();
}

static bool IsBossVictim( CTerrorPlayer *player )
{
	return player && player->IsConnected() && player->IsAlive() && player->GetTeamNumber() == TEAM_SURVIVOR;
}

static bool AreAllHumanSurvivorsUnavailable()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( IsBossVictim( player ) && !player->IsFakeClient() && !player->IsIncapacitated() )
			return false;
	}
	return true;
}

// S260260/28FF20/28FFB0. Same selector, with the native optional filters.
CTerrorPlayer *FindClosestBossSurvivor( const Vector &position,
	bool includeImmobile, bool ignoreRescueVehicle, CBaseEntity *ignore )
{
	CTerrorPlayer *closest = NULL;
	float closestRangeSquared = 1.0e8f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || player->entindex() == 0 ||
			!IsBossVictim( player ) || player == ignore ||
			( !includeImmobile && !IsMobileBossVictim( player ) ) )
			continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( ignoreRescueVehicle && area &&
			ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
			continue;
		const float rangeSquared = ( player->WorldSpaceCenter() - position ).LengthSqr();
		if ( rangeSquared < closestRangeSquared )
		{
			closest = player;
			closestRangeSquared = rangeSquared;
		}
	}
	return closest;
}

BossZombiePlayerBot::BossZombiePlayerBot()
{
	// S:0x4253D0 allocates no components. Each concrete infected owns its
	// genuine intention, locomotion, PlayerBody and ZombieBotVision.
	m_humanSpectatorUserID = 0;
	INextBot::Reset();
}

CBaseCombatCharacter *BossZombiePlayerBot::GetEntity() const
{
	return const_cast< BossZombiePlayerBot * >( this );
}

void BossZombiePlayerBot::Spawn()
{
	BaseClass::Spawn();
	ClearHumanSpectator();
	m_victimSelectionTimer.Invalidate();
	GetBodyInterface()->SetArousal( IBody::INTENSE );
}

void BossZombiePlayerBot::Update()
{
	if ( z_look_at_local_player.GetBool() )
	{
		CBasePlayer *host = UTIL_GetListenServerHost();
		if ( host )
			GetBodyInterface()->AimHeadTowards( host, IBody::CRITICAL, 1.0f, NULL,
				"Watching listen server host", true, 0.25f );
	}
	if ( IsAlive() && !NextBotPlayerStop.GetBool() )
		INextBot::Update();
}

void BossZombiePlayerBot::PhysicsSimulate()
{
	if ( m_humanSpectatorUserID > 0 )
	{
		CBasePlayer *human = UTIL_PlayerByUserId( m_humanSpectatorUserID );
		if ( !human || !human->IsPlayer() || human->GetTeamNumber() != TEAM_SPECTATOR )
			ClearHumanSpectator();
	}
	INextBot *selected = TheNextBots().GetSelected();
	if ( !selected || selected->IsSelf( this ) )
	{
		if ( z_boss_crouch.GetBool() )
			PressCrouchButton();
	}
	BaseClass::PhysicsSimulate();
}

void BossZombiePlayerBot::ChangeTeam( int team )
{
	BaseClass::ChangeTeam( team );
	if ( GetTeamNumber() != TEAM_UNASSIGNED && GetTeamNumber() != TEAM_INFECTED
		&& ( team == TEAM_INFECTED || team == TEAM_UNASSIGNED ) )
		engine->ServerCommand( CFmtStr( "kickid %d infected team is full\n", GetUserID() ) );
}

void BossZombiePlayerBot::OnPreThinkObserverMode()
{
	engine->ServerCommand( CFmtStr( "kickid %d death anim finished\n", GetUserID() ) );
}

void BossZombiePlayerBot::OnLeaveActive()
{
	BaseClass::OnLeaveActive();
	if ( m_humanSpectatorUserID > 0 )
	{
		CTerrorPlayer *human = ToTerrorPlayer( UTIL_PlayerByUserId( m_humanSpectatorUserID ) );
		if ( human && human->IsPlayer() )
			human->HandleCommand_JoinTeam( TEAM_INFECTED, 8, false );
		ClearHumanSpectator();
	}
}

bool BossZombiePlayerBot::SetHumanSpectator( CTerrorPlayer *human )
{
	if ( m_humanSpectatorUserID > 0 )
		return false;
	m_humanSpectatorUserID = engine->GetPlayerUserId( human->edict() );
	return true;
}

void BossZombiePlayerBot::ClearHumanSpectator()
{
	m_humanSpectatorUserID = 0;
}

bool BossZombiePlayerBot::ShouldTouch( const CBaseEntity *object ) const
{
	return !IsInCommentaryMode() || !object->IsPlayer() || static_cast< const CBasePlayer * >( object )->IsFakeClient();
}

bool BossZombiePlayerBot::IsDebugFilterMatch( const char *name ) const
{
	if ( !Q_strnicmp( name, GetZombieClassName( GetZombieClass() ), Q_strlen( name ) ) )
		return true;
	return INextBot::IsDebugFilterMatch( name );
}

const char *BossZombiePlayerBot::GetDebugIdentifier() const
{
	static char identifier[256];
	V_snprintf( identifier, sizeof( identifier ), "%s(#%d)", GetZombieClassName( GetZombieClass() ), entindex() );
	return identifier;
}

bool BossZombiePlayerBot::IsAggressive() const
{
	return g_TerrorModes.IsPlayerControlledZombies() || TheDirector->GetSurvivalMode()->HasStarted()
		|| ( TheDirector->GetScriptedMode()->IsEnabled()
			&& TheDirector->GetDirectorOptionInt( "cm_AggressiveSpecials", 0 ) != 0 );
}

bool BossZombiePlayerBot::ShouldRetreatToCover() const
{
	return TheDirector->GetScriptedMode()->IsEnabled()
		&& TheDirector->GetDirectorOptionInt( "cm_SpecialsRetreatToCover", 0 ) != 0;
}

CTerrorPlayer *BossZombiePlayerBot::SelectVictim( CTerrorPlayer *current, int flags, CTerrorPlayer *ignore )
{
	if ( ( GetSenseFlags() & 1 ) || nb_blind.GetBool() || IsCommentaryStatue() )
		return NULL;
	if ( m_victimSelectionTimer.HasStarted() && gpGlobals->curtime < m_victimSelectionTimer.m_timestamp )
		return current;
	m_victimSelectionTimer.Start( 0.3f );

	const bool allHumansUnavailable = AreAllHumanSurvivorsUnavailable();
	const bool ignoreImmobile = allHumansUnavailable || ( !( flags & 1 ) && ( flags & 4 ) );
	const Vector &position = INextBot::GetPosition();
	CTerrorPlayer *selected = NULL;
	float bestRangeSquared = 1.0e8f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !IsBossVictim( player ) || player == ignore || ( IsInCommentaryMode() && !player->IsFakeClient() )
			|| ( ignoreImmobile && !IsMobileBossVictim( player ) ) )
			continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( area && ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) )
			continue;
		const Vector target = player->WorldSpaceCenter();
		float rangeSquared;
		if ( flags & 1 )
		{
			float range = ( target - position ).Length();
			if ( player->IsKnockedDown() )
				range *= 0.5f;
			rangeSquared = range * range;
		}
		else
		{
			rangeSquared = ( target - position ).LengthSqr();
		}
		if ( rangeSquared >= bestRangeSquared )
			continue;
		trace_t trace;
		NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
		UTIL_TraceLine( position, target, MASK_BLOCKLOS_AND_NPCS | CONTENTS_IGNORE_NODRAW_OPAQUE, &filter, &trace );
		if ( trace.fraction >= 1.0f && !trace.startsolid )
		{
			selected = player;
			bestRangeSquared = rangeSquared;
		}
	}

	if ( !selected )
	{
		if ( IsInCommentaryMode() || ( flags & 2 ) )
			return NULL;
		selected = FindClosestBossSurvivor( position, false );
	}
	if ( !selected || IsRangeGreaterThan( selected, 500.0f ) )
	{
		if ( !( flags & 4 ) )
		{
			CTerrorPlayer *closest = FindClosestBossSurvivor( position, true );
			if ( closest && ( closest->IsFakeClient() || !allHumansUnavailable ) )
				selected = closest;
		}
	}
	if ( !selected )
		return NULL;
	if ( !current || !current->IsAlive() )
		return selected;
	if ( current->IsStaggeringOrPending() || current == selected )
		return current;

	if ( !IsMobileBossVictim( current ) && IsMobileBossVictim( selected ) && selected->IsPlayer() )
	{
		if ( IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s switching to mobile victim\n", gpGlobals->curtime, GetDebugIdentifier() );
		return selected;
	}
	if ( GetDistanceBetween( current ) - GetDistanceBetween( selected ) > 200.0f
		&& GetVisionInterface()->IsAbleToSee( selected, IVision::DISREGARD_FOV ) )
	{
		if ( IsDebugging( NEXTBOT_BEHAVIOR ) )
			DevMsg( "%3.2f: %s switching to closer victim\n", gpGlobals->curtime, GetDebugIdentifier() );
		return selected;
	}
	if ( !GetVisionInterface()->IsAbleToSee( selected, IVision::DISREGARD_FOV )
		|| GetVisionInterface()->IsAbleToSee( current, IVision::DISREGARD_FOV ) )
		return current;
	if ( IsDebugging( NEXTBOT_BEHAVIOR ) )
		DevMsg( "%3.2f: %s switching to directly visible victim\n", gpGlobals->curtime, GetDebugIdentifier() );
	return selected;
}

bool BossZombiePlayerBot::IsVictimFarAhead( CTerrorPlayer *victim ) const
{
	if ( !victim )
		return false;
	const CNavArea *myArea = GetLastKnownArea();
	const CNavArea *victimArea = victim->GetLastKnownArea();
	const float myFlow = myArea ? ToTerrorNavArea( myArea )->GetFlowDistance() : -9999.0f;
	const float victimFlow = victimArea ? ToTerrorNavArea( victimArea )->GetFlowDistance() : -9999.0f;
	return victimFlow > myFlow + 500.0f;
}
