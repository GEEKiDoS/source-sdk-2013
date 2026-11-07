//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_retreat_path.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/player/bots/survivor_legs_tuning.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "nav_ladder.h"
#include "ndebugoverlay.h"
#include "tier0/memdbgon.h"

SurvivorRetreatPathBuilder::SurvivorRetreatPathBuilder( SurvivorBot *me, CBaseEntity *threat, float retreatRange ) :
	m_me( me ), m_mover( me->GetLocomotionInterface() ),
	m_toThreat( threat->GetAbsOrigin() - me->GetAbsOrigin() ), m_threat( threat ),
	m_retreatRange( retreatRange ), m_adjacentCount( 0 )
{
	m_toThreat.NormalizeInPlace();
}

// Original server 103BC840. Keep N,S,W,E and ladder endpoint order: the
// fixed 64-entry limit and equal-cost open-list ordering are observable.
void SurvivorRetreatPathBuilder::AddAdjacentArea( CNavArea *area, CNavLadder *ladder, NavTraverseType how )
{
	if ( area && m_adjacentCount < MAX_ADJ_AREAS )
	{
		m_adjacent[m_adjacentCount].area = area;
		m_adjacent[m_adjacentCount].ladder = ladder;
		m_adjacent[m_adjacentCount].how = how;
		++m_adjacentCount;
	}
}

void SurvivorRetreatPathBuilder::CollectAdjacentAreas( CNavArea *area )
{
	m_adjacentCount = 0;
	const NavDirType directions[] = { NORTH, SOUTH, WEST, EAST };
	for ( int direction = 0; direction < ARRAYSIZE( directions ); ++direction )
	{
		const NavConnectVector &adjacent = *area->GetAdjacentAreas( directions[direction] );
		FOR_EACH_VEC( adjacent, i )
		{
			if ( m_adjacentCount >= MAX_ADJ_AREAS )
				break;
			AddAdjacentArea( adjacent[i].area, NULL, static_cast< NavTraverseType >( directions[direction] ) );
		}
	}
	const NavLadderConnectVector &up = *area->GetLadders( CNavLadder::LADDER_UP );
	FOR_EACH_VEC( up, i )
	{
		CNavLadder *ladder = up[i].ladder;
		AddAdjacentArea( ladder->m_topForwardArea, ladder, GO_LADDER_UP );
		AddAdjacentArea( ladder->m_topLeftArea, ladder, GO_LADDER_UP );
		AddAdjacentArea( ladder->m_topRightArea, ladder, GO_LADDER_UP );
	}
	const NavLadderConnectVector &down = *area->GetLadders( CNavLadder::LADDER_DOWN );
	FOR_EACH_VEC( down, i )
	{
		if ( m_adjacentCount >= MAX_ADJ_AREAS )
			break;
		CNavLadder *ladder = down[i].ladder;
		AddAdjacentArea( ladder->m_bottomArea, ladder, GO_LADDER_DOWN );
	}
}

// Original server 103BCAF0. CostSoFar holds the most recent danger density,
// not cumulative distance. Do not replace it with the generic retreat cost.
float SurvivorRetreatPathBuilder::Cost( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder )
{
	if ( !m_mover->IsAreaTraversable( area ) )
		return -1.0f;
	const float areaWidth = ( area->GetSizeY() + area->GetSizeX() ) * 0.5f;
	float cost;
	if ( !fromArea )
	{
		cost = 0.0f;
		if ( area->Contains( m_threat->GetAbsOrigin() ) )
		{
			cost = 1000.0f;
			if ( sb_debug_retreat.GetBool() )
				area->DrawFilled( 255, 0, 0, 128, 0.1f, true );
		}
		else
		{
			const float range = ( m_threat->GetAbsOrigin() - m_me->GetAbsOrigin() ).Length();
			if ( range < 500.0f )
			{
				cost = ( 1.0f - range * 0.0020000001f ) * 1000.0f;
				if ( sb_debug_retreat.GetBool() )
					NDebugOverlay::Line( m_me->GetAbsOrigin(), m_threat->GetAbsOrigin(), 255, 0, 0, true, 3.0f );
			}
		}
		area->SetCostSoFar( cost );
	}
	else
	{
		float distance;
		if ( ladder )
		{
			distance = ladder->m_length * 100.0f;
		}
		else
		{
			distance = ( area->GetCenter() - fromArea->GetCenter() ).Length();
			const float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );
			if ( deltaZ > m_mover->GetMaxJumpHeight() || -deltaZ > m_mover->GetDeathDropHeight() )
				return -1.0f;
			distance = fabsf( deltaZ ) * 10.0f + distance;
		}
		cost = fromArea->GetTotalCost() + distance;
		float dangerCost = fromArea->GetCostSoFar();
		Vector closest;
		float t;
		CalcClosestPointOnLineSegment( m_threat->GetAbsOrigin(), area->GetCenter(), fromArea->GetCenter(), closest, &t );
		if ( t < 0.0f )
			closest = area->GetCenter();
		else if ( t > 1.0f )
			closest = fromArea->GetCenter();
		const float range = ( m_threat->GetAbsOrigin() - closest ).Length();
		if ( range < 500.0f )
		{
			const float dangerFactor = 1.0f - range * 0.0020000001f;
			dangerCost = dangerFactor * 1000.0f;
			if ( sb_debug_retreat.GetBool() )
			{
				NDebugOverlay::HorzArrow( fromArea->GetCenter(), area->GetCenter(), 5.0f, static_cast< int >( dangerFactor * 255.0f ), 0, 0, 255, true, 3.0f );
				Vector away = closest - m_threat->GetAbsOrigin();
				away.NormalizeInPlace();
				NDebugOverlay::Line( closest, closest - away * 50.0f, 255, 0, 0, true, 3.0f );
			}
		}
		cost = dangerCost + cost;
		area->SetCostSoFar( dangerCost );
	}
	const unsigned char infectedCount = area->GetPlayerCount( TEAM_INFECTED );
	if ( infectedCount && areaWidth >= 1.0f )
		return static_cast< float >( infectedCount ) * 50000.0f / areaWidth + cost;
	return cost;
}

// Original server 103BD0C0. Native search deliberately rejects the threat's
// entire Place, including an unnamed Place, and never expands toward it.
CNavArea *SurvivorRetreatPathBuilder::ComputePath()
{
	if ( !m_mover )
		return NULL;
	CNavArea *start = m_me->GetLastKnownArea();
	if ( !start )
		return NULL;
	if ( !TheNavMesh->GetNearestNavArea( m_threat->GetAbsOrigin(), false, 10000.0f, false, true, TEAM_ANY ) )
		return NULL;
	start->SetParent( NULL );
	CNavArea *threatArea = TheNavMesh->GetNavArea( m_threat, GETNAVAREA_CHECK_LOS, 120.0f );
	CNavArea::ClearSearchLists();
	const float initialCost = Cost( start, NULL, NULL );
	if ( initialCost < 0.0f )
		return NULL;
	const int team = m_me->GetTeamNumber();
	start->SetTotalCost( initialCost );
	start->AddToOpenList();
	CNavArea *farthest = NULL;
	float farthestRange = 0.0f;
	SurvivorBotPathCost traversableCost( m_me, true );
	while ( !CNavArea::IsOpenListEmpty() )
	{
		CNavArea *area = CNavArea::PopOpenList();
		area->AddToClosedList();
		if ( area->IsBlocked( team ) || ( threatArea && area->GetPlace() == threatArea->GetPlace() ) )
			continue;
		CollectAdjacentAreas( area );
		for ( int i = 0; i < m_adjacentCount; ++i )
		{
			CNavArea *next = m_adjacent[i].area;
			if ( next->IsClosed() && !next->IsOpen() )
				continue;
			if ( traversableCost( next, area, NULL, NULL, 1.0f ) < 0.0f )
				continue;
			if ( ( next->GetCenter() - m_me->GetAbsOrigin() ).LengthSqr() > m_retreatRange * m_retreatRange )
				continue;
			if ( -m_mover->GetMaxJumpHeight() > area->ComputeGroundHeightChange( next ) )
				continue;
			const float cost = Cost( next, area, m_adjacent[i].ladder );
			if ( cost < 0.0f )
				continue;
			if ( DotProduct( m_toThreat, next->GetCenter() - m_threat->GetAbsOrigin() ) > 0.0f )
				continue;
			if ( next->IsOpen() && cost >= next->GetTotalCost() )
				continue;
			next->SetParent( area, m_adjacent[i].how );
			next->SetTotalCost( cost );
			if ( next->IsOpen() )
				next->UpdateOnOpenList();
			else
				next->AddToOpenList();
			const float range = ( next->GetCenter() - m_threat->GetAbsOrigin() ).Length();
			if ( range > farthestRange )
			{
				farthest = next;
				farthestRange = range;
			}
		}
	}
	return farthest;
}
