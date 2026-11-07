//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/nav/escape_route.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/terror_shareddefs.h"
#include "nav_ladder.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/nav/terror_nav_search.h"
#include "debugoverlay_shared.h"
#include <emmintrin.h>
#include "l4d2/director/zombiemanager_escape_route_console_server_cvars.inc"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar nav_flow_max_survivor_climb_height;
extern ConVar nav_flow_max_survivor_drop_height;
extern ConVar director_debug_threat_placement;
static ConVar director_threat_min_separation( "director_threat_min_separation", "5000", 0x00004004 );
static ConVar director_threat_max_separation( "director_threat_max_separation", "5000", 0x00004004 );
static ConVar director_max_threat_areas( "director_max_threat_areas", "4", 0x00004004 );
static ConVar vs_threat_initial_distance_first_map_min( "vs_threat_initial_distance_first_map_min", "8000", 0x00004004, "Min distance of starting threat area in vs mode on the first map" );
static ConVar vs_threat_initial_distance_first_map_max( "vs_threat_initial_distance_first_map_max", "12000", 0x00004004, "Max distance of starting threat area in vs mode on the first map" );
static ConVar vs_threat_initial_distance_min( "vs_threat_initial_distance_min", "3000", 0x00004004, "Min distance of starting threat area in vs mode" );
static ConVar vs_threat_initial_distance_max( "vs_threat_initial_distance_max", "8000", 0x00004004, "Max distance of starting threat area in vs mode" );
ConVar z_debug_escape_route( "z_debug_escape_route", "0", 0x00004004, "If nonzero, display information when escape route fails" );
int g_DebugPathfindCounter; // Original10875594, zero-initialized process storage.

namespace
{
float EscapePathDistance( const Vector &first, const Vector &second )
{
	float x = first.x - second.x;
	float y = first.y - second.y;
	float z = first.z - second.z;
	return sqrtf( ( y * y + z * z ) + x * x );
}

// A failed native A* may retain a cyclic parent chain. The bounded route
// sampler still keeps its 1024-area native limit; only the independently
// unbounded population walk applies the standing stop-on-first-revisit rule.
// Floyd detection adds no per-area allocation or growing linear membership scan.
CNavArea *FindEscapeParentCycleEntry( CNavArea *goal )
{
	CNavArea *slow = goal;
	CNavArea *fast = goal;
	do
	{
		if ( !fast || !fast->GetParent() ) return NULL;
		slow = slow->GetParent();
		fast = fast->GetParent()->GetParent();
	} while ( slow != fast );
	slow = goal;
	while ( slow != fast )
	{
		slow = slow->GetParent();
		fast = fast->GetParent();
	}
	return slow;
}

// S:3591B0. This is not the rescue or generic SDK cost functor: ladders
// cost100x, and dark areas use the original quantized average-light byte.
double EscapePathCost( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
	const CFuncElevator *elevator, float length )
{
	if ( !from ) return 0.0;
	if ( area->GetAttributes() & 0x30000 ) return -1.0;
	if ( ladder )
	{
		if ( !ladder->IsUsableByTeam( TEAM_SURVIVOR ) ) return -1.0;
		length = ladder->m_length * 100.0f;
	}
	else
	{
		if ( !( length > 0.0f ) ) length = EscapePathDistance( area->GetCenter(), from->GetCenter() );
		if ( !elevator )
		{
			double originalHeight = from->ComputeAdjacentConnectionHeightChange( area );
			float height = (float)originalHeight;
			if ( originalHeight > (double)nav_flow_max_survivor_climb_height.GetFloat() ||
				-height > nav_flow_max_survivor_drop_height.GetFloat() ) return -1.0;
			if ( height > 18.0f ) length *= 2.0f;
			else if ( height < -18.0f ) length = ( ( -height - 18.0f ) * 0.012195121f + 1.0f ) * length;
		}
		// Native4760C0's x87 result is compared BEFORE rounding to float.
		double light = area->GetLightIntensity();
		if ( light < (double)0.4f )
		{
			float lightFloat = (float)light;
			length = ( ( ( 0.4f - lightFloat ) * 2.0f ) * 2.5f + 1.0f ) * length;
		}
	}
	return (double)from->GetCostSoFar() + (double)length;
}

// S:35BFC0, the actual specialization reached by35C990: center endpoints,
// no closest-area output, no path-length limit, survivor team, ignore blockers.
// The SDK template adds a parent-edge skip, NaN repair and minimum-cost clamp
// which are NOT present in this original search.
bool BuildEscapePath( CNavArea *start, CNavArea *goal )
{
	bool draw = g_DebugPathfindCounter-- > 0;
	if ( !start || !goal || goal->IsBlocked( TEAM_SURVIVOR, true ) ) return false;
	start->SetParent( NULL );
	if ( start == goal ) return true;
	const Vector &goalPosition = goal->GetCenter();
	CNavArea::ClearSearchLists();
	start->SetTotalCost( ( start->GetCenter() - goalPosition ).Length() );
	start->SetCostSoFar( 0.0f );
	start->SetPathLengthSoFar( 0.0f );
	start->AddToOpenList();
	while ( !CNavArea::IsOpenListEmpty() )
	{
		CNavArea *area = CNavArea::PopOpenList();
		if ( draw ) area->DrawFilled( 0, 255, 0, 128, 30.0f, true );
		if ( area->IsBlocked( TEAM_SURVIVOR, true ) ) continue;
		if ( area == goal ) return true;
		auto visit = [&]( CNavArea *next, NavTraverseType how, const CNavLadder *ladder,
			const CFuncElevator *elevator, float length )
		{
			if ( next == area || next->IsBlocked( TEAM_SURVIVOR, true ) ) return;
			double originalCost = EscapePathCost( next, area, ladder, elevator, length );
			float cost = (float)originalCost;
			if ( originalCost < 0.0 ) return;
			if ( ( next->IsOpen() || next->IsClosed() ) && cost >= next->GetCostSoFar() ) return;
			Vector delta = next->GetCenter() - goalPosition;
			float distanceSquared = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
			float remaining = 0.0f;
			if ( distanceSquared > 0.0f )
			{
				float reciprocal = _mm_cvtss_f32( _mm_rsqrt_ss( _mm_set_ss( distanceSquared ) ) );
				remaining = reciprocal * distanceSquared;
			}
			next->SetCostSoFar( cost );
			next->SetTotalCost( remaining + cost );
			if ( next->IsOpen() ) next->UpdateOnOpenList();
			else next->AddToOpenList();
			next->SetParent( area, how );
		};
		for ( int direction = 0; direction < NUM_DIRECTIONS; ++direction )
		{
			const NavConnectVector *outgoing = area->GetAdjacentAreas( static_cast< NavDirType >( direction ) );
			FOR_EACH_VEC( *outgoing, i )
			{
				CNavArea *next = ( *outgoing )[i].area;
				float length = ( *outgoing )[i].length;
				if ( area == start ) length = EscapePathDistance( next->GetCenter(), start->GetCenter() );
				else if ( next == goal ) length = EscapePathDistance( goalPosition, area->GetCenter() );
				visit( next, static_cast< NavTraverseType >( direction ), NULL, NULL, length );
			}
		}
		const NavLadderConnectVector *ladders = area->GetLadders( CNavLadder::LADDER_UP );
		FOR_EACH_VEC( *ladders, i )
		{
			const CNavLadder *ladder = ( *ladders )[i].ladder;
			if ( ladder->m_topForwardArea ) visit( ladder->m_topForwardArea, GO_LADDER_UP, ladder, NULL, -1.0f );
			if ( ladder->m_topLeftArea ) visit( ladder->m_topLeftArea, GO_LADDER_UP, ladder, NULL, -1.0f );
			if ( ladder->m_topRightArea ) visit( ladder->m_topRightArea, GO_LADDER_UP, ladder, NULL, -1.0f );
		}
		ladders = area->GetLadders( CNavLadder::LADDER_DOWN );
		FOR_EACH_VEC( *ladders, i )
		{
			const CNavLadder *ladder = ( *ladders )[i].ladder;
			if ( ladder->m_bottomArea ) visit( ladder->m_bottomArea, GO_LADDER_DOWN, ladder, NULL, -1.0f );
		}
		if ( area->HasAttributes( NAV_MESH_HAS_ELEVATOR ) && area->GetElevator() )
		{
			const NavConnectVector &elevators = area->GetElevatorAreas();
			FOR_EACH_VEC( elevators, i )
			{
				CNavArea *next = elevators[i].area;
				visit( next, next->GetCenter().z > area->GetCenter().z ? GO_ELEVATOR_UP : GO_ELEVATOR_DOWN,
					NULL, area->GetElevator(), -1.0f );
			}
		}
		area->AddToClosedList();
	}
	return false;
}
}

// S:35C990. Find FIRST map entity; the singleton getter deliberately remains
// the MOST RECENT constructor. Multiple authored escape_route entities keep
// the original first/last asymmetry. Never substitute mesh endpoints/coords.
void BuildEscapeRoute( TerrorNavArea *start )
{
	CEscapeRoute *route = static_cast< CEscapeRoute * >( gEntList.FindEntityByClassname( NULL, "escape_route" ) );
	if ( !route ) route = static_cast< CEscapeRoute * >( CBaseEntity::Create( "escape_route", vec3_origin, vec3_angle, NULL ) );
	route->Clear();
	TerrorNavArea *goal = TheTerrorNavMesh()->GetGoalArea();
	if ( start && goal )
	{
		if ( z_debug_escape_route.GetBool() ) g_DebugPathfindCounter = 1;
		if ( !BuildEscapePath( start, goal ) )
		{
			Warning( "ERROR: BuildEscapeRoute failed. No path from start to end. zeroFlowArea = #%d, endArea = #%d.\n", start->GetID(), goal->GetID() );
			if ( z_debug_escape_route.GetBool() )
			{
				TheNavMesh->AddToSelectedSet( start );
				TheNavMesh->AddToSelectedSet( goal );
			}
		}
		CNavArea *area = goal;
		int visited = 0;
		for ( ;; )
		{
			++visited;
			route->AddArea( ToTerrorNavArea( area ) );
			if ( z_debug_escape_route.GetBool() ) TheNavMesh->AddToSelectedSet( area );
			if ( visited >= 1024 ) break;
			area = area->GetParent();
			if ( !area )
			{
				route->Finish();
				return;
			}
		}
		Warning( "ERROR: BuildEscapeRoute failed. Distance to end of map is too far. Infected population incomplete. zeroFlowArea = #%d, endArea = #%d.\n", start->GetID(), goal->GetID() );
		if ( z_debug_escape_route.GetBool() )
		{
			TheNavMesh->AddToSelectedSet( start );
			TheNavMesh->AddToSelectedSet( goal );
		}
		route->Finish();
	}
	else if ( start )
	{
		Warning( "ERROR: BuildEscapeRoute failed. The end area was NULL. zeroFlowArea = #%p\n", start );
		if ( z_debug_escape_route.GetBool() ) TheNavMesh->AddToSelectedSet( start );
	}
	else if ( goal )
	{
		Warning( "ERROR: BuildEscapeRoute failed. The start area was NULL. endArea = #%p\n", goal );
		if ( z_debug_escape_route.GetBool() ) TheNavMesh->AddToSelectedSet( goal );
	}
	else Warning( "ERROR: BuildEscapeRoute failed. The start and end area were both NULL\n" );
}

void ZombieManager::OnFlowDistancesRecomputed( TerrorNavArea *start )
{
	DevMsg( "%3.2f: OnFlowDistancesRecomputed()\n", gpGlobals->curtime );
	BuildEscapeRoute( start );
}

// S:35F370. This is the complete actual initial-flow callback, not a route-only
// stand-in: threat classification and the area292 population producer follow
// the real escape-route search and retain its parent-chain side effects.
void ZombieManager::OnFlowDistancesInitialized( TerrorNavArea *start )
{
	DevMsg( "%3.2f: OnFlowDistancesInitialized()\n", gpGlobals->curtime );
	TerrorNavArea *goal = TheTerrorNavMesh()->GetGoalArea();
	if ( !goal )
	{
		Warning( "ERROR: OnFlowDistancesInitialized failed. The end area was NULL\n" );
		return;
	}
	FOR_EACH_VEC( TheNavAreas, i )
	{
		TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[i] );
		area->RemoveSpawnAttributes( TERROR_NAV_THREAT );
		area->SetWanderingPopulation( 0 );
	}
	BuildEscapeRoute( start );
	if ( start )
	{
		CUtlVector< CNavArea * > path;
		CNavArea *cycleEntry = FindEscapeParentCycleEntry( goal );
		bool visitedCycleEntry = false;
		for ( CNavArea *area = goal; area; area = area->GetParent() )
		{
			if ( area == cycleEntry )
			{
				if ( visitedCycleEntry ) break;
				visitedCycleEntry = true;
			}
			path.AddToTail( area );
		}
		// Native repeatedly inserts at the head. Reverse once instead of
		// copying the growing array on every insertion; pointer order is the
		// same before any geometry, options, RNG or callback is evaluated.
		for ( int i = 0; i < path.Count() / 2; ++i ) V_swap( path[i], path[path.Count() - i - 1] );
		float spawnRange = GetZombieSpawnRange();
		float initialMaximumSeparation = director_threat_max_separation.GetFloat();
		float random = RandomFloat( 0.0f, 1.0f );
		float maximumSeparation = director_threat_max_separation.GetFloat();
		float minimumSeparation = director_threat_min_separation.GetFloat();
		float remaining = (float)( (double)random * (double)initialMaximumSeparation +
			( (double)spawnRange + (double)spawnRange ) );
		float length = 0.0f;
		CNavArea *debugArea = NULL;
		for ( int i = 1; i < path.Count(); ++i )
		{
			Vector delta = path[i]->GetCenter() - path[i - 1]->GetCenter();
			float segment = sqrtf( ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z );
			length += segment;
			debugArea = path[i];
		}
		int maximumThreats = director_max_threat_areas.GetInt();
		// Native truncates length to int and BACK to float before dividing.
		// CVTT preserves the pinned nonfinite/out-of-range conversion result.
		int integralLength = _mm_cvttss_si32( _mm_set_ss( length ) );
		float meanSeparation = ( maximumSeparation + minimumSeparation ) * 0.5f;
		int threatCount = _mm_cvttss_si32( _mm_set_ss( (float)integralLength / meanSeparation ) );
		if ( threatCount > maximumThreats )
		{
			threatCount = maximumThreats;
			minimumSeparation = maximumSeparation = length / (float)maximumThreats;
		}
		if ( TerrorGameRules()->IsVersusMode() )
		{
			if ( TheDirector->GetChapterPanicWaveTier() != 0 )
				remaining = RandomFloat( vs_threat_initial_distance_min.GetFloat(), vs_threat_initial_distance_max.GetFloat() );
			else
				remaining = RandomFloat( vs_threat_initial_distance_first_map_min.GetFloat(), vs_threat_initial_distance_first_map_max.GetFloat() );
		}
		else
		{
			minimumSeparation = (float)( (double)RandomFloat( 0.8f, 1.0f ) * (double)minimumSeparation );
			maximumSeparation = (float)( (double)RandomFloat( 1.0f, 1.2f ) * (double)maximumSeparation );
		}
		CUtlVectorFixedGrowable< TerrorNavArea *, 8 > threats;
		int placed = 0;
		for ( int i = 1; i < path.Count(); ++i )
		{
			CNavArea *area = path[i];
			Vector delta = area->GetCenter() - path[i - 1]->GetCenter();
			float segment = sqrtf( ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z );
			remaining -= segment;
			if ( remaining <= 0.0f )
			{
				struct CollectCandidates
				{
					CUtlVectorFixedGrowable< CNavArea *, 256 > m_areas;
					bool operator()( TerrorNavArea *candidate ) { m_areas.AddToTail( candidate ); return true; }
				} candidates;
				CollectSurroundingTerrorAreas( area, area->GetCenter(), candidates, 240.0f, 0, TEAM_ANY );
				// Original is NOT Fisher-Yates: every iteration uses [0,n-1].
				FOR_EACH_VEC( candidates.m_areas, j )
				{
					int other = RandomInt( 0, candidates.m_areas.Count() - 1 );
					V_swap( candidates.m_areas[j], candidates.m_areas[other] );
				}
				FOR_EACH_VEC( candidates.m_areas, j )
				{
					TerrorNavArea *candidate = dynamic_cast< TerrorNavArea * >( candidates.m_areas[j] );
					// Native immediately dereferences this actual-area cast.
					// Its ordered-negative tests also admit unordered sizes.
					if ( candidate->HasSpawnAttributes( TERROR_NAV_NOTHREAT ) ||
						candidate->GetSizeX() < 24.0f || candidate->GetSizeY() < 24.0f ) continue;
					threats.AddToTail( candidate );
					++placed;
					remaining = RandomFloat( minimumSeparation, maximumSeparation );
					break;
				}
			}
			// This comparison is AFTER the first iteration even when zero.
			if ( placed == threatCount ) break;
		}
		FOR_EACH_VEC( threats, i )
		{
			threats[i]->AddSpawnAttributes( TERROR_NAV_THREAT );
			if ( director_debug_threat_placement.GetBool() )
			{
				// Original35F8BE uses the last LENGTH-loop area, not threats[i].
				const Vector &origin = debugArea->GetCenter();
				Vector top = origin + Vector( 0.0f, 0.0f, 1000.0f );
				NDebugOverlay::VertArrow( top, origin, 20.0f, 0, 255, 0, 255, true, 9999.9f );
			}
		}
		if ( director_debug_threat_placement.GetBool() ) DevMsg( "Escape route length is %f\n", length );
		double density = TheDirector->GetWanderingZombieDensityModifier();
		// Original55ED50's supported SSE2 path spills to DOUBLE, then uses
		// CVTTSD2SI EAX32. Do not first round this expression to float.
		int count = _mm_cvttsd_si32( _mm_set_sd( density * (double)length + 0.5 ) );
		int remainingPopulation = count;
		int rejected = 0;
		while ( remainingPopulation > 0 && rejected < 1000 )
		{
			TerrorNavArea *area = ToTerrorNavArea( TheNavAreas[RandomInt( 0, TheNavAreas.Count() - 1 )] );
			if ( area->IsValidForWanderingPopulation() )
			{
				unsigned int bits = (unsigned int)area->GetWanderingPopulation() + 1U;
				int population;
				Q_memcpy( &population, &bits, sizeof( population ) );
				area->SetWanderingPopulation( population < 0 ? 0 : population );
				--remainingPopulation;
				rejected = 0;
			}
			else ++rejected;
		}
		if ( director_debug_threat_placement.GetBool() ) DevMsg( "Total wandering zombie population = %d wanderers\n", count );
	}
	else Warning( "ERROR: OnFlowDistancesInitialized failed. The start and/or end area was NULL\n" );
	if ( !TheDirector->IsBackgroundMap() ) TheTerrorNavMesh()->MaintainWanderingPopulation( true );
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
