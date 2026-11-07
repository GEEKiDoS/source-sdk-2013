//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "tier0/memdbgon.h"

extern ConVar sb_neighbor_range;
extern ConVar sb_threat_exposure_walk;
ConVar sb_narrow_corridor_width( "sb_narrow_corridor_width", "100", 0x00004004 );

// Original server 1038BAD0. The final mode query is made even though its
// result does not change the original's 150-unit default.
float GetSurvivorRushingRange()
{
	if ( TerrorGameRules()->IsVersusMode() )
		return 180.0f;
	if ( TerrorGameRules()->IsScavengeMode() )
		return 120.0f;
	TerrorGameRules()->IsPlayerControlledZombies();
	return 150.0f;
}

// Original server 1038D7F0/1038C910. Deliberately includes dead human
// survivors, and disconnect-in-progress is still connected. The 480-unit
// near-vehicle test is strict and is not limited to RESCUE_VEHICLE NAV areas.
int CountHumansNearRescueVehicle()
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || ENTINDEX( player->edict() ) == 0 || !player->IsPlayer() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || player->IsBot() )
			continue;
		TerrorNavArea *area = static_cast< TerrorNavArea * >( player->GetLastKnownArea() );
		if ( area && ( area->GetSpawnAttributes() & 0x8000 ) != 0 )
		{
			++count;
			continue;
		}
		TerrorNavArea *rescue = TheTerrorNavMesh()->GetRescueVehicleArea();
		if ( rescue && ( player->GetAbsOrigin() - rescue->GetCenter() ).LengthSqr() < 230400.0f )
			++count;
	}
	return count;
}

// Original server 103AA7D0. Unlike INextBot's entity-range overload, this
// compares the two absolute origins, not their collision nearest points.
bool SurvivorBot::IsThreatWithinDangerRange( CBaseEntity *threat, float range ) const
{
	return range * range > ( GetAbsOrigin() - threat->GetAbsOrigin() ).LengthSqr();
}

// Original server 1038EAA0. Current visibility/range and the recent-friend
// interval are distinct native queries; never substitute the latter here.
bool SurvivorBot::IsFriendNearby( CTerrorPlayer *friendPlayer ) const
{
	return INextBot::IsRangeLessThan( friendPlayer, sb_neighbor_range.GetFloat() ) &&
		( !friendPlayer->IsInCombat() || GetIntentionInterface()->GetPrimaryKnownThreat() != NULL ) &&
		IsLineOfSightClear( friendPlayer, CBaseCombatCharacter::IGNORE_ACTORS );
}

// Original server 1038ED10. Walking (1) and running (2) are requested through
// player input by the action; this query does not write position or velocity.
int SurvivorBot::GetDesiredPace() const
{
	if ( NextBotPlayerWalk.GetBool() )
		return 1;
	SurvivorTeamSituation &team = GetTeamSituation();
	if ( GetIntentionInterface()->ShouldHurry( this ) == ANSWER_YES )
		return 2;
	int pace = 2;
	if ( !IsStill( true ) && DotProduct( GetLocomotionInterface()->GetMotionVector(), GetBodyInterface()->GetViewVector() ) < 0.0f )
		pace = 1;
	if ( IsSelf( team.GetLeader() ) )
	{
		float totalArea = 0.0f;
		FOR_EACH_VEC( m_unclearedAreas, i )
		{
			totalArea += m_unclearedAreas[i]->GetSizeY() * m_unclearedAreas[i]->GetSizeX();
		}
		if ( totalArea > sb_threat_exposure_walk.GetFloat() )
			pace = 1;
	}
	CTerrorPlayer *human = team.GetLeadingHuman();
	if ( human && !IsSelf( human ) )
	{
		const float range = team.GetRangeToFriend( human );
		if ( range > sb_separation_danger_min_range.GetFloat() )
			return 2;
		if ( range < sb_separation_range.GetFloat() )
		{
			if ( human->IsRunning() && human->GetTimeSinceStartedRunning() > GetVisionInterface()->GetMinRecognizeTime() )
				return 2;
			if ( human->IsSneaking() )
				return 1;
		}
	}
	return pace;
}
