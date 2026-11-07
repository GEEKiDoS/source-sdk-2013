//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "nav_ladder.h"

#include "tier0/memdbgon.h"

InfectedPathCost::InfectedPathCost( INextBot *bot )
{
	m_bot = bot;
	m_locomotion = bot->GetLocomotionInterface();
	CBaseCombatCharacter *entity = bot->GetEntity();
	Infected *infected = dynamic_cast< Infected * >( entity );
	m_actorRandomValue = infected ? infected->GetActorRandomValue() : 1.0f;
	m_player = entity->IsPlayer() ? static_cast< CTerrorPlayer * >( entity ) : NULL;
	m_zombieClass = entity->GetTeamNumber() == TEAM_INFECTED ? entity->GetZombieClass() : 9;
}

float InfectedPathCost::operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder,
	const CFuncElevator *elevator, float length ) const
{
	if ( !m_locomotion->IsAreaTraversable( area ) )
		return -1.0f;

	const float size = 0.5f * ( area->GetSizeX() + area->GetSizeY() );
	if ( !fromArea )
		return 0.0f;

	float distance;
	const float randomness = 1.0f - m_actorRandomValue;
	if ( ladder )
	{
		float multiplier = ( randomness + 1.0f ) * 5.0f;
		if ( fromArea->GetCenter().z > area->GetCenter().z )
			multiplier *= 3.0f;
		else if ( m_zombieClass == 1 ) // Smoker
			multiplier = 1.0f;
		distance = ladder->m_length * multiplier;
	}
	else
	{
		float heightChange = fromArea->ComputeAdjacentConnectionHeightChange( area );
		if ( heightChange > m_locomotion->GetMaxJumpHeight() )
			return -1.0f;

		distance = length > 0.0f ? length : ( area->GetCenter() - fromArea->GetCenter() ).Length();
		if ( m_zombieClass == 1 ) // Smoker
		{
			if ( heightChange < 0.0f )
				heightChange *= 2.0f;
			else if ( heightChange > 0.0f )
				heightChange = 0.0f;
		}
		else
		{
			heightChange = MAX( 0.0f, heightChange );
			if ( heightChange >= 24.0f )
				heightChange *= randomness * 3.0f + 1.0f;
		}
		if ( fabsf( heightChange ) > 3.0f )
			distance += fabsf( heightChange );

		if ( m_player )
		{
			const TerrorNavArea *terrorArea = ToTerrorNavArea( area );
			// Original class dispatch: Smoker/Boomer/Spitter use 10/5;
			// Hunter/Tank use 3/2; Jockey/Charger/Common have no modifier.
			if ( m_zombieClass == 1 || m_zombieClass == 2 || m_zombieClass == 4 )
			{
				if ( terrorArea->IsCompletelyVisibleToAnyCharacter() )
					distance *= 10.0f;
				else if ( terrorArea->IsPotentiallyVisibleToAnyCharacter() )
					distance *= 5.0f;
			}
			else if ( m_zombieClass == 3 || m_zombieClass == 8 )
			{
				if ( terrorArea->IsCompletelyVisibleToAnyCharacter() )
					distance *= 3.0f;
				else if ( terrorArea->IsPotentiallyVisibleToAnyCharacter() )
					distance *= 2.0f;
			}
			if ( area->HasAttributes( 0x00000001 ) )
				return -1.0f;
		}
		if ( area->HasAttributes( 0x00080000 ) )
			distance *= 10.0f;
	}

	float cost = fromArea->GetCostSoFar() + distance;
	const unsigned char count = area->GetPlayerCount( TEAM_INFECTED );
	if ( count && size >= 1.0f )
		cost += count * 50000.0f / size;
	return cost;
}
