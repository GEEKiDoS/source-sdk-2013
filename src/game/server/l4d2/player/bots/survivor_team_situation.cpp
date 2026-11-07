//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_team_situation.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "nav_pathfind.h"
#include "NextBot/NextBotVisionInterface.h"
#include "tier0/memdbgon.h"

extern ConVar sb_neighbor_range;
ConVar sb_debug_buddy( "sb_debug_buddy", "0", 0x00004004 );
ConVar sb_min_orphan_time_to_cover( "sb_min_orphan_time_to_cover", "1", 0x00004004 );

static float SurvivorFlow( CTerrorPlayer *player )
{
	CNavArea *area = player->GetLastKnownArea();
	return area ? ToTerrorNavArea( area )->GetFlowDistance() : -9999.0f;
}

SurvivorTeamSituation::SurvivorTeamSituation( SurvivorBot *owner ) : m_owner( owner )
{
	Reset();
}

// S3A5B60 deliberately leaves the timers and old path storage alone.
void SurvivorTeamSituation::Reset()
{
	m_friendPathCount = 0;
	m_seenFriendCount = 0;
	m_neighborCount = 0;
	m_separatedFriend = NULL;
	ResetScan();
}

// S3A5B90, called for every new snapshot, not for every accessor.
void SurvivorTeamSituation::ResetScan()
{
	m_downedFriend = NULL; m_downedFriendHealth = FLT_MAX;
	m_downedHuman = NULL; m_downedHumanHealth = FLT_MAX;
	m_weakestFriend = NULL; m_weakestFriendHealth = 200.0f;
	m_weakestHuman = NULL; m_weakestHumanHealth = 200.0f;
	m_nearestFriend = NULL; m_nearestFriendRange = FLT_MAX;
	m_nearestHuman = NULL; m_nearestHumanRange = FLT_MAX;
	m_recentlyFightingFriend = NULL; m_recentlyFightingFriendRange = FLT_MAX;
	m_nearestVisibleFriend = NULL; m_nearestVisibleFriendRange = FLT_MAX;
	m_farthestFriend = NULL; m_farthestFriendRange = -1.0f;
	m_leader = NULL;
	m_leadingHuman = NULL; m_leadingHumanScore = -1.0f;
	m_leadingBot = NULL; m_leadingBotScore = -1.0f;
	m_trailingFriend = NULL; m_trailingFriendFlow = FLT_MAX;
	m_trailingHuman = NULL; m_trailingHumanFlow = FLT_MAX;
	m_friendAhead = NULL; m_friendAheadFlow = FLT_MAX;
	m_friendBehind = NULL; m_friendBehindFlow = FLT_MAX;
	m_meleeWeaponCount = 0;
	m_teammateCount = 0;
	m_neighborCount = 0;
	m_tonguedFriend = NULL; m_pouncedFriend = NULL;
	m_pummeledFriend = NULL; m_jockeyedFriend = NULL;
}

void SurvivorTeamSituation::Update()
{
	ResetScan();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() )
			CollectPlayer( player );
	}
	SelectLeader();
}

// S38CB40: only handles are fast-removed. Ranges and offsets are NOT moved with
// the final handle; repairing their temporary mismatch would change retail.
void SurvivorTeamSituation::PruneInvalidTeammates()
{
	for ( int i = m_teammateCount - 1; i >= 0; --i )
	{
		if ( !m_teammates[i].Get() )
			m_teammates[i] = m_teammates[--m_teammateCount];
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void SurvivorTeamSituation::CollectPlayer( CTerrorPlayer *player )
{
	if ( !player->IsAlive() || player->GetTeamNumber() != m_owner->GetTeamNumber() || !player->GetLastKnownArea() )
		return;
	if ( player->IsTongueVictim() ) m_tonguedFriend = player;
	if ( player->IsBeingPounced() ) m_pouncedFriend = player;
	if ( player->IsPummelVictim() ) m_pummeledFriend = player;
	if ( player->IsJockeyVictim() ) m_jockeyedFriend = player;
	if ( player->IsRecentlyAttacking() ) m_friendFiredTimer.Start( 3.0f );

	bool reachable = false;
	bool testedReachability = false;
	if ( !player->IsIncapacitated() )
	{
		const int health = player->GetHealth();
		const double totalHealth = (double)player->GetHealthBuffer() + (double)health;
		if ( totalHealth < (double)m_weakestFriendHealth )
		{
			reachable = m_owner->CanReachSurvivor( player );
			if ( reachable )
			{
				m_weakestFriend = player;
				const int storedHealth = player->GetHealth();
				m_weakestFriendHealth = (float)( (double)player->GetHealthBuffer() + (double)storedHealth );
			}
		}
	}
	if ( !player->IsIncapacitated() && !player->IsFakeClient() )
	{
		const int health = player->GetHealth();
		const double totalHealth = (double)player->GetHealthBuffer() + (double)health;
		if ( totalHealth < (double)m_weakestHumanHealth )
		{
			reachable = m_owner->CanReachSurvivor( player );
			testedReachability = true;
			if ( reachable )
			{
				m_weakestHuman = player;
				const int storedHealth = player->GetHealth();
				m_weakestHumanHealth = (float)( (double)player->GetHealthBuffer() + (double)storedHealth );
			}
		}
	}

	const float flow = SurvivorFlow( player );
	const float score = float( player->GetSurvivorCharacter() ) + ( flow > 0.0f ? flow : 1.0f );
	if ( player->IsBot() )
	{
		if ( score > m_leadingBotScore ) { m_leadingBot = player; m_leadingBotScore = score; }
	}
	else if ( score > m_leadingHumanScore ) { m_leadingHuman = player; m_leadingHumanScore = score; }
	// The human record is only considered when the overall trailing record
	// changes. A bot encountered earlier can therefore hide a trailing human.
	if ( flow < m_trailingFriendFlow )
	{
		m_trailingFriend = player; m_trailingFriendFlow = flow;
		if ( !player->IsBot() ) { m_trailingHuman = player; m_trailingHumanFlow = flow; }
	}
	if ( flow <= SurvivorFlow( m_owner ) )
	{
		if ( flow < m_friendBehindFlow ) { m_friendBehind = player; m_friendBehindFlow = flow; }
	}
	else if ( flow < m_friendAheadFlow ) { m_friendAhead = player; m_friendAheadFlow = flow; }
	if ( player->HasWeaponOfType( TERROR_WEAPONTYPE_MELEE ) ) ++m_meleeWeaponCount;
	if ( m_owner->IsSelf( player ) ) return;

	if ( player->CanBeRevived() && player->m_reviveOwner.Get() == NULL )
	{
		if ( !testedReachability ) reachable = m_owner->CanReachSurvivor( player );
		if ( reachable )
		{
			const float weightedHealth = player->GetHealth() * ( player->GetTimeSinceEnemyInjury() >= 3.0f ? 1.0f : 0.1f );
			if ( weightedHealth < m_downedFriendHealth ) { m_downedFriend = player; m_downedFriendHealth = weightedHealth; }
			if ( !player->IsBot() && weightedHealth < m_downedHumanHealth ) { m_downedHuman = player; m_downedHumanHealth = weightedHealth; }
		}
		else
		{
			const Vector &from = m_owner->GetAbsOrigin(), &to = player->GetAbsOrigin();
			Msg( "SURVIVORBOT (%s, [%f %f %f]) will not help incap (%s,[%f %f %f]): UNREACHABLE via NAV\n", m_owner->GetDebugName(), from.x, from.y, from.z, player->GetDebugName(), to.x, to.y, to.z );
			L4DGameStats().ReportBotsWontHelp( m_owner, player );
		}
	}
	Vector offset = player->GetAbsOrigin() - m_owner->GetAbsOrigin();
	const float range = offset.NormalizeInPlace();
	if ( range < m_nearestFriendRange ) { m_nearestFriend = player; m_nearestFriendRange = range; }
	if ( !player->IsBot() && range < m_nearestHumanRange ) { m_nearestHuman = player; m_nearestHumanRange = range; }
	if ( range < m_recentlyFightingFriendRange && ( player->GetTimeSinceWeaponFired() < 2.0f || player->GetTimeSinceEnemyInjury() < 2.0f ) )
	{
		m_recentlyFightingFriend = player; m_recentlyFightingFriendRange = range;
	}
	const bool visible = m_owner->GetVisionInterface()->IsAbleToSee( player, IVision::DISREGARD_FOV );
	if ( visible ) RememberVisibleFriend( player );
	if ( visible && range < m_nearestVisibleFriendRange ) { m_nearestVisibleFriend = player; m_nearestVisibleFriendRange = range; }
	if ( visible && range < sb_neighbor_range.GetFloat() ) ++m_neighborCount;
	if ( range > m_farthestFriendRange ) { m_farthestFriend = player; m_farthestFriendRange = range; }
	if ( m_teammateCount < 3 )
	{
		m_teammates[m_teammateCount] = player;
		m_teammateRanges[m_teammateCount] = range;
		m_teammateOffsets[m_teammateCount] = offset;
		++m_teammateCount;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void SurvivorTeamSituation::RememberVisibleFriend( CTerrorPlayer *player )
{
	if ( !player ) return;
	for ( int i = 0; i < m_seenFriendCount; ++i )
	{
		if ( m_seenFriends[i].player.Get() && m_seenFriends[i].player->entindex() == player->entindex() )
		{
			m_seenFriends[i].lastSeen.Start();
			return;
		}
	}
	if ( m_seenFriendCount < 3 )
	{
		m_seenFriends[m_seenFriendCount].player = player;
		m_seenFriends[m_seenFriendCount].lastSeen.Start();
		++m_seenFriendCount;
	}
	else
	{
		// Native does not stop after replacing one stale entry.
		for ( int i = 0; i < m_seenFriendCount; ++i )
		{
			if ( !m_seenFriends[i].player.Get() || m_seenFriends[i].player->GetTeamNumber() != m_owner->GetTeamNumber() )
			{
				m_seenFriends[i].player = player;
				m_seenFriends[i].lastSeen.Start();
			}
		}
	}
}

void SurvivorTeamSituation::SelectLeader()
{
	m_leader = m_leadingHuman.Get() ? m_leadingHuman : m_leadingBot;
	if ( !m_leadingHuman.Get() && !m_leadingBot.Get() ) m_leader = m_owner;
	else if ( m_leadingHuman.Get() && m_leadingBot.Get() && m_leadingHumanScore < m_leadingBotScore ) m_leader = m_leadingBot;
	SelectSeparatedFriend();
	if ( sb_debug_buddy.GetBool() && m_separatedFriend.Get() )
		NDebugOverlay::HorzArrow( m_owner->GetAbsOrigin(), m_separatedFriend->GetAbsOrigin(), 5.0f, 255, 255, 0, 255, true, 0.2f );
}

bool SurvivorTeamSituation::AreAllTeammatesBots() const
{
	for ( int i = 0; i < m_teammateCount; ++i )
		if ( m_teammates[i]->IsAlive() && !m_teammates[i]->IsBot() ) return false;
	return true;
}

bool SurvivorTeamSituation::IsTeamNearCheckpoint() const
{
	if ( m_owner->IsNearCheckpoint() ) return true;
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
	if ( !checkpoint || checkpoint->HasLockedDoor() ) return false;
	TerrorNavArea *exit = checkpoint->GetLargestSpawnArea();
	if ( !exit ) return false;
	for ( int i = 0; i < m_teammateCount; ++i )
	{
		CTerrorPlayer *player = m_teammates[i].Get();
		if ( player->IsAlive() && player->GetLastKnownArea() && exit->GetFlowDistance() - ToTerrorNavArea( player->GetLastKnownArea() )->GetFlowDistance() < 1500.0f ) return true;
	}
	return false;
}

float SurvivorTeamSituation::GetRangeToFriend( CTerrorPlayer *player ) const
{
	for ( int i = 0; i < m_teammateCount; ++i ) if ( m_teammates[i].Get() == player ) return m_teammateRanges[i];
	return 100000000.0f;
}

float SurvivorTeamSituation::GetTimeSinceSeenAnyFriend() const
{
	float elapsed = 1000000000.0f;
	for ( int i = 0; i < m_seenFriendCount; ++i )
		if ( m_seenFriends[i].player.Get() && elapsed > m_seenFriends[i].lastSeen.GetElapsedTime() ) elapsed = m_seenFriends[i].lastSeen.GetElapsedTime();
	return elapsed;
}

CTerrorPlayer *SurvivorTeamSituation::GetRandomRecentlySeenFriend() const
{
	// Four draws, even when there are no records. Retail passes (0,-1) to
	// RandomInt in that case; do not introduce an early-return RNG change.
	for ( int attempt = 0; attempt < 4; ++attempt )
	{
		const int i = RandomInt( 0, m_seenFriendCount - 1 );
		if ( m_seenFriends[i].player.Get() && m_seenFriends[i].lastSeen.GetStartTime() > 0.0f && m_seenFriends[i].lastSeen.GetElapsedTime() < 0.5f ) return m_seenFriends[i].player.Get();
	}
	return NULL;
}

PathFollower *SurvivorTeamSituation::GetPathToFriend( CTerrorPlayer *player )
{
	VPROF_BUDGET( "SurvivorTeamSituation::GetPathToFriend", "NextBot" );
	if ( !player || !player->IsAlive() || !m_owner->CanReachSurvivor( player ) ) return NULL;
	SurvivorBotPathCost cost( m_owner );
	for ( int i = 0; i < m_friendPathCount; ++i )
	{
		PathFollower *path = &m_friendPaths[i];
		CBaseEntity *subject = path->GetSubject();
		if ( subject && subject->entindex() == player->entindex() )
		{
			if ( path->GetAge() > 1.0f && ( path->GetEndPosition() - subject->GetAbsOrigin() ).LengthSqr() > 10000.0f ) path->Compute( m_owner, player, cost, 1500.0f );
			return path;
		}
	}
	if ( m_friendPathCount >= 3 ) return NULL;
	PathFollower *path = &m_friendPaths[m_friendPathCount++];
	path->Compute( m_owner, player, cost, 1500.0f );
	return path;
}

CTerrorPlayer *SurvivorTeamSituation::GetClosestReachableFriend()
{
	CTerrorPlayer *closest = NULL;
	float range = FLT_MAX;
	for ( int i = 0; i < m_teammateCount; ++i )
	{
		PathFollower *path = GetPathToFriend( m_teammates[i].Get() );
		if ( path && path->GetLength() < range ) { closest = m_teammates[i].Get(); range = path->GetLength(); }
	}
	return closest;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static float SurvivorTravelDistance( CNavArea *from, CNavArea *to, SurvivorBotPathCost &cost, float limit )
{
	if ( !from || !to ) return -1.0f;
	if ( from == to ) return 0.0f;
	if ( !NavAreaBuildPath( from, to, NULL, cost, NULL, limit ) ) return -1.0f;
	float distance = 0.0f;
	for ( CNavArea *area = to; area->GetParent(); area = area->GetParent() )
	{
		const Vector offset = area->GetCenter() - area->GetParent()->GetCenter();
		// S3A673A..6756 accumulates F32 sqrt((dy*dy + dz*dz) + dx*dx).
		distance += sqrtf( ( offset.y * offset.y + offset.z * offset.z ) + offset.x * offset.x );
	}
	return distance;
}

void SurvivorTeamSituation::SelectSeparatedFriend()
{
	if ( m_separatedFriendTimer.GetRemainingTime() > 0.0f ) return;
	m_separatedFriendTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	m_separatedFriend = NULL;
	float best = FLT_MAX;
	SurvivorBotPathCost cost( m_owner, true );
	for ( int i = 0; i < m_teammateCount; ++i )
	{
		CTerrorPlayer *player = m_teammates[i].Get();
		if ( player->GetTimeSinceFriendNearby() <= sb_min_orphan_time_to_cover.GetFloat() ) continue;
		PathFollower *path = GetPathToFriend( player );
		if ( !path ) continue;
		const int character = m_owner->GetSurvivorCharacter();
		const double wideScore = (double)path->GetLength() + (double)character;
		const float score = (float)wideScore;
		if ( wideScore > (double)best ) continue;
		for ( int j = 0; j < m_teammateCount; ++j )
		{
			if ( i == j ) continue;
			CTerrorPlayer *other = m_teammates[j].Get();
			const float distance = SurvivorTravelDistance( other->GetLastKnownArea(), player->GetLastKnownArea(), cost, 2000.0f );
			// S3A71DA exits the whole scan when another teammate is closer.
			if ( distance >= 0.0f && score > other->GetSurvivorCharacter() + distance ) return;
		}
		m_separatedFriend = player;
		best = score;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

CTerrorPlayer *SurvivorTeamSituation::GetClosestFriendTo( CBaseEntity *subject, int flags )
{
	if ( !subject ) return NULL;
	CNavArea *area = TheNavMesh->GetNearestNavArea( subject->WorldSpaceCenter(), true, 200.0f, true, true );
	float closestRange = 999999.88f;
	CTerrorPlayer *closest = NULL;
	for ( int i = 0; i < m_teammateCount; ++i )
	{
		CTerrorPlayer *player = m_teammates[i].Get();
		if ( ( flags & 1 ) && !player->CanPerformBackpackAction() ) continue;
		// Retail indexes sight history by teammate index, not by matching handle.
		if ( ( flags & 2 ) && m_seenFriends[i].lastSeen.GetElapsedTime() > 0.1f ) continue;
		const float range = ( player->WorldSpaceCenter() - subject->WorldSpaceCenter() ).LengthSqr();
		if ( range < closestRange && area && m_owner->CanReachArea( player->GetLastKnownArea(), area ) ) { closest = player; closestRange = range; }
	}
	if ( ( !( flags & 1 ) || m_owner->CanPerformBackpackAction() ) &&
		( !( flags & 2 ) || m_owner->CBaseCombatCharacter::IsAbleToSee( subject, CBaseCombatCharacter::DISREGARD_FOV ) ) &&
		( m_owner->WorldSpaceCenter() - subject->WorldSpaceCenter() ).LengthSqr() < closestRange && m_owner->IsReachable( subject ) ) return m_owner;
	return closest;
}
