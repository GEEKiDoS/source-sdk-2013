//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_bot_path_cost.h"
#include "l4d2/player/bots/survivor_team_situation.h"
#include "l4d2/nav/terror_nav_area.h"
#include "nav_ladder.h"
#include "l4d2/entities/func_elevator.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "tier0/memdbgon.h"

SurvivorBotPathCost::SurvivorBotPathCost( SurvivorBot *bot, bool shortestPath )
	: m_bot( bot ), m_locomotion( bot->GetLocomotionInterface() ),
	  m_allowDamagingAreas( !shortestPath && bot->GetIntentionInterface()->ShouldHurry( bot ) != ANSWER_NO ),
	  m_shortestPath( shortestPath )
{
}

// S38DEE0. The apparently gratuitous area-ID/actor-ID cosine is native route
// variation, not a random draw. Do not replace it with a new RNG or danger score.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
float SurvivorBotPathCost::operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *elevator, float length ) const
{
	if ( !m_locomotion->IsAreaTraversable( area ) ||
		( !m_shortestPath && area->IsDamaging() && !m_allowDamagingAreas ) )
		return -1.0f;
	if ( !fromArea )
		return 0.0f;
	if ( ladder )
		return ladder->IsUsableByTeam( TEAM_SURVIVOR ) ? fromArea->GetCostSoFar() + 2.0f * ladder->m_length : -1.0f;

	float distance = length > 0.0f ? length : ( area->GetCenter() - fromArea->GetCenter() ).Length();
	if ( !elevator )
	{
		const float height = fromArea->ComputeAdjacentConnectionHeightChange( area );
		const float step = m_locomotion->GetStepHeight();
		const float jump = m_locomotion->GetMaxJumpHeight();
		if ( height > step + jump || -height > m_locomotion->GetDeathDropHeight() )
			return -1.0f;
		if ( !m_shortestPath )
		{
			if ( height > step )
				distance *= height <= jump ? 2.0f : 100.0f;
			else if ( height < -step )
			{
				const float numerator = static_cast< float >( -static_cast< double >( height ) - static_cast< double >( step ) );
				distance = static_cast< float >( ( static_cast< double >( numerator ) / ( 100.0 - static_cast< double >( step ) ) + 1.0 ) * static_cast< double >( distance ) );
			}
		}
	}
	if ( m_shortestPath )
		return fromArea->GetCostSoFar() + distance;

	float darkCost = 1.0f;
	const float light = area->GetLightIntensity();
	if ( light < 0.4f )
		darkCost += ( 0.4f - light ) * 2.0f * 2.5f;
	const float damageCost = area->IsDamaging() ? 100.0f : 1.0f;
	float obstacleCost = 1.0f;
	if ( area->HasAvoidanceObstacle( 18.0f ) )
	{
		const float height = area->GetAvoidanceObstacleHeight();
		obstacleCost = height >= m_locomotion->GetMaxJumpHeight() ? 100.0f :
			RemapVal( height, m_locomotion->GetStepHeight(), m_locomotion->GetMaxJumpHeight(), 10.0f, 20.0f );
	}

	SurvivorTeamSituation *team = &m_bot->GetTeamSituation();
	float unseenByHumansCost = 1.0f;
	int i;
	for ( i = 0; i < team->GetTeammateCount(); ++i )
	{
		CTerrorPlayer *player = team->GetTeammate( i );
		if ( !player->IsBot() && ToTerrorNavArea( area )->IsCompletelyVisibleByCharacter( player->GetSurvivorCharacter() ) )
			break;
	}
	if ( team->GetTeammateCount() != 0 && i == team->GetTeammateCount() )
		unseenByHumansCost = 10.0f;

	const float penalty = MAX( MAX( darkCost, obstacleCost ), MAX( damageCost, unseenByHumansCost ) );
	const float variationInput = static_cast< float >( static_cast< double >( m_bot->entindex() ) * static_cast< double >( area->GetID() ) );
	const double variation = ( static_cast< double >( FastCos( variationInput ) ) + 1.0 ) * 10.0;
	return static_cast< float >( ( variation + static_cast< double >( penalty ) ) * static_cast< double >( distance ) + static_cast< double >( fromArea->GetCostSoFar() ) );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
