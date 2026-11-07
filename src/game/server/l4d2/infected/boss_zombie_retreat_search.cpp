//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss_zombie_retreat_search.h"
#include "l4d2/infected/boss_zombie_player_bot.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotVisionInterface.h"

#include "debugoverlay_shared.h"
#include "tier0/memdbgon.h"

ConVar z_debug_escape_scan( "z_debug_escape_scan", "0", 0x00004004 ); // Native5A8910, sole owner.
BossZombieRetreatSearch::BossZombieRetreatSearch( BossZombiePlayerBot *bot )
	: m_bot( bot ), m_area( NULL ), m_visibleSurvivorCount( 4 ), m_rangeSquared( 0.0f )
{
}

#pragma float_control( precise, on, push )
bool BossZombieRetreatSearch::operator()( CNavArea *area )
{
	if ( z_debug_escape_scan.GetBool() && area->GetParent() )
		NDebugOverlay::HorzArrow( area->GetParent()->GetCenter(), area->GetCenter(), 2.0f,
			255, 255, 0, 255, true, 99999.8984375f );
	if ( area == m_bot->GetLastKnownArea() ) return true;
	TerrorNavArea *terrorArea = ToTerrorNavArea( area );
	int visibleCount = 0;
	for ( int i = 0; i < 4; ++i )
		if ( terrorArea->IsPotentiallyVisibleByCharacter( static_cast< SurvivorCharacterType >( i ) ) ) ++visibleCount;
	if ( visibleCount > m_visibleSurvivorCount ) return true;
	CTerrorPlayer *nearest = FindClosestBossSurvivor( area->GetCenter(), true, true );
	float distance = nearest ? ( nearest->GetAbsOrigin() - area->GetCenter() ).LengthSqr() : 0.0f;
	if ( visibleCount == m_visibleSurvivorCount )
	{
		// Equal-exposure candidates do not rerun the native path-cost test.
		if ( distance > m_rangeSquared && m_bot->GetVisionInterface()->IsLineOfSightClear( area->GetCenter() ) )
		{
			m_area = terrorArea;
			m_rangeSquared = distance;
		}
	}
	else
	{
		InfectedPathCost cost( m_bot );
		if ( !( cost( area, area->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) &&
			m_bot->GetVisionInterface()->IsLineOfSightClear( area->GetCenter() ) )
		{
			m_area = terrorArea;
			m_visibleSurvivorCount = visibleCount;
			m_rangeSquared = distance;
		}
	}
	return true;
}
#pragma float_control( pop )
