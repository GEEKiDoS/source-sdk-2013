//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "igamesystem.h"
#include "tier0/memdbgon.h"

// S30AE10/309430 and the RTTI CSurvivorGroupInfo singleton1085665C.
// Native v13 is FrameUpdatePreEntityThink (the original has no SDK-added
// LevelShutdownPreClearSteamAPIContext slot). This is not a Director cache.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
class CSurvivorGroupInfo : public CAutoGameSystemPerFrame
{
public:
	CSurvivorGroupInfo() : CAutoGameSystemPerFrame( "SurvivorGroupInfo" ) {}
	virtual void FrameUpdatePreEntityThink() OVERRIDE
	{
		float longest = 0.0f;
		float shortest = FLT_MAX;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !player || FNullEnt( player->edict() ) || !player->IsConnected() ||
				player->GetTeamNumber() != TEAM_SURVIVOR )
				continue;
			// No alive, incapacitation, or team4 predicate exists in S309430.
			const float elapsed = player->GetTimeSinceCombat();
			if ( !( longest > elapsed ) )
				longest = elapsed;
			if ( !( elapsed > shortest ) )
				shortest = elapsed;
		}
		m_longestCombatInterval = longest;
		m_shortestCombatInterval = shortest;
	}
	float GetTimeSinceCombat( bool everyone ) const
	{
		return everyone ? m_longestCombatInterval : m_shortestCombatInterval;
	}
private:
	float m_longestCombatInterval;
	float m_shortestCombatInterval;
};
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

static CSurvivorGroupInfo s_survivorGroupInfo;

// S2FCFD0(true) reads the maximum; false reads the minimum. The static object's
// native lifetime is preserved; no spawn/death/transition reset was present.
float CTerrorPlayer::GetTimeSinceGroupInCombat( bool everyone ) const
{
	return s_survivorGroupInfo.GetTimeSinceCombat( everyone );
}
