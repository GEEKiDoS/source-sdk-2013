//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/player/cs_player.h"
#include "team.h"
#include "gamevars_shared.h"
#include "tier0/memdbgon.h"

// Native84DDC0 registration. No second ConVarRef/default-only provider.
ConVar z_spawn_flow_limit( "z_spawn_flow_limit", "1500", 0x00004004, "" );

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
bool CTerrorGameRules::FPlayerCanRespawn( CBasePlayer *base )
{
	// S2DD9B0 / Terror Rules v276 replaces CMultiplayRules' inherited true.
	if ( !base || !base->IsPlayer() ) Error( "FPlayerCanRespawn: pPlayer=0" );
	CCSPlayer *player = static_cast< CCSPlayer * >( base );
	// Native CTeam v848 counts the ACTUAL roster; no connected/alive recount or
	// invented zero-team fallback. Native Rules628 is written before624.
	m_iNumCT = GetGlobalTeam( TEAM_INFECTED )->GetNumPlayers();
	m_iNumTerrorist = GetGlobalTeam( TEAM_SURVIVOR )->GetNumPlayers();
	if ( m_iNumTerrorist > 0 )
	{
		if ( player->GetTeamNumber() == TEAM_SURVIVOR && m_iNumTerrorist == 1 ) return true;
		if ( player->GetTeamNumber() == TEAM_SURVIVOR )
		{
			// S262330's TEAM2 outer iterator and S28FCD0's universal iterator
			// reach the SAME25B890 fixed-TEAM2 functor. Preserve the two scans.
			if ( !( GetLowestSurvivorFlow() == 0.0f ) && TheDirector->HasAnySurvivorLeftSafeArea() )
			{
				float lowest = GetLowestSurvivorFlow();
				// Native COMISS threshold,lowest / JA allows only ordered threshold>lowest.
				// Unordered therefore DENIES, unlike a literal threshold<=lowest translation.
				if ( !( z_spawn_flow_limit.GetFloat() > lowest ) )
				{
					if ( mp_fadetoblack.GetInt() != 0 )
					{
						// The original stack DWORD really is0, INCLUDING alpha0.
						const color32 black = { 0, 0, 0, 0 };
						UTIL_ScreenFade( player, black, 3.0f, 3.0f, 10 );
					}
					return false;
				}
			}
			return true;
		}
	}
	// Original fallback for non-survivors OR a nonpositive survivor roster.
	// Native640 is the existing BASE m_bGameStarted field; Terror virtual7002E0490
	// is genuinely false and does not run CCS23EA20's true writer.
	if ( player->GetNumSpawns() > 0 && m_bGameStarted ) return false;
	return true;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
