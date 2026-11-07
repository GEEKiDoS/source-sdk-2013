//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/nav/terror_nav_world.h"
#include "l4d2/entities/terror_transition_entities.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/director/director_item_manager.h"
#include "l4d2/director/director_music_banks.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_scavenge_mode.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/entities/survivor_rescue.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotBodyInterface.h"
#include "nav_ladder.h"
#include "nav_pathfind.h"
#include "datacache/imdlcache.h"
#include "igameevents.h"
#include <xmmintrin.h>
#include "l4d2/terror_message_text.h"
#include "l4d2/entities/trigger_finale.h"
#include <emmintrin.h>
#include "l4d2/director/director_frame_console_server_cvars.inc"
#if defined( L4D2 ) && !defined( CLIENT_DLL )
#include "tier1/convar.h"
#endif
#include "tier0/memdbgon.h"

extern ConVar z_mob_spawn_max_size;
extern ConVar z_director_special_spawn_delay; // Sole existing admission definition.
// Implemented beside the real, private CLogicBranch class in logicentities.cpp.
extern bool GetTerrorLogicBranchState( CBaseEntity *entity, bool *state );
// Native 8523D1 is bracketed ONLY by actual infected relocation3493C0.
// Its actor-owned writer must be adopted with this reader, never defaulted.
extern bool g_bSuppressSpecialRespawnInterval;

static ConVar sv_hibernate_when_empty( "sv_hibernate_when_empty", "1", 0x00000006, "Puts the server into extremely low CPU usage mode when no clients connected" );
static void OnAllBotGameChanged( IConVar *var, const char *oldValue, float oldFloat )
{
	// S:25D3E0 writes the actual server hibernation convar, not Director state.
	sv_hibernate_when_empty.SetValue( !static_cast< ConVar * >( var )->GetBool() );
}
static ConVar sb_all_bot_game( "sb_all_bot_game", "0", 0x00004004,
	"Allow a server of nothing but bots", OnAllBotGameChanged );
static ConVar allow_all_bot_survivor_team( "allow_all_bot_survivor_team", "0", 0x00000006,
	"Allow a survivor team of nothing but bots. Human players are still required for the game to be active." );
static ConVar director_allow_infected_bots( "director_allow_infected_bots", "1", 0x00000006,
	"Allow bots to fill empty slots on the infected team" );
ConVar director_no_specials( "director_no_specials", "0", 0x00004004, "Disable PZ spawns" );
static ConVar director_debug( "director_debug", "0", 0x00004004 );
static ConVar director_no_death_check( "director_no_death_check", "0", 0x00004004,
	"Disable survivor team death ending scenario" );
ConVar director_transition_timeout( "director_transition_timeout", "120", 0x00004004,
	"Duration (in seconds) to wait for survivors to transition across changelevels" );
static ConVar director_survivor_movement_window( "director_survivor_movement_window", "10.0", 0x00004004 );
static ConVar z_mob_recharge_rate( "z_mob_recharge_rate", "0.0025", 0x00004004 );
static ConVar sv_rescue_disabled( "sv_rescue_disabled", "0", 0x00000006,
	"If set to 1, dead survivors will never be rescue-able." );
ConVar rescue_interval( "rescue_interval", "2.5", 0x00004004,
	"Dead survivors are checked to start calling for help this often" );
static ConVar rescue_ignore_distance( "rescue_ignore_distance", "2000", 0x00004004,
	"If living survivors are this far past a rescuable survivor, he stops calling for help" );
static ConVar rescue_finale_ignore_distance( "rescue_finale_ignore_distance", "4000", 0x00004004,
	"If living survivors are this far past a rescuable survivor, he stops calling for help" );
static ConVar rescue_min_dead_time( "rescue_min_dead_time", "60", 0x00004004,
	"Duration in seconds a survivor must be dead before being rescuable" );
static ConVar rescue_spawn_range( "rescue_spawn_range", "2000", 0x00004004,
	"info_survivor_rescue ents will be considered if they are this close to the leading survivor" );
static ConVar rescue_finale_spawn_range( "rescue_finale_spawn_range", "4000", 0x00004004,
	"info_survivor_rescue ents will be considered if they are this close to the leading survivor" );
static ConVar z_ghost_finale_spawn_interval( "z_ghost_finale_spawn_interval", "20", 0x00004004,
	"Interval for spawning special zombies during the finale" );
static ConVar z_ghost_spawn_interval( "z_ghost_spawn_interval", "60", 0x00004004,
	"Interval for spawning special zombies" );
static ConVar z_ghost_group_spawn( "z_ghost_group_spawn", "1", 0x00000006,
	"Should ghosts all spawn at once instead of a set time after death?" );
static ConVar director_build_up_min_interval( "director_build_up_min_interval", "15", 0x00004004 );
static ConVar director_relax_min_interval( "director_relax_min_interval", "30", 0x00004004 );
static ConVar director_relax_max_interval( "director_relax_max_interval", "45", 0x00004004 );
static ConVar director_relax_max_flow_travel( "director_relax_max_flow_travel", "3000", 0x00004004 );
static ConVar director_sustain_peak_min_time( "director_sustain_peak_min_time", "3", 0x00004004 );
static ConVar director_sustain_peak_max_time( "director_sustain_peak_max_time", "5", 0x00004004 );
static ConVar director_intensity_threshold( "director_intensity_threshold", "0.9", 0x00006004 );
static ConVar director_intensity_relax_threshold( "director_intensity_relax_threshold", "0.9", 0x00004004,
	"All survivors must be below this intensity before a Peak is allowed to switch to Relax (in addition to the normal peak timer)" );

namespace
{
// The native foreach includes Disconnecting, but not Disconnected. The Tank
// scan deliberately omits that connection-state gate; its caller passes false.
CTerrorPlayer *FramePlayer( int index, bool requireConnected = true )
{
	CBasePlayer *player = UTIL_PlayerByIndex( index );
	if ( !player || !player->edict() || player->entindex() == 0 || !player->IsPlayer() ||
		( requireConnected && !player->IsConnected() ) )
		return NULL;
	return ToTerrorPlayer( player );
}


// S:2E84A0 ->2E72E0 ->2E6510. Native PVS entries, not a radius search.
void ClearWanderingPopulationInSurvivorPVS()
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) continue;
		const CUtlVector< TerrorNavArea * > &areas = player->GetPotentiallyVisibleAreas();
		FOR_EACH_VEC( areas, j ) areas[j]->SetWanderingPopulation( 0 );
	}
}


}

// S:28FD50/25B980, same native functor with actual reached flags0 and8.
// The normal-direction validity gate also applies to direction1 searches.
CTerrorPlayer *FindHighestFlowSurvivor( int flowDirection, int flags )
{
	CTerrorPlayer *best = NULL;
	float bestFlow = -9999.0f;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR )
			continue;
		if ( player->IsIncapacitated() && ( flags & 1 ) ) continue;
		if ( !player->IsIncapacitated() && ( flags & 2 ) ) continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( !area || !( ToTerrorNavArea( area )->GetFlowDistance() > -9999.0f ) )
			continue;
		if ( flags & 4 )
		{
			CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
			if ( checkpoint && checkpoint->GetAreas().Find( ToTerrorNavArea( area ) ) != -1 ) continue;
		}
		if ( flags & 8 )
		{
			CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
			if ( checkpoint && checkpoint->GetAreas().Find( ToTerrorNavArea( area ) ) != -1 ) continue;
		}
		float flow = ToTerrorNavArea( area )->GetFlowDistance( static_cast< TerrorFlowDirection >( flowDirection ) );
		if ( !best || flow > bestFlow )
		{
			best = player;
			bestFlow = flow;
		}
	}
	return best;
}

namespace
{
// S:25B890/28FCD0; native28FD90 uses flags4. Unlike the highest-flow
// functor, lowest-flow flag4 excludes BOTH incoming and outward checkpoints.
CTerrorPlayer *ScanLowestSurvivorFlow( int flags, float &bestFlow )
{
	CTerrorPlayer *best = NULL;
	bestFlow = 1000000000.0f; // actual native4E6E6B28; retained if no candidate
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		if ( player->IsIncapacitated() && ( flags & 1 ) ) continue;
		if ( !player->IsIncapacitated() && ( flags & 2 ) ) continue;
		CNavArea *known = player->GetLastKnownArea();
		if ( !known ) continue;
		TerrorNavArea *area = ToTerrorNavArea( known );
		float flow = area->GetFlowDistance( FLOW_TOWARD_GOAL );
		if ( !( flow > -9999.0f ) ) continue;
		if ( flags & 4 )
		{
			CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
			if ( checkpoint && checkpoint->GetAreas().Find( area ) != -1 ) continue;
			checkpoint = TheTerrorNavMesh()->GetExitCheckpoint();
			if ( checkpoint && checkpoint->GetAreas().Find( area ) != -1 ) continue;
		}
		if ( !best || bestFlow > flow ) { best = player; bestFlow = flow; }
	}
	return best;
}
}

CTerrorPlayer *FindLowestFlowSurvivor( int flags )
{
	float flow;
	return ScanLowestSurvivorFlow( flags, flow );
}

float GetLowestSurvivorFlow( int flags )
{
	float flow;
	ScanLowestSurvivorFlow( flags, flow );
	return flow; // Never requery the selected player's NAV/flow.
}

namespace
{


// S:255C0/25BD0. The native state==-1 accepts dead as well as living slots.
int CountTeamSlots( int team, bool includeBots )
{
	int count = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( player && player->GetTeamNumber() == team && ( includeBots || !player->IsBot() ) )
			++count;
	}
	return count;
}

TerrorNavArea *RescueNavArea( CSurvivorRescue *spot )
{
	// S:2CCDE0. This is a real nearest-area query, not cached closet flow.
	return ToTerrorNavArea( TheNavMesh->GetNearestNavArea( spot->GetAbsOrigin(),
		false, 350.0f, true, true, TEAM_ANY, true ) );
}

// S:2CD3B0, including its strict 350-unit test and canonical survivor filter.
bool IsLivingSurvivorInCloset( CSurvivorRescue *spot )
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectTerrorPlayers( survivors, TEAM_SURVIVOR, 2, 3, 9 );
	FOR_EACH_VEC( survivors, i )
	{
		CTerrorPlayer *player = survivors[i];
		CNavArea *area = player->GetLastKnownArea();
		if ( area && ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_RESCUE_CLOSET ) &&
			( player->GetAbsOrigin() - spot->GetAbsOrigin() ).LengthSqr() < 350.0f * 350.0f )
			return true;
	}
	return false;
}

// S:2CEDE0/2D1D20 and the path-length limit use this scalar addition order,
// unlike the endpoint-adjusted floor-edge length and its x/y/z dot product.
float RescuePhysicalDistance( const Vector &from, const Vector &to )
{
	float dx = from.x - to.x;
	float dy = from.y - to.y;
	float dz = from.z - to.z;
	return FastSqrt( ( dy * dy + dz * dz ) + dx * dx );
}

// S:2D1D20 with original flags3. These flags bypass both danger weighting and
// transient-area weighting; they do not bypass blocked areas or height limits.
float RescuePathCost( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
	const CFuncElevator *elevator, float length )
{
	if ( !from ) return 0.0f;
	if ( ( area->GetAttributes() & 0x30001 ) || area->IsBlocked( TEAM_SURVIVOR ) )
		return -1.0f;
	if ( ladder )
		return ladder->IsUsableByTeam( TEAM_SURVIVOR ) ? from->GetCostSoFar() + ladder->m_length * 2.0f : -1.0f;
	if ( !( length > 0.0f ) )
		length = RescuePhysicalDistance( area->GetCenter(), from->GetCenter() );
	if ( !elevator )
	{
		float height = from->ComputeAdjacentConnectionHeightChange( area );
		if ( height > 57.0f || -height > 200.0f ) return -1.0f;
		if ( height > 18.0f ) length *= 2.0f;
		else if ( height < -18.0f ) length = ( ( -height - 18.0f ) * 0.012195121f + 1.0f ) * length;
	}
	return from->GetCostSoFar() + length;
}

// S:2CE120. The SDK's generic NavAreaBuildPath has no start-position argument,
// adds a minimum-cost clamp absent here, and skips parent edges. Keep this
// particular original specialization, including endpoint-adjusted FLOOR edges.
bool BuildRescuePath( CNavArea *start, CNavArea *goal, const Vector &startPos,
	const Vector &goalPos, float maxPathLength )
{
	if ( !start ) return false;
	if ( goal && goal->IsBlocked( TEAM_SURVIVOR ) ) goal = NULL;
	start->SetParent( NULL );
	if ( start == goal ) return true;
	CNavArea::ClearSearchLists();
	start->SetTotalCost( ( start->GetCenter() - goalPos ).Length() );
	start->SetCostSoFar( 0.0f );
	start->SetPathLengthSoFar( 0.0f );
	start->AddToOpenList();
	while ( !CNavArea::IsOpenListEmpty() )
	{
		CNavArea *area = CNavArea::PopOpenList();
		if ( area->IsBlocked( TEAM_SURVIVOR ) ) continue;
		if ( area == goal || ( !goal && area->Contains( goalPos ) ) ) return true;

		// Native insertion order is N/E/S/W, ladder up forward/left/right,
		// ladder down, then actual elevator connections. No behind-up edge.
		auto visit = [&]( CNavArea *next, NavTraverseType how, const CNavLadder *ladder,
			const CFuncElevator *elevator, float length )
		{
			if ( next == area || next->IsBlocked( TEAM_SURVIVOR ) ) return;
			float cost = RescuePathCost( next, area, ladder, elevator, length );
			if ( cost < 0.0f ) return;
			if ( maxPathLength > 0.0f )
			{
				float distance = area->GetPathLengthSoFar() + RescuePhysicalDistance( next->GetCenter(), area->GetCenter() );
				if ( distance > maxPathLength ) return;
				next->SetPathLengthSoFar( distance );
			}
			if ( ( next->IsOpen() || next->IsClosed() ) && next->GetCostSoFar() <= cost ) return;
			float distanceSquared = ( next->GetCenter() - goalPos ).LengthSqr();
			float remaining = 0.0f;
			if ( distanceSquared > 0.0f )
			{
				// Original2CE7DF is rsqrtss, then a single rounded product.
				// This SDK's FastSqrt is sqrtf and is NOT interchangeable.
				float reciprocal = _mm_cvtss_f32( _mm_rsqrt_ss( _mm_set_ss( distanceSquared ) ) );
				remaining = reciprocal * distanceSquared;
			}
			next->SetCostSoFar( cost );
			next->SetTotalCost( cost + remaining );
			if ( next->IsOpen() ) next->UpdateOnOpenList();
			else next->AddToOpenList();
			next->SetParent( area, how );
		};
		for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
		{
			const NavConnectVector *adjacent = area->GetAdjacentAreas( static_cast< NavDirType >( direction ) );
			FOR_EACH_VEC( *adjacent, i )
			{
				CNavArea *next = ( *adjacent )[i].area;
				float length = ( *adjacent )[i].length;
				if ( area == start )
					length = ( ( next == goal ? goalPos : next->GetCenter() ) - startPos ).Length();
				else if ( next == goal )
					length = ( goalPos - area->GetCenter() ).Length();
				visit( next, static_cast< NavTraverseType >( direction ), NULL, NULL, length );
			}
		}
		const NavLadderConnectVector *up = area->GetLadders( CNavLadder::LADDER_UP );
		FOR_EACH_VEC( *up, i )
		{
			const CNavLadder *ladder = ( *up )[i].ladder;
			if ( ladder->m_topForwardArea ) visit( ladder->m_topForwardArea, GO_LADDER_UP, ladder, NULL, -1.0f );
			if ( ladder->m_topLeftArea ) visit( ladder->m_topLeftArea, GO_LADDER_UP, ladder, NULL, -1.0f );
			if ( ladder->m_topRightArea ) visit( ladder->m_topRightArea, GO_LADDER_UP, ladder, NULL, -1.0f );
		}
		const NavLadderConnectVector *down = area->GetLadders( CNavLadder::LADDER_DOWN );
		FOR_EACH_VEC( *down, i )
		{
			const CNavLadder *ladder = ( *down )[i].ladder;
			if ( ladder->m_bottomArea ) visit( ladder->m_bottomArea, GO_LADDER_DOWN, ladder, NULL, -1.0f );
		}
		if ( area->HasAttributes( 0x40000000 ) && area->GetElevator() )
		{
			const NavConnectVector &adjacent = area->GetElevatorAreas();
			FOR_EACH_VEC( adjacent, i )
			{
				CNavArea *next = adjacent[i].area;
				visit( next, next->GetCenter().z > area->GetCenter().z ? GO_ELEVATOR_UP : GO_ELEVATOR_DOWN,
					NULL, area->GetElevator(), -1.0f );
			}
		}
		area->AddToClosedList();
	}
	return false;
}

// S:2CEDE0. The path cost selects the route; the returned distance is the
// physical parent-chain length with the two ORIGINAL entity-origin endpoints.
float RescueTravelDistance( CSurvivorRescue *spot, float maxPathLength )
{
	TerrorNavArea *goal = RescueNavArea( spot );
	if ( !goal ) return -1.0f;
	Vector goalPos = spot->GetAbsOrigin() + Vector( 0, 0, 18.0f );
	CUtlVector< CTerrorPlayer * > survivors;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() )
			survivors.AddToTail( player );
	}
	float nearest = -1.0f;
	FOR_EACH_VEC( survivors, i )
	{
		CTerrorPlayer *player = survivors[i];
		Vector startPos = player->GetAbsOrigin();
		if ( !BuildRescuePath( player->GetLastKnownArea(), goal, startPos, goalPos, maxPathLength ) ) continue;
		CNavArea *parent = goal->GetParent();
		float distance;
		if ( !parent ) distance = RescuePhysicalDistance( goalPos, startPos );
		else
		{
			distance = RescuePhysicalDistance( goalPos, parent->GetCenter() );
			CNavArea *next = parent->GetParent();
			while ( next )
			{
				distance += RescuePhysicalDistance( parent->GetCenter(), next->GetCenter() );
				parent = next;
				next = parent->GetParent();
			}
			distance = RescuePhysicalDistance( startPos, parent->GetCenter() ) + distance;
		}
		if ( nearest < 0.0f || nearest > distance ) nearest = distance;
	}
	return nearest;
}

bool HasAssignedRescueSpot( CTerrorPlayer *player )
{
	CSurvivorRescue *spot = dynamic_cast< CSurvivorRescue * >(
		gEntList.FindEntityByClassname( NULL, "info_survivor_rescue" ) );
	while ( spot )
	{
		if ( spot->GetRescueTarget() == player ) return true;
		spot = dynamic_cast< CSurvivorRescue * >(
			gEntList.FindEntityByClassname( spot, "info_survivor_rescue" ) );
	}
	return false;
}
}

// Native ordered/unordered directions and spill points apply only to these two methods.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S:26E9D0. This selects an EXISTING rescue entity and appends its real handle
// before SetRescueTarget. Closing an open door deliberately postpones append.
void CDirector::SelectRescueSpot( bool inFinale, CTerrorPlayer *survivor )
{
	CSurvivorRescue *best = NULL;
	float range = inFinale ? rescue_finale_spawn_range.GetFloat() : rescue_spawn_range.GetFloat();
	float bestDistance = FLT_MAX;
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_rescue" ) ) )
	{
		CSurvivorRescue *spot = static_cast< CSurvivorRescue * >( entity );
		if ( spot->IsCallingForRescue() || ( !inFinale && spot->GetRescueCount() != 0 ) ) continue;
		TerrorNavArea *area = RescueNavArea( spot );
		if ( inFinale && ( !area || !area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) ) ) continue;
		if ( IsLivingSurvivorInCloset( spot ) ) continue;
		if ( !spot->HasClosedDoor() && IsTerrorSpawnPointVisibleToTeam( spot->WorldSpaceCenter(),
			TEAM_SURVIVOR, 0, 0.0f, area, spot ) ) continue;
		if ( best && ( spot->GetRescueCount() > best->GetRescueCount() ||
			( !spot->HasClosedDoor() && best->HasClosedDoor() ) ) ) continue;
		float distance = RescueTravelDistance( spot, range + 500.0f );
		if ( !( distance < 0.0f ) && !( distance > range ) &&
			( !best || ( !best->HasClosedDoor() && spot->HasClosedDoor() ) ||
			best->GetRescueCount() > spot->GetRescueCount() || bestDistance > distance ) )
		{
			best = spot;
			bestDistance = distance;
		}
	}
	if ( !best ) return;
	if ( best->CloseDoors() )
	{
		DevMsg( "rescue spot %d(%s) closes door to start calling for help with nearest survivor %.1f path units away\n",
			best->entindex(), STRING( best->GetEntityName() ), bestDistance );
		return;
	}
	DevMsg( "rescue spot %d(%s) starts calling for help for %s with nearest survivor %.1f path units away\n",
		best->entindex(), STRING( best->GetEntityName() ), survivor->GetPlayerName(), bestDistance );
	m_activeRescueSpots.AddToTail( best );
	best->SetRescueTarget( survivor );
	m_RescueUpdateTimer.Start( 0.1f );
}

// S272190. Only selected active handles occupy Director992, not an entity registry.
void CDirector::UpdateRescueSpots()
{
	if ( sv_rescue_disabled.GetBool() || !( (double)gpGlobals->curtime >= (double)m_RescueUpdateTimer.m_timestamp ) ||
		!HasAnySurvivorLeftSafeArea() || !AllowSurvivorRescue( GetModeDescription()->IsBaseMode( "coop" ) ) ) return;
	m_RescueUpdateTimer.Start( rescue_interval.GetFloat() );
	// Native25BB10 tests FINALE0x40, not BATTLEFIELD0x100. It returns true
	// for an empty survivor set, and includes incapacitated players here.
	bool allInFinale = true;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( !area || !ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_FINALE ) ) { allInFinale = false; break; }
	}
	bool inBattlefield = m_pScenario->AreSurvivorsInFinaleAreaBeforePanic();
	for ( int i = m_activeRescueSpots.Count() - 1; i >= 0; --i )
	{
		CSurvivorRescue *spot = m_activeRescueSpots[i].Get();
		if ( spot )
		{
			if ( !IsFinale() && spot->IsCallingForRescue() && spot->GetRescueTarget() )
			{
				TerrorNavArea *area = RescueNavArea( spot );
				float range = 0.0f;
				if ( area ) range = area->HasSpawnAttributes( TERROR_NAV_FINALE | TERROR_NAV_BATTLEFIELD ) ?
					rescue_finale_ignore_distance.GetFloat() : rescue_ignore_distance.GetFloat();
				if ( !area || RescueTravelDistance( spot, range + 500.0f ) < 0.0f )
				{
					DevMsg( "rescue spot %d(%s) stops calling for help with nearest survivor at least %.1f path units away\n",
						spot->entindex(), STRING( spot->GetEntityName() ), range );
					IGameEvent *event = gameeventmanager->CreateEvent( "survivor_rescue_abandoned" );
					if ( event ) gameeventmanager->FireEvent( event );
					spot->SetRescueTarget( NULL );
					m_activeRescueSpots.FastRemove( i );
				}
				continue;
			}
			if ( !IsFinale() ) spot->SetRescueTarget( NULL );
			else
			{
				inputdata_t input;
				input.pActivator = NULL;
				input.pCaller = NULL;
				input.nOutputID = 0;
				spot->InputRescue( input );
			}
		}
		m_activeRescueSpots.FastRemove( i );
	}
	if ( IsFinale() || m_pScenario->IsFirstPanicEvent() ) return;
	if ( allInFinale || inBattlefield )
	{
		CUtlVector< CTerrorPlayer * > candidates;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = FramePlayer( i );
			if ( !player || player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
			if ( !allInFinale )
			{
				float deadTime = gpGlobals->curtime - player->m_lastAliveTime;
				if ( !player->m_hasSpawnedOnTeam[0] && !( deadTime > rescue_min_dead_time.GetFloat() ) )
					deadTime = rescue_min_dead_time.GetFloat();
				if ( rescue_min_dead_time.GetFloat() > deadTime ) continue;
			}
			if ( !HasAssignedRescueSpot( player ) ) candidates.AddToTail( player );
		}
		if ( candidates.Count() > 0 ) SelectRescueSpot( true, candidates[0] );
		return;
	}
	if ( !FindHighestFlowSurvivor( FLOW_FROM_START ) ) return;
	CUtlVector< CTerrorPlayer * > assigned;
	FOR_EACH_VEC( m_activeRescueSpots, i )
	{
		// Original25F550 resolves this element; the earlier reverse cleanup
		// removed invalid handles before this forward reader.
		CTerrorPlayer *player = m_activeRescueSpots[i]->GetRescueTarget();
		if ( player ) assigned.AddToTail( player );
	}
	CTerrorPlayer *candidate = NULL;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || assigned.Find( player ) != assigned.InvalidIndex() ) continue;
		float deadTime = player->m_hasSpawnedOnTeam[0] ? gpGlobals->curtime - player->m_lastAliveTime : FLT_MAX;
		if ( !( rescue_min_dead_time.GetFloat() > deadTime ) &&
			( !candidate || candidate->IsBot() || !player->IsBot() ) &&
			( !candidate || gpGlobals->curtime - player->m_lastAliveTime > gpGlobals->curtime - candidate->m_lastAliveTime ) )
			candidate = player;
	}
	if ( candidate ) SelectRescueSpot( false, candidate );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:2741C0 Tank448/special456/humaninfected460 scan. Actual body arousal0 is
// NEUTRAL (PlayerBody v+272); this is NOT an activity or a posture test.
void CDirector::UpdateInfectedCounts()
{
	int previousTankCount = m_nTankCount;
	m_nTankCount = m_nSpecialCount = m_nHumanInfectedCount = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i, false );
		if ( !player || player->GetTeamNumber() != TEAM_INFECTED || ( !player->IsAlive() && player->IsBot() ) ) continue;
		if ( player->GetZombieClass() == 8 )
		{
			INextBot *bot = player->MyNextBotPointer();
			if ( player->IsAlive() && ( !bot || !bot->GetBodyInterface()->IsArousal( IBody::NEUTRAL ) ) ) ++m_nTankCount;
		}
		else if ( player->GetZombieClass() != 7 ) ++m_nSpecialCount;
		if ( !player->IsBot() ) ++m_nHumanInfectedCount;
	}
	if ( m_nTankCount != 0 && previousTankCount == 0 )
	{
		CTerrorPlayer *leader = FindHighestFlowSurvivor( FLOW_TOWARD_GOAL );
		m_flTankStartFlow = leader ? (float)leader->GetFlowDistance( FLOW_TOWARD_GOAL ) : -9999.0f;
	}
}

// S:25C1E0/25E9B0 and the actual ten-element frame history. Zero is the
// native first-sample sentinel; do not replace it with FLT_MAX or filter incap.
void CDirector::UpdateSurvivorFlow()
{
	if ( m_SurvivorMovementTimer.m_timestamp > 0.0f )
	{
		if ( gpGlobals->curtime < m_SurvivorMovementTimer.m_timestamp ) return;
		float minFlow = 0.0f;
		float maxFlow = 0.0f;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = FramePlayer( i );
			if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
			CNavArea *area = player->GetLastKnownArea();
			if ( !area ) continue;
			float flow = ToTerrorNavArea( area )->GetFlowDistance();
			if ( minFlow == 0.0f || minFlow > flow ) minFlow = flow;
			if ( maxFlow == 0.0f || flow > maxFlow ) maxFlow = flow;
		}
		m_minSurvivorFlow[m_nSurvivorFlowIndex] = minFlow;
		m_maxSurvivorFlow[m_nSurvivorFlowIndex] = maxFlow;
		m_nSurvivorFlowIndex = ( m_nSurvivorFlowIndex + 1 ) % 10;
		if ( maxFlow > m_flMaximumSurvivorFlow ) m_flMaximumSurvivorFlow = maxFlow;
		float lowest = m_maxSurvivorFlow[0];
		float highest = lowest;
		m_flSurvivorFlowSpan = 0.0f;
		for ( int i = 0; i < ARRAYSIZE( m_maxSurvivorFlow ); ++i )
		{
			m_flSurvivorFlowSpan = ( m_maxSurvivorFlow[i] - m_minSurvivorFlow[i] ) + m_flSurvivorFlowSpan;
			if ( lowest > m_maxSurvivorFlow[i] ) lowest = m_maxSurvivorFlow[i];
			if ( m_maxSurvivorFlow[i] > highest ) highest = m_maxSurvivorFlow[i];
		}
		m_flSurvivorFlowSpan *= 0.1f;
		m_flSurvivorMovement = ( highest - lowest ) / director_survivor_movement_window.GetFloat();
	}
	m_SurvivorMovementTimer.Start( director_survivor_movement_window.GetFloat() * 0.1f );
}

// S:26EDF0, before flow sampling and after status. This is the actual projectile
// handle scan; no fabricated grenade count is substituted for the real vector.
void CDirector::UpdatePipeBombs()
{
	if ( gpGlobals->curtime < m_PipeBombScanTimer.m_timestamp ) return;
	m_PipeBombScanTimer.Start( 1.0f );
	m_pipeBombs.RemoveAll();
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "pipe_bomb_projectile" ) ) )
		m_pipeBombs.AddToTail( entity );
}

// S:26D5F0. This is the real +640 producer, not the speech cleanup265450.
void CDirector::UpdateGamePlaying()
{
	KeyValues *mode = GetModeDescription()->GetCurrentMode();
	if ( m_pScenario->IsSacrificeFinale() && m_pScenario->HasSacrificeSucceeded() && mode && mode->GetInt( "maxplayers", 0 ) == 1 ) return;
	int survivorSlots = CountTeamSlots( TEAM_SURVIVOR, true );
	int infectedHumans = CountTeamSlots( TEAM_INFECTED, false );
	int survivors = CountLivingSurvivors( 1 );
	if ( survivors != 0 && !sb_all_bot_game.GetBool() && !g_bTerrorTransitionRestore && !m_bTransitionInProgress )
	{
		int humans = CountLivingSurvivors( 4 );
		// These two counts have different incap/bot filters in the original.
		// Do not replace their minimum with a newly intersected selector.
		if ( !allow_all_bot_survivor_team.GetBool() && survivors >= humans ) survivors = humans;
		if ( survivors != 0 )
		{
			if ( !GetModeDescription()->IsPlayerControlledZombies() || infectedHumans > 0 )
				if ( m_HumanSurvivorGraceTimer.m_timestamp > 0.0f ) m_HumanSurvivorGraceTimer.Invalidate();
		}
		else if ( gpGlobals->curtime < m_HumanSurvivorGraceTimer.m_timestamp ) return;
	}
	if ( GetModeDescription()->IsPlayerControlledZombies() )
	{
		if ( gpGlobals->curtime < m_HumanSurvivorGraceTimer.m_timestamp )
			m_bGamePlaying = allow_all_bot_survivor_team.GetBool() ? infectedHumans > 0 : survivorSlots > 0 && infectedHumans > 0;
		else m_bGamePlaying = true;
	}
	else m_bGamePlaying = survivorSlots > 0;
	if ( sb_all_bot_game.GetBool() ) m_bGamePlaying = true;
	m_bEverHadHumanSurvivor = m_bEverHadHumanSurvivor || survivors > 0;
	// Original byte642 is RECOMPUTED, despite the retained field's old label.
	m_bEverHadHumanPlayer = m_bEverHadHumanSurvivor || infectedHumans > 0;
	bool onlyDeadOrIncapacitatedSurvivors = survivorSlots != 0 && survivors == 0;
	bool canEnd = sb_all_bot_game.GetBool() || !GetModeDescription()->IsPlayerControlledZombies() || infectedHumans > 0;
	if ( m_bGamePlaying && ( m_bEverHadHumanSurvivor || allow_all_bot_survivor_team.GetBool() ) &&
		onlyDeadOrIncapacitatedSurvivors && canEnd ) EndScenario();
}

// S:623F0. Actual survivor character -> player mapping is last slot wins;
// missing characters retain the original +infinity coordinates (not SDK
// vec3_invalid/FLT_MAX), including NaN separation between two missing slots.
void CDirector::UpdateSurvivorSeparation()
{
	CTerrorPlayer *characters[8] = { NULL };
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && static_cast< unsigned int >( player->GetSurvivorCharacter() ) <= 7 )
			characters[player->GetSurvivorCharacter()] = player;
	}
	float positions[8][3];
	for ( int i = 0; i < ARRAYSIZE( positions ); ++i )
	{
		if ( characters[i] )
		{
			const Vector &origin = characters[i]->GetAbsOrigin();
			positions[i][0] = origin.x;
			positions[i][1] = origin.y;
			positions[i][2] = origin.z;
		}
		else positions[i][0] = positions[i][1] = positions[i][2] = BitsToFloat( 0x7f800000 );
	}
	for ( int i = 0; i < ARRAYSIZE( positions ); ++i )
	{
		m_survivorSeparation[i][i] = 0.0f;
		for ( int j = i + 1; j < ARRAYSIZE( positions ); ++j )
		{
			float dx = positions[i][0] - positions[j][0];
			float dy = positions[i][1] - positions[j][1];
			float dz = positions[i][2] - positions[j][2];
			float distance = FastSqrt( ( dy * dy + dz * dz ) + dx * dx );
			m_survivorSeparation[i][j] = m_survivorSeparation[j][i] = distance;
		}
	}
	m_nHumansNearCheckpoint = m_nHumansAwayFromCheckpoint = 0;
	CTerrorCheckpoint *checkpoint = TheTerrorNavMesh()->GetTransitionCheckpoint();
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || player->IsBot() ) continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( !area ) continue;
		if ( !ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_EMPTY | TERROR_NAV_CHECKPOINT | TERROR_NAV_RESCUE_VEHICLE ) ||
			( checkpoint && checkpoint->GetAreas().Find( ToTerrorNavArea( area ) ) != checkpoint->GetAreas().InvalidIndex() ) ) ++m_nHumansAwayFromCheckpoint;
		else ++m_nHumansNearCheckpoint;
	}
}

// S:25D400 intentionally accepts1..6 but indexes the six native timers WITHOUT
// subtracting1. Index6 therefore reads the SPECIAL VECTOR'S GROW-SIZE word as a
// float. Keep that original journal bug without out-of-bounds C++ array access.
double CDirector::GetDirectorTimerRemaining( int index ) const
{
	if ( static_cast< unsigned int >( index ) - 1U > 5U ) return 0.0f;
	if ( index < 6 ) return static_cast< double >( m_BaseSpecialSpawnTimers[index].m_timestamp ) - static_cast< double >( gpGlobals->curtime );
	int growSize = m_SpecialSpawnTimers.GetGrowSize();
	float growthWord;
	V_memcpy( &growthWord, &growSize, sizeof( growthWord ) );
	return static_cast< double >( growthWord ) - static_cast< double >( gpGlobals->curtime );
}
double CDirector::GetSpecialSpawnTimerRemaining( int index ) const
{
	if ( index < 0 || index >= m_SpecialSpawnTimers.Count() ) return 0.0f;
	return static_cast< double >( m_SpecialSpawnTimers[index].m_timestamp ) - static_cast< double >( gpGlobals->curtime );
}

// S:26FF00. Survival and Scavenge have real, separate arbitration; neither is
// folded into scenario type or the scripted context's active byte.
void CDirector::UpdateMobSpawning()
{
	if ( m_pSurvivalMode->Update() ) return; // S:28EE00
	if ( !IsFinale() && ( m_nIntensityState == 3 || m_nIntensityState == 2 ) ) return;
	if ( IsInCommentaryMode() ) return;
	if ( GetModeDescription()->IsBaseMode( "scavenge" ) ) { UpdateMob(); return; } // S:2856B0
	if ( m_pScenario->Update() || !m_pScenario->ShouldAllowMobs() ) return;
	UpdateMob(); // S:26CBB0, real population producer
}

// S:2741C0, full frame ORDER. Called services remain their real native owners;
// unfinished broad Director services are not supplied an empty implementation.
void CDirector::UpdateProviders()
{
	VPROF_BUDGET( "Director::Update", "ZOMBIE" );
	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	UpdateDirectorTestLoop(); // S:260DF0
	bool update = false;
	if ( m_pSessionManager->Update() )
	{
		if ( !ProcessTransitionRestoration() || HasAnySurvivorLeftSafeArea() ) update = true;
		else { UpdateMissionStatus(); UpdateStartAreaExit(); }
	}
	if ( director_debug.GetBool() ) DrawDebugText(); // S:2693B0, even when frozen
	if ( !update ) return;

	m_pScriptedMode->UpdateForFrame(); // S:287840 HUD -> journal -> callback -> finale
	UpdateMobSpawning();
	if ( GetModeDescription()->IsPlayerControlledZombies() )
	{
		UpdateTankOffer(); // S:26C520
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = FramePlayer( i );
			if ( player && !player->IsAlive() && !player->IsBot() && player->CanBecomeGhost( false ) )
				player->AttemptBecomeGhost( false ); // S:32FB10 repeats native eligibility321030
		}
		if ( !m_pScriptedMode->IsTankRun() && director_allow_infected_bots.GetBool() ) UpdateSpecialSpawning();
	}
	else UpdateSpecialSpawning(); // S:26F910, original virtual+88
	UpdateTempHealthDecayRate(); // S:266330
	CTerrorGameRules *pWaterRules = TerrorGameRules();
	pWaterRules->m_bWaterSlowdownEnabled = GetDirectorOptionInt( "WaterSlowsMovement", 1 ) != 0;
	UpdateRescueSpots();
	m_pItemManager->Update();
	m_pMusicBanks->Update();
	m_pScenario->UpdatePanic();
	UpdateSpawnRemovalQueue(); // S:265E00
	UpdateInfectedCounts();
	UpdateIntensity(); // S:266B50, original virtual+20
	m_pVersusMode->Update(); // S:292160
	if ( HaveAllSurvivorsLeftSafeArea() )
	{
		float recharge = GetDirectorOptionFloat( "MobRechargeRate", z_mob_recharge_rate.GetFloat() );
		m_flMobSizeIncrement = gpGlobals->frametime * recharge + m_flMobSizeIncrement;
		m_flMobSize = gpGlobals->frametime * m_flMobSizeIncrement + m_flMobSize;
		if ( m_flMobSize > GetDirectorOptionFloat( "MobMaxSize", z_mob_spawn_max_size.GetFloat() ) )
			m_flMobSize = GetDirectorOptionFloat( "MobMaxSize", z_mob_spawn_max_size.GetFloat() );
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = FramePlayer( i );
		if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() ) continue;
		CNavArea *area = player->GetLastKnownArea();
		if ( area && !ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) && !player->HasLeftCheckpoint() )
			player->OnLeftCheckpoint(); // S:2FC490, actual speech/state lifecycle
	}
	UpdateStartAreaExit();
	UpdateMissionStatus();
	UpdatePipeBombs();
	UpdateSurvivorFlow();
	m_pScenario->UpdateGauntletMovement();
	UpdateScripts();
	UpdateSurvivorSeparation();
	if ( m_bUpdatePreferredL4D1Survivor )
	{
		CBaseEntity *branch = gEntList.FindEntityByName( NULL, "branch_zoey" );
		m_bUpdatePreferredL4D1Survivor = false;
		bool state;
		if ( GetTerrorLogicBranchState( branch, &state ) ) m_nPreferredL4D1Survivor = 6 - ( state ? 1 : 0 );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:271BF0. Preserve the actual round-end/status/exit gates and their order;
// UpdateGamePlaying is reached only through the original final ordinary branch.
void CDirector::UpdateMissionStatus()
{
	if ( !( m_MissionEndTimer.m_timestamp > 0.0f ) )
	{
		if ( m_bMissionEndBlocked ) return;
		if ( m_ScenarioExitTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_ScenarioExitTimer.m_timestamp )
		{
			m_ScenarioExitTimer.Invalidate();
			CheckScenarioExit(); // S:26D430, real versus exit/disconnect owner
			return;
		}
		static ConVarRef director_test_loop( "director_test_loop" );
		if ( director_test_loop.GetBool() )
		{
			ResetMissionEndState(); // S:260BE0, original virtual76
			director_test_loop.SetValue( 0 );
			EndRound( 0 ); // S:269130, not OnRoundEnd(bool)
			return;
		}
		if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_MissionStatusTimer.m_timestamp.Get() ) ) ) return;
		m_MissionStatusTimer.Start( 1.0f );
		m_pVersusMode->UpdateMissionEnd(); // S:291B00
		if ( m_bGamePlaying && m_bEverHadHumanPlayer && GetModeDescription()->IsPlayerControlledZombies() &&
			!sb_all_bot_game.GetBool() && gpGlobals->curtime >= m_HumanSurvivorGraceTimer.m_timestamp )
		{
			int waitingHumans = 0;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = FramePlayer( i );
				if ( player && !player->IsBot() && ( player->IsAwaitingTransitionRestore() ||
					!m_pSessionManager->IsUserIDFullyConnected( player->GetUserID() ) ) ) ++waitingHumans;
			}
			if ( waitingHumans == 0 )
			{
				int survivorHumans = CountTeamSlots( TEAM_SURVIVOR, false );
				int infectedHumans = CountTeamSlots( TEAM_INFECTED, false );
				if ( ( survivorHumans == 0 && !allow_all_bot_survivor_team.GetBool() ) || infectedHumans == 0 )
				{
					EndRound( GetModeDescription()->IsBaseMode( "versus" ) ? 8 : 13 );
					return;
				}
			}
		}
		if ( IsBleedoutSuppressed() )
		{
			if ( m_pScenario->GetFinaleType() == 1 ) StopLocalScript(); // S:266940
		}
		else
		{
			if ( GetModeDescription()->IsBaseMode( "scavenge" ) ) m_pScavengeMode->CheckMissionStatus(); // S:286B80
			if ( !director_no_death_check.GetBool() ) UpdateGamePlaying();
		}
		return;
	}

	if ( m_bDelayMissionEndScoreboard &&
		gpGlobals->curtime - m_MissionEndTimer.m_timestamp + m_MissionEndTimer.m_duration > 1.0f )
	{
		const char *panel = NULL;
		if ( GetModeDescription()->IsBaseMode( "survival" ) ) panel = "fullscreen_survival_scoreboard";
		else if ( GetModeDescription()->IsBaseMode( "scavenge" ) ) panel = "fullscreen_scavenge_scoreboard";
		else if ( TerrorGameRules()->m_bChallengeModeActive ) m_pScriptedMode->ShowScoreboard(); // S:276750
		if ( panel )
		{
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
				if ( CTerrorPlayer *player = FramePlayer( i ) ) player->ShowViewPortPanel( panel, true, NULL );
		}
		m_bDelayMissionEndScoreboard = false;
	}
	if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_MissionEndTimer.m_timestamp.Get() ) ) ) return;
	m_MissionEndTimer.Invalidate();
	switch ( m_nRoundEndReason )
	{
	case 5:
	case 8:
		m_pVersusMode->EndRound(); // S:291970
		break;
	case 7:
		m_pVersusMode->FinishRound(); // S:290830
		break;
	case 9:
	case 10:
	case 13:
		m_pScavengeMode->EndRound(); // S:2868B0
		break;
	case 12:
		m_pScavengeMode->ResetRound(); // S:285830
		break;
	case 14:
	case 16:
		m_pSurvivalMode->EndRound(); // S:28E9D0
		break;
	default:
		// Native repeats Invalidate in this branch; the timer itself guards
		// unchanged writes. Do not turn this into another round-start/reset.
		m_MissionEndTimer.Invalidate();
		RestartScenario(); // S:2700D0, actual campaign transition
		break;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:271BA0. Existing world records clear before saved players and scenario restart.
void CDirector::RestartAfterNoHumanPlayers()
{
	m_nRoundEndReason = 0;
	ClearTerrorTransitionEntities();
	g_SavedTerrorPlayerStates.ClearTransitionRecords();
	RestartScenario();
	m_bInSecondHalfOfRound = false;
	m_pScenario->ClearCustomFinaleChoice();
	m_pVersusMode->OnNewGame(); // S:290520, native bytes5/24
	m_pScavengeMode->ResetTeams(); // native byte10
}

// S:2707E0, called by full Event_Killed336010 BEFORE CCSPlayer::Event_Killed.
// This immediate decrement is a separate writer from the later frame scan.
void CDirector::OnInfectedKilled( CTerrorPlayer *player )
{
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED ) return;
	if ( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 && m_nTankCount != 0 )
	{
		--m_nTankCount;
		DevMsg( "Tank Count = %d\n", m_nTankCount );
		struct NotifyTankKilled
		{
			CTerrorPlayer *m_tank;
			bool operator()( CBasePlayer *base ) const
			{
				static_cast< CTerrorPlayer * >( base )->OnTankKilled( m_tank );
				return true;
			}
		} notify = { player };
		ForEachPlayer( notify ); // S:25F2A0, real connected-player admission
		if ( m_nTankCount == 0 ) OnLastTankKilled();
	}
	if ( !g_bSuppressSpecialRespawnInterval &&
		( player->GetZombieClass() >= 0 || player->GetZombieClass() < 9 ) )
	{
		int zombieClass = player->GetZombieClass();
		// Original OR is intentional. For real class9, IntervalStart at
		// 684+8*9 writes the DURATION of base timer0, not its deadline.
		// Preserve that alias through actual storage, not target array OOB.
		if ( zombieClass == 9 )
		{
			if ( m_BaseSpecialSpawnTimers[0].m_duration != gpGlobals->curtime )
				m_BaseSpecialSpawnTimers[0].m_duration = gpGlobals->curtime;
		}
		else m_ClassSpawnIntervals[zombieClass].Start();
	}
	int slot = player->m_nSpecialSpawnSlot;
	if ( slot >= 0 )
	{
		if ( slot < m_SpecialSpawnTimers.Count() )
		{
			float countdown = GetSpecialSlotCountdownTime(); // S:264820
			m_SpecialSpawnTimers[slot].Start( countdown );
		}
		player->m_nSpecialSpawnSlot = -1;
	}
}

bool CDirector::IsAllBotSurvivorTeamAllowed() const
{
	return allow_all_bot_survivor_team.GetBool(); // S:25E250, actual sole setting.
}

// S:263000. This query MUTATES the real initial-spawn timer464. It is neither
// a fixed delay nor the separate special-slot countdown264820.
double CDirector::GetInfectedSpawnDelay( float elapsed )
{
	if ( !m_bHasAnySurvivorLeftSafeArea ) return (double)z_director_special_spawn_delay.GetFloat();
	int delay = -1;
	if ( m_InitialSpawnDelay.m_timestamp > 0.0f )
	{
		if ( (double)gpGlobals->curtime >= (double)m_InitialSpawnDelay.m_timestamp )
		{
			if ( m_pScenario->GetFinaleType() != 5 ) m_InitialSpawnDelay.Start( z_ghost_finale_spawn_interval.GetFloat() );
			else if ( m_nTankCount == 0 ) m_InitialSpawnDelay.Start( z_ghost_spawn_interval.GetFloat() );
		}
		double remaining = (double)m_InitialSpawnDelay.m_timestamp - (double)gpGlobals->curtime;
		float countdown;
		if ( remaining < 1.0 ) countdown = 1.0f;
		else
		{
			// Native takes this SECOND clock sample for >=1 OR unordered.
			countdown = (float)( (double)m_InitialSpawnDelay.m_timestamp - (double)gpGlobals->curtime );
		}
		delay = _mm_cvttss_si32( _mm_set_ss( countdown ) );
	}
	if ( z_ghost_group_spawn.GetBool() && elapsed > 0.0f )
	{
		int elapsedSeconds = _mm_cvttss_si32( _mm_set_ss( elapsed ) );
		unsigned int bits = (unsigned int)z_ghost_spawn_interval.GetInt() - (unsigned int)elapsedSeconds;
		int groupDelay;
		Q_memcpy( &groupDelay, &bits, sizeof( groupDelay ) );
		if ( delay > groupDelay )
		{
			delay = groupDelay;
			m_InitialSpawnDelay.Start( (float)groupDelay );
		}
	}
	return (double)delay;
}

// S:262100. Real player_death event writer of the SAME initial timer464.
void CDirector::OnPlayerDeath( CTerrorPlayer *player )
{
	if ( player->GetTeamNumber() == TEAM_INFECTED &&
		m_bHasAnySurvivorLeftSafeArea &&
		m_InitialSpawnDelay.m_timestamp > 0.0f &&
		(double)gpGlobals->curtime >= (double)m_InitialSpawnDelay.m_timestamp &&
		TheDirector->m_pScenario->GetFinaleType() == 5 &&
		m_nTankCount == 0 )
	{
		m_InitialSpawnDelay.Start( z_ghost_spawn_interval.GetFloat() );
	}
}

// S:266500, original virtual+68. A locked tempo forces404 to BUILD_UP only;
// it does not reset408/420, and its fallback is overridden by the real option.
bool CDirector::IsTempoLocked() const
{
	bool locked = IsFinale() || m_pSurvivalMode->HasStarted() || m_pScenario->IsFirstPanicEvent() ||
		m_pVersusMode->IsActive() || m_pScavengeMode->HasStarted();
	return GetDirectorOptionInt( "LockTempo", locked ? 1 : 0 ) != 0;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:266B50. Preserve native float spills, individually widened flow operands,
// maximum-before-minimum option queries, and actual callback ordering.
void CDirector::UpdateIntensity()
{
	if ( IsTempoLocked() )
	{
		m_nIntensityState = 0;
		return;
	}
	switch ( m_nIntensityState )
	{
	case 0:
		if ( (double)gpGlobals->curtime >= (double)m_IntensityStateTimer.m_timestamp )
		{
			float intensity = GetMaximumSurvivorIntensity();
			if ( (double)intensity >= (double)GetDirectorOptionFloat( "IntensityThreshold", director_intensity_threshold.GetFloat() ) )
			{
				DevMsg( "------------------------------ Director state change BUILD_UP -> SUSTAIN_PEAK\n" );
				m_nIntensityState = 1;
				float maximum = GetDirectorOptionFloat( "SustainPeakMaxTime", director_sustain_peak_max_time.GetFloat() );
				float minimum = GetDirectorOptionFloat( "SustainPeakMinTime", director_sustain_peak_min_time.GetFloat() );
				m_IntensityStateTimer.Start( RandomFloat( minimum, maximum ) );
				ClearWanderingPopulationInSurvivorPVS();
			}
		}
		break;
	case 1:
		if ( (double)gpGlobals->curtime >= (double)m_IntensityStateTimer.m_timestamp )
		{
			DevMsg( "------------------------------ Director state change SUSTAIN_PEAK -> PEAK_FADE\n" );
			m_nIntensityState = 2;
			CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
			m_flIntensityStartFlow = front ? (float)front->GetFlowDistance( 0 ) : -9999.0f;
		}
		break;
	case 2:
		{
			CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
			bool relax = false;
			if ( m_nTankCount <= 0 )
			{
				float intensity = GetMaximumSurvivorIntensity();
				relax = (double)GetDirectorOptionFloat( "IntensityRelaxThreshold", director_intensity_relax_threshold.GetFloat() ) > (double)intensity;
			}
			if ( !relax && front && m_flIntensityStartFlow != -9999.0f )
			{
				float flow = front->GetFlowDistance( 0 );
				relax = (double)flow > (double)GetDirectorOptionFloat( "RelaxMaxFlowTravel", director_relax_max_flow_travel.GetFloat() ) + (double)m_flIntensityStartFlow;
			}
			if ( relax )
			{
				DevMsg( "------------------------------ Director state change PEAK_FADE -> RELAX\n" );
				m_nIntensityState = 3;
				float maximum = GetDirectorOptionFloat( "RelaxMaxInterval", director_relax_max_interval.GetFloat() );
				float minimum = GetDirectorOptionFloat( "RelaxMinInterval", director_relax_min_interval.GetFloat() );
				m_IntensityStateTimer.Start( RandomFloat( minimum, maximum ) );
				m_flIntensityStartFlow = front ? (float)front->GetFlowDistance( 0 ) : -9999.0f;
				L4DGameStats().Event_DirectorRelax();
			}
		}
		break;
	case 3:
		{
			CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
			bool buildUp = (double)gpGlobals->curtime >= (double)m_IntensityStateTimer.m_timestamp;
			if ( !buildUp && front && m_flIntensityStartFlow != -9999.0f )
			{
				float flow = front->GetFlowDistance( 0 );
				buildUp = (double)flow > (double)GetDirectorOptionFloat( "RelaxMaxFlowTravel", director_relax_max_flow_travel.GetFloat() ) + (double)m_flIntensityStartFlow;
			}
			if ( buildUp )
			{
				DevMsg( "------------------------------ Director state change RELAX -> BUILD_UP\n" );
				m_nIntensityState = 0;
				m_IntensityStateTimer.Start( GetDirectorOptionFloat( "BuildUpMinInterval", director_build_up_min_interval.GetFloat() ) );
			}
		}
		break;
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:2608B0. This is the actual fixed-growable slot vector828. The999 seed
// and strict comparison deliberately leave all slots >=999 (or NaN) alone.
void CDirector::DelayNextSpecialSpawn( float delay )
{
	int best = -1;
	float bestRemaining = 999.0f;
	FOR_EACH_VEC( m_SpecialSpawnTimers, i )
	{
		double remaining = (double)m_SpecialSpawnTimers[i].m_timestamp - (double)gpGlobals->curtime;
		float value;
		if ( remaining <= 0.0 ) value = 0.0f;
		else value = (float)( (double)m_SpecialSpawnTimers[i].m_timestamp - (double)gpGlobals->curtime );
		if ( bestRemaining > value ) { bestRemaining = value; best = i; }
	}
	if ( best >= 0 ) m_SpecialSpawnTimers[best].Start( bestRemaining + delay );
}

#pragma float_control( precise, on, push )
double CDirector::GetMobSpawnTimeRemaining() const
{
	const float deadline = m_MobSpawnTimer.m_timestamp;
	if ( !( deadline > 0.0f ) )
		return 10000000.0;
	return static_cast< double >( deadline ) - static_cast< double >( gpGlobals->curtime );
}
#pragma float_control( pop )

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
namespace
{
// Native1083B1C0: ONLY director_report temporarily assigns this actual player.
CTerrorPlayer *s_pDirectorDebugRecipient;

int DebugTruncate( double value )
{
	// Native __ftol2_sse's SSE2 path spills to double, then returns signed32.
	return _mm_cvttsd_si32( _mm_set_sd( value ) );
}
int DebugCeil( const CountdownTimer &timer )
{
	return DebugTruncate( ceil( (double)timer.m_timestamp - (double)gpGlobals->curtime ) );
}

// Native25DBF0 +261AA0 +263750: one real stack functor, not a HUD-side cache.
class SurvivorIntensityDebug
{
public:
	explicit SurvivorIntensityDebug( int line )
	{
		m_x = 0.01f;
		m_y = 0.0f;
		m_line = line + 1;
		DirectorDebugText( 0.01f, 0.0f, line, "SURVIVOR INTENSITY", 255, 255, 255, 255, 0.25f );
		m_firstPlayer = NULL; // Native store is AFTER logging/message/overlay.
	}
	void Draw()
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = FramePlayer( i );
			if ( !player || !player->IsAlive() ) continue;
			const int team = player->GetTeamNumber();
			if ( team != TEAM_SURVIVOR && team != TEAM_L4D1_SURVIVOR ) continue;
			const float intensity = player->GetIntensity(); // Actual raw12020 owner.
			const char *level;
			int red, green;
			if ( intensity >= 0.25f )
			{
				red = 255;
				if ( intensity >= 0.5f )
				{
					if ( intensity >= 0.75f ) { level = "EXTREME"; green = 0; }
					else { level = "HIGH"; green = 128; }
				}
				else { level = "MODERATE"; green = 255; }
			}
			else { level = "MILD"; red = 0; green = 255; }
			CFmtStrN<256, true> text( "  %s: %3.2f (%s)", player->GetPlayerName(), (double)intensity, level );
			DirectorDebugText( m_x, m_y, m_line, text, red, green, 0, 255, 0.25f );
			++m_line;
			if ( !m_firstPlayer ) m_firstPlayer = player;
		}
	}
private:
	float m_x;
	float m_y;
	int m_line;
	CTerrorPlayer *m_firstPlayer;
};
}

// Native25DB70. Keep actual UTIL_LogPrintf's existing logging gate; this is NOT
// Msg/DevMsg. Message RGB is native0, independently of overlay's supplied RGBA.
void DirectorDebugText( float x, float y, int line, const char *text,
	int red, int green, int blue, int alpha, float duration )
{
	UTIL_LogPrintf( "%3.2f: Director debug: %s.\n", gpGlobals->curtime, text );
	if ( s_pDirectorDebugRecipient )
		TerrorMessageText( s_pDirectorDebugRecipient, text, 0 );
	else
		NDebugOverlay::ScreenTextLine( x, y, line, text, red, green, blue, alpha, duration );
}

CON_COMMAND_F( director_report, "Prints a snapshot of the director's state", 0x00004004 )
{
	// Native26DC50: no previous-recipient save/restore or broadcast fallback.
	s_pDirectorDebugRecipient = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( s_pDirectorDebugRecipient ) TheDirector->DrawDebugText();
	s_pDirectorDebugRecipient = NULL;
}

void CDirector::DrawDebugText()
{
	CFmtStrN<256, true> text;
	int line = m_pScenario->DrawDebugText( 6 );
	line = m_pSurvivalMode->DrawDebugText( line );
	// Native root virtual+24/271B90 is the genuine identity return(line).
	// The actual root allocation is CDirector, not a runtime debug subclass.
	++line;
	if ( m_pScenario->GetPanicStage() == CDirectorScenario::STAGE_DONE )
	{
		DirectorDebugText( 0.01f, 0.0f, line++, "NORMAL MODE", 255, 255, 255, 255, 0.01023f ); // native direct text1
		switch ( m_nIntensityState )
		{
		case 0:
			if ( gpGlobals->curtime >= m_IntensityStateTimer.m_timestamp ) text.sprintf( "  tempo: BUILD_UP" );
			else
			{
				const float remaining = (float)( (double)m_IntensityStateTimer.m_timestamp - (double)gpGlobals->curtime );
				text.sprintf( "  tempo: BUILD_UP (%d seconds remaining)", DebugTruncate( ceil( remaining ) ) );
			}
			DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
			break;
		case 1:
			text.sprintf( "  tempo: SUSTAIN_PEAK (%d seconds remaining)", DebugCeil( m_IntensityStateTimer ) );
			DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
			break;
		case 2:
			text.sprintf( "  tempo: PEAK_FADE (%3.2f max intensity)", (double)GetMaximumSurvivorIntensity() );
			DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
			break;
		case 3:
			text.sprintf( "  tempo: RELAX (%d seconds remaining)", DebugCeil( m_IntensityStateTimer ) );
			DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f );
			break;
		}
		text.sprintf( "  Total Population: %d", TheZombieManager->GetPopulationCounter() );
		DirectorDebugText( 0.01f, 0.0f, line, text, 255, 255, 0, 255, 0.01023f ); //2
	}
	// These five native map rows ALWAYS use overlay, even inside director_report.
	NDebugOverlay::ScreenTextLine( 0.01f, 0.0f, 24, "MAP INFO", 255, 255, 255, 255, 0.01023f );
	text.sprintf( "  Previous Elapsed time: %.0f", (double)m_flMissionElapsedTime );
	NDebugOverlay::ScreenTextLine( 0.01f, 0.0f, 25, text, 255, 255, 0, 255, 0.01023f );
	const double elapsed = m_MissionTimer.m_timestamp > 0.0f ?
		(double)gpGlobals->curtime - (double)m_MissionTimer.m_timestamp : (double)99999.8984375f;
	text.sprintf( "  Current Elapsed time: %.0f", elapsed );
	NDebugOverlay::ScreenTextLine( 0.01f, 0.0f, 26, text, 255, 255, 0, 255, 0.01023f );
	text.sprintf( "  Original map was %s", m_originalMapName );
	NDebugOverlay::ScreenTextLine( 0.01f, 0.0f, 27, text, 255, 255, 0, 255, 0.01023f );
	text.sprintf( "  Map number: %d", m_nCurrentChapterIndex );
	NDebugOverlay::ScreenTextLine( 0.01f, 0.0f, 28, text, 255, 255, 0, 255, 0.01023f );

	DirectorDebugText( 0.01f, 0.0f, 30, "COMMON INFECTED STATS", 255, 255, 255, 255, 0.01023f ); //3
	if ( m_pScenario->GetPanicStage() == CDirectorScenario::STAGE_DONE )
	{
		if ( m_MobSpawnTimer.m_timestamp > 0.0f )
		{
			const float seconds = (float)ceil( (double)m_MobSpawnTimer.m_timestamp - (double)gpGlobals->curtime );
			const int size = DebugTruncate( GetDirectorOptionFloat( "MobSpawnSize", m_flMobSize ) );
			text.sprintf( "  Next mob size: %d (in %d seconds)", size, _mm_cvttss_si32( _mm_set_ss( seconds ) ) );
		}
		else text.sprintf( "  Next mob size: %d", DebugTruncate( GetDirectorOptionFloat( "MobSpawnSize", m_flMobSize ) ) );
		DirectorDebugText( 0.01f, 0.0f, 31, text, 255, 255, 0, 255, 0.01023f );
	}
	else DirectorDebugText( 0.01f, 0.0f, 31, "  Next mob size: N/A (in panic mode)", 255, 255, 0, 255, 0.01023f ); //4
	const int commonLimit = GetCommonLimit(); // Native query BEFORE actual population visitor.
	const int population = GetCommonInfectedCount( 0 );
	text.sprintf( "  Population: %d (out of %d max)", population, commonLimit );
	DirectorDebugText( 0.01f, 0.0f, 32, text, 255, 255, 0, 255, 0.01023f ); //5
	text.sprintf( "  Pending count: %d", TheZombieManager->GetPendingMobCount() );
	DirectorDebugText( 0.01f, 0.0f, 33, text, 255, 255, 0, 255, 0.01023f ); //6
	const int areaCount = TheZombieManager->GetMobSpawnAreaCount();
	if ( areaCount != 0 )
	{
		TerrorNavArea *areas[4] = { NULL, NULL, NULL, NULL };
		for ( int i = 0; i < areaCount && i < 4; ++i ) areas[i] = TheZombieManager->GetMobSpawnArea( i );
		text.sprintf( "  Spawn areas: #%d, #%d, #%d, #%d %s", areas[0] ? areas[0]->GetID() : 0,
			areas[1] ? areas[1]->GetID() : 0, areas[2] ? areas[2]->GetID() : 0, areas[3] ? areas[3]->GetID() : 0,
			areaCount > 4 ? "more..." : "" );
		DirectorDebugText( 0.01f, 0.0f, 34, text, 255, 255, 0, 255, 0.01023f ); //7
	}
	else DirectorDebugText( 0.01f, 0.0f, 34, "No mob spawn areas", 255, 255, 0, 255, 0.01023f ); //8

	line = m_pSessionManager->DrawDebugText( 36 );
	DirectorDebugText( 0.01f, 0.0f, line++, "SURVIVOR STATS", 255, 255, 255, 255, 0.01023f ); //9
	if ( m_pScenario->AreSurvivorsInFinaleAreaBeforePanic() )
		DirectorDebugText( 0.01f, 0.0f, line++, "  In a Battlefield Area", 255, 255, 0, 255, 0.01023f ); //10
	text.sprintf( "  %s survivors have left the starting safe area", m_bHasAnySurvivorLeftSafeArea ? ( m_bHasAllSurvivorsLeftSafeArea ? "All" : "Some" ) : "No" );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //11
	text.sprintf( "  %sIN COMBAT", IsSurvivorInCombat() ? "" : "NOT " );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //12
	text.sprintf( "  Flow span: %d", _mm_cvttss_si32( _mm_set_ss( m_flSurvivorFlowSpan ) ) );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //13
	text.sprintf( "  Furthest flow: %d", _mm_cvttss_si32( _mm_set_ss( m_flMaximumSurvivorFlow ) ) );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //14
	text.sprintf( "  Flow/s: %d", _mm_cvttss_si32( _mm_set_ss( m_flSurvivorMovement ) ) );
	DirectorDebugText( 0.01f, 0.0f, line, text, 255, 255, 0, 255, 0.01023f ); //15

	TerrorInfectedClassCounts counts( false );
	counts.Count();
	if ( TerrorGameRules() && TerrorGameRules()->IsScriptedModeActive() )
		text.sprintf( "SPECIALS (%d max) - being run by Challenge Mode", GetMaxSpecialCount() );
	else if ( !m_bHasAnySurvivorLeftSafeArea )
		text.sprintf( "SPECIALS (%d max) - Not spawning - all players still in safe area.", GetMaxSpecialCount() );
	else if ( m_nTankCount > 0 && !( m_pScenario->GetFinaleType() == 1 && m_pScenario->GetFinaleStage() == CDirectorScenario::FINALE_GAUNTLET_ESCAPE ) )
		text.sprintf( "SPECIALS (%d max) - Not spawning - tank in play.", GetMaxSpecialCount() );
	else if ( m_pScenario->AreSurvivorsInFinaleAreaBeforePanic() && m_pScenario->GetPanicStage() == CDirectorScenario::STAGE_DONE &&
		m_pScenario->GetFinaleStage() != CDirectorScenario::FINALE_CUSTOM_SCRIPTED && m_pScenario->GetFinaleType() != 1 )
		text.sprintf( "SPECIALS (%d max) - Not spawning - in battlefield with panic event cleared.", GetMaxSpecialCount() );
	else if ( ::AreAllSurvivorsInFinaleArea( true ) && m_pScenario->GetFinaleType() == 5 )
		text.sprintf( "SPECIALS (%d max) - Not spawning - setting up for finale.", GetMaxSpecialCount() );
	else text.sprintf( "SPECIALS (%d max)", GetMaxSpecialCount() );
	DirectorDebugText( 0.01f, 0.0f, 44, text, 255, 255, 255, 255, 0.01023f ); //16 (native common branch; two helper-routed alternatives)
	line = 45;
	int total = 0;
	for ( int i = 0; i < 6; ++i )
	{
		int seconds = DebugCeil( m_BaseSpecialSpawnTimers[i] );
		if ( seconds >= 0 ) seconds = DebugCeil( m_BaseSpecialSpawnTimers[i] );
		else seconds = 0;
		text.sprintf( "  [%02ds] %d %sS", seconds, counts.special[i], GetZombieClassName( i + 1 ) );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //17
		total += counts.special[i];
	}
	const bool tooMany = total > GetMaxSpecialCount();
	text.sprintf( "  Total Specials %d", total );
	DirectorDebugText( 0.01f, 0.0f, line++, text, 255, tooMany ? 0 : 255, tooMany ? 0 : 255, 255, 0.01023f ); //18
	for ( int i = 0; ; ++i )
	{
		// Preserve repeated max-special queries, not a cached replacement cap.
		const int cap = GetMaxSpecialCount() <= 3 ? 3 : GetMaxSpecialCount();
		if ( i >= cap || i >= m_SpecialSpawnTimers.Count() ) break;
		int seconds = DebugCeil( m_SpecialSpawnTimers[i] );
		if ( seconds >= 0 && ( seconds = DebugCeil( m_SpecialSpawnTimers[i] ) ) != 0 )
		{
			if ( seconds >= 600 ) text.sprintf( "  Slot #%d: Currently active.", i );
			else text.sprintf( "  Slot #%d: %d seconds remaining for respawn.", i, seconds );
		}
		else text.sprintf( "  Slot #%d: Slot available.", i );
		DirectorDebugText( 0.01f, 0.0f, line++, text, 255, 255, 0, 255, 0.01023f ); //19
	}
	const int intensityLine = GetMaxSpecialCount() <= 3 ? 3 : GetMaxSpecialCount();
	SurvivorIntensityDebug intensity( intensityLine + 52 );
	intensity.Draw();
	FOR_EACH_VEC( m_carAlarms, i )
	{
		CBaseEntity *entity = m_carAlarms[i].Get();
		if ( entity )
		{
			NDebugOverlay::EntityBounds( entity, 255, 0, 0, 10, 0.01023f );
			NDebugOverlay::EntityText( entity->entindex(), 0, "FORBIDDEN TARGET", 0.01023f, 255, 255, 255, 255 );
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#include "l4d2/director/director_frame_console_server_callbacks.inc"
