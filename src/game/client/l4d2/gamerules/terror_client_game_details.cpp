//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "tier0/memdbgon.h"

KeyValues *TerrorBuildConsumedClientGameProjection()
{
	// Explicit game-owned consumed-Game adapter, NOT a MatchSession/Framework.
	// Native campaign/chapter are session-owned. Target uses the SAME current
	// mode's FIRST matching mission reference, then ascending chapter order.
	// Exact only for a UNIQUE mode/BSP match; ambiguous/addon session selection
	// is qualified projection, never asserted equivalent to session identity.
	if ( !engine->IsInGame() ) return NULL;
	char map[MAX_PATH];
	V_FileBase( engine->GetLevelName(), map, sizeof( map ) );
	if ( !*map ) return NULL;
	KeyValues *details = new KeyValues( "GameDetailsClient" );
	ConVarRef mode( "mp_gamemode" ); // The genuine replicated client-known convar.
	if ( mode.IsValid() ) details->SetString( "Game/mode", mode.GetString() );
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfoByBspName( details, map, &mission );
	if ( chapter )
	{
		details->SetString( "Game/campaign", mission->GetString( "Name", "" ) );
		details->SetInt( "Game/chapter", chapter->GetInt( "chapter", 0 ) );
	}
	return details;
}

KeyValues *CTerrorGameRules::GetChapterInfo() const
{
	// Native C27B3E0 releases owned details and returns a borrowed chapter.
	// Reuse the existing consumed-Game projection with its stated qualification.
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	if ( !details )
		return NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, NULL );
	details->deleteThis();
	return chapter;
}

static int s_clientSurvivorSet = 0;
int CTerrorGameRules::GetSurvivorSet() const
{
	// Native27C7A0->27C720: zero triggers lookup; missing owned details or
	// borrowed mission is the original guarded2 fallback, not missing-service2.
	if ( !s_clientSurvivorSet )
	{
		KeyValues *details = TerrorBuildConsumedClientGameProjection();
		if ( !details ) return s_clientSurvivorSet = 2;
		KeyValues *mission = NULL;
		g_TerrorModes.GetChapterInfo( details, &mission );
		details->deleteThis();
		s_clientSurvivorSet = mission ? mission->GetInt( "survivor_set", 2 ) : 2;
	}
	return s_clientSurvivorSet;
}
void CTerrorGameRules::ResetSurvivorSetCache() { s_clientSurvivorSet = 0; }
void CTerrorGameRules::LevelInitPreEntity()
{
	// Native27D630 ->27CB20, actual EXISTING Rules callback, not an extra
	// game system or per-round initializer. The metadata projection's unique-
	// lookup qualification applies here too; no session/System state inferred.
	if ( g_TerrorModes.IsBaseMode( "versus" ) )
	{
		KeyValues *details = TerrorBuildConsumedClientGameProjection();
		if ( details )
		{
			KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, NULL );
			details->deleteThis();
			if ( chapter )
			{
				int maximum = chapter->GetInt( "VersusCompletionScore", 400 );
				if ( maximum % 4 != 0 ) maximum &= ~3;
				m_nVersusMaxCompletionScore = maximum;
			}
		}
	}
	m_flTeamBestRoundTime = 0.0f;
	ResetSurvivorSetCache();
}
