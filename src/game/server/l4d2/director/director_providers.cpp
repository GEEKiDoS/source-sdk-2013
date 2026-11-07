//========= Copyright Valve Corporation, All rights reserved. ============//
// External integration packet. Production ownership remains M3Integrator5.
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/info_director.h"
#include "l4d2/entities/survivor_position.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/director/zombiemanager.h"
#include "entitylist.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotIntentionInterface.h"
#include "tier1/fmtstr.h"
#include "l4d2/terror_native_qsort.h"
#include <float.h>
#include "tier0/memdbgon.h"

// Sole native constructor definitions are in director_frame_console_server_cvars.inc.
// Existing frame/session-manager readers and these methods use those same objects.
extern ConVar director_test_loop;
extern ConVar director_test_loop_rotate_maps;
extern ConVar director_test_loop_restarts_before_rotate;
extern ConVar director_test_loop_time;
extern ConVar director_no_mobs;

namespace
{
// S7A5428/2C/30 and S83DB08/0C/10/DC10/14 are DLL-lifetime globals, not
// Director Reset/constructor state. Wall-clock samples narrow only on stores.
float s_flDirectorTestMapStart = -999.0f;
float s_flDirectorTestFastAdvance = FLT_MAX;
int s_nDirectorTestServerCount = -1;
int s_nDirectorTestCampaignRestarts;
int s_nDirectorTestCheckpointWarps;
char s_szDirectorTestLastMap[256];
float s_flDirectorTestLastSpectatorCommand;
bool s_bDirectorTestConfigured;

struct DirectorTestCampaign_t
{
	const char *finale;
	const char *nextMap;
};
// Native S7A53C0..S7A5427, including DLC3 -> the first campaign.
const DirectorTestCampaign_t s_DirectorTestCampaigns[] =
{
	{ "c1m4_atrium", "c2m1_highway" },
	{ "c2m5_concert", "c3m1_plankcountry" },
	{ "c3m4_plantation", "c4m1_milltown_a" },
	{ "c4m5_milltown_escape", "c5m1_waterfront" },
	{ "c5m5_bridge", "c6m1_riverbank" },
	{ "c6m3_port", "c7m1_docks" },
	{ "c7m3_port", "c8m1_apartment" },
	{ "c8m5_rooftop", "c9m1_alleys" },
	{ "c9m2_lots", "c10m1_caves" },
	{ "c10m5_houseboat", "c11m1_greenhouse" },
	{ "c11m5_runway", "c12m1_hilltop" },
	{ "c12m5_cornfield", "c13m1_alpinecreek" },
	{ "c13m4_cutthroatcreek", "c1m1_hotel" }
};

// S260E18..S26104E constructs 55 prefixes plus a wraparound sentinel.
const char *const s_DirectorTestMapPrefixes[] =
{
	"c1m1", "c1m2", "c1m3", "c1m4",
	"c2m1", "c2m2", "c2m3", "c2m4", "c2m5",
	"c3m1", "c3m2", "c3m3", "c3m4",
	"c4m1", "c4m2", "c4m3", "c4m4", "c4m5",
	"c5m1", "c5m2", "c5m3", "c5m4", "c5m5",
	"c6m1", "c6m2", "c6m3",
	"c7m1", "c7m2", "c7m3",
	"c8m1", "c8m2", "c8m3", "c8m4", "c8m5",
	"c9m1", "c9m2",
	"c10m1", "c10m2", "c10m3", "c10m4", "c10m5",
	"c11m1", "c11m2", "c11m3", "c11m4", "c11m5",
	"c12m1", "c12m2", "c12m3", "c12m4", "c12m5",
	"c13m1", "c13m2", "c13m3", "c13m4", "c1m1"
};

const char *DirectorTestMapName()
{
	const char *map = STRING( gpGlobals->mapname );
	return map ? map : "";
}

// S261262..S2612B9 / S2613A0..S2613F7: the native ForEachPlayer
// admission, then team 2. There is deliberately no alive/ghost filter.
CTerrorPlayer *DirectorTestSurvivor( int index )
{
	CBasePlayer *base = UTIL_PlayerByIndex( index );
	if ( !base || !base->edict() || !base->entindex() || !base->IsPlayer() ||
		!base->IsConnected() || base->GetTeamNumber() != TEAM_SURVIVOR )
		return NULL;
	return static_cast< CTerrorPlayer * >( base );
}

// S25DCB0 deliberately returns ONLY zero or one. Do not replace this with
// subtraction/a three-way comparator: the native qsort receives this body.
int __cdecl CompareDirectorSurvivorPositionOrder( const void *left, const void *right )
{
	return ( *static_cast< CSurvivorPosition *const * >( left ) )->GetOrder() >
		( *static_cast< CSurvivorPosition *const * >( right ) )->GetOrder();
}
}

// S260DF0. EDI in the decompilation is a saved register, not a parameter.
#pragma float_control( precise, on, push )
void CDirector::UpdateDirectorTestLoop()
{
	if ( director_test_loop.GetInt() == 3 )
	{
		if ( s_nDirectorTestServerCount != gpGlobals->serverCount )
		{
			s_nDirectorTestServerCount = gpGlobals->serverCount;
			s_flDirectorTestFastAdvance = static_cast< float >( Plat_FloatTime() + 15.0 );
		}
		if ( Plat_FloatTime() > s_flDirectorTestFastAdvance )
		{
			s_flDirectorTestFastAdvance = FLT_MAX;
			engine->ServerCommand( "mem_dump\n" );
			for ( int i = 0; i < ARRAYSIZE( s_DirectorTestMapPrefixes ) - 1; ++i )
			{
				// Exactly four characters, case-sensitive, with NO break. Native
				// consequently matches multiple c10/c11/c12/c13 prefixes.
				if ( !Q_strncmp( DirectorTestMapName(), s_DirectorTestMapPrefixes[i], 4 ) )
					engine->ServerCommand( CFmtStr( "changelevel %s\n", s_DirectorTestMapPrefixes[i + 1] ) );
			}
		}
		return;
	}

	if ( director_test_loop.GetInt() != 0 )
	{
		if ( !s_bDirectorTestConfigured && Plat_FloatTime() - s_flDirectorTestMapStart > 20.0 )
		{
			engine->ServerCommand( "sb_all_bot_game 1\n" );
			engine->ServerCommand( "allow_all_bot_survivor_team 1\n" );
			engine->ServerCommand( "host_timescale 2.5\n" );
			engine->ServerCommand( "mem_test_every_n_seconds 30\n" );
			engine->ServerCommand( "think_limit 999\n" );
			engine->ServerCommand( "memorylog_tick 20\n" );
			engine->ServerCommand( "memorylog_mem_dump 1\n" );
			if ( director_test_loop.GetInt() > 1 )
			{
				engine->ServerCommand( "sv_logfile 0\n" );
				engine->ServerCommand( "log on\n" );
				engine->ServerCommand( "developer 2\n" );
			}
			else
			{
				engine->ServerCommand( "developer 1\n" );
			}
			s_flDirectorTestLastSpectatorCommand = -99.0f;
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = DirectorTestSurvivor( i );
				if ( player && player->IsBot() )
					player->MyNextBotPointer()->GetIntentionInterface()->Reset();
			}
			s_bDirectorTestConfigured = true;
		}

		if ( Q_strcmp( s_szDirectorTestLastMap, DirectorTestMapName() ) )
		{
			const double now = Plat_FloatTime();
			s_flDirectorTestMapStart = static_cast< float >( now );
			s_nDirectorTestCheckpointWarps = 0;
			s_flDirectorTestLastSpectatorCommand = static_cast< float >( now - 5.0 );
			Q_strncpy( s_szDirectorTestLastMap, DirectorTestMapName(), sizeof( s_szDirectorTestLastMap ) );
		}
		if ( Plat_FloatTime() - s_flDirectorTestLastSpectatorCommand > 5.0 )
		{
			s_flDirectorTestLastSpectatorCommand = static_cast< float >( Plat_FloatTime() );
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = DirectorTestSurvivor( i );
				if ( player && !player->IsBot() )
					engine->ClientCommand( player->edict(), "jointeam spectator\n" );
			}
		}
		const float mapTimeMinutes = director_test_loop_time.GetFloat();
		if ( Plat_FloatTime() - s_flDirectorTestMapStart > static_cast< double >( mapTimeMinutes ) * 60.0 ||
			m_ScenarioExitTimer.m_timestamp > 0.0f )
		{
			s_flDirectorTestMapStart = static_cast< float >( Plat_FloatTime() );
			s_flDirectorTestLastSpectatorCommand = FLT_MAX;
			engine->ServerCommand( "mem_dump\n" );
			bool isCampaignFinale = false;
			for ( int i = 0; i < ARRAYSIZE( s_DirectorTestCampaigns ); ++i )
			{
				if ( !Q_strcmp( s_DirectorTestCampaigns[i].finale, DirectorTestMapName() ) )
				{
					isCampaignFinale = true;
					break;
				}
			}
			if ( !isCampaignFinale )
			{
				if ( ++s_nDirectorTestCheckpointWarps > 1 )
				{
					Msg( "Survivors haven't closed the saferoom door?\n" );
					char nextMap[8];
					Q_strncpy( nextMap, DirectorTestMapName(), 5 );
					++nextMap[3]; // Preserve the native four-character/truncation quirk.
					engine->ServerCommand( CFmtStr( "map %s\n", nextMap ) );
				}
				else
				{
					engine->ServerCommand( "warp_all_survivors_to_checkpoint\n" );
				}
				Q_strncpy( s_szDirectorTestLastMap, DirectorTestMapName(), sizeof( s_szDirectorTestLastMap ) );
				return;
			}

			s_bDirectorTestConfigured = false;
			if ( !director_test_loop_rotate_maps.GetBool() ||
				s_nDirectorTestCampaignRestarts < director_test_loop_restarts_before_rotate.GetInt() )
			{
				++s_nDirectorTestCampaignRestarts;
				engine->ServerCommand( "mp_restartgame 1\n" );
			}
			else
			{
				const char *nextMap = s_DirectorTestCampaigns[0].nextMap;
				s_nDirectorTestCampaignRestarts = 0;
				for ( int i = 0; i < ARRAYSIZE( s_DirectorTestCampaigns ); ++i )
				{
					if ( !Q_strcmp( s_DirectorTestCampaigns[i].finale, DirectorTestMapName() ) )
						nextMap = s_DirectorTestCampaigns[i].nextMap;
				}
				engine->ServerCommand( CFmtStr( "map %s\n", nextMap ) );
			}
		}
	}
	else if ( s_bDirectorTestConfigured )
	{
		s_bDirectorTestConfigured = false;
		s_flDirectorTestMapStart = -999.0f;
		CBasePlayer *host = UTIL_GetListenServerHost();
		if ( host ) engine->ClientCommand( host->edict(), "jointeam survivor\n" );
		engine->ServerCommand( "host_timescale 1\n" );
		engine->ServerCommand( "mem_test_every_n_seconds 0\n" );
		engine->ServerCommand( "sb_all_bot_game 0\n" );
		engine->ServerCommand( "allow_all_bot_survivor_team 0\n" );
		engine->ServerCommand( "sb_all_bot_game 0\n" ); // Native duplicate is intentional.
	}
}

// S26CBB0: reaching the expired timer ALWAYS resets it, even when the Tank
// suppresses the actual mob request. Native raw scripted-mode byte0 is ACTIVE.
void CDirector::UpdateMob()
{
	// Native1026CBB3/1026CBB9 reads raw byte0; OnActivate279A30 writes ACTIVE.
	if ( m_pScriptedMode->IsActive() && GetDirectorOptionInt( "NoMobSpawns", 0 ) != 0 ) return;
	if ( director_no_mobs.GetBool() || m_nIntensityState != 0 ||
		!( m_MobSpawnTimer.m_timestamp > 0.0f ) ||
		!( gpGlobals->curtime >= m_MobSpawnTimer.m_timestamp ) ) return;
	if ( m_nTankCount <= 0 || HasSurvivorsAdvancedBeyondTank() ||
		GetDirectorOptionInt( "ShouldAllowMobsWithTank", 0 ) != 0 )
		TheZombieManager->SpawnMob( TheDirector->GetMobSize() );
	ResetMobTimer(); // Existing S26AAA0 owner, not ScriptResetMobTimer S262E90.
}
#pragma float_control( pop )

bool CDirector::IsPublicLiveSession() const
{
	// Recorded MatchFramework boundary, approved by Main: the target has no
	// MatchFramework/session. Native S28D1BF..S28D229 takes the no-framework
	// or no-session path before testing System/network="LIVE" and
	// System/access="public" (both case-insensitive). Do not infer public
	// access from maxClients or listen-server status. The independent native
	// dedicated-server bypass S28D1AD..S28D1BD remains in the existing caller.
	return false;
}

// S25CCB0: only the FIRST info_director is looked up, then actual RTTI.
void CDirector::FireCustomStageOutput()
{
	CInfoDirector *director = dynamic_cast< CInfoDirector * >( gEntList.FindEntityByClassname( NULL, "info_director" ) );
	if ( director ) director->m_OnCustomPanicStageFinished.FireOutput( NULL, director, 0.0f );
}

// S26DC90. Named positions are not sorted or consumed: every matching name
// is applied, so later duplicate names intentionally replace earlier matches.
void CDirector::PositionFinaleEscapees( const CUtlVector< CTerrorPlayer * > &escaped )
{
	CUtlVector< CSurvivorPosition * > namedPositions;
	CUtlVector< CSurvivorPosition * > unnamedPositions;
	CBaseEntity *entity = NULL;
	while ( ( entity = gEntList.FindEntityByClassname( entity, "info_survivor_position" ) ) != NULL )
	{
		CSurvivorPosition *position = dynamic_cast< CSurvivorPosition * >( entity );
		if ( !position ) continue;
		if ( position->GetSurvivorName() ) namedPositions.AddToTail( position );
		else unnamedPositions.AddToTail( position );
	}
	TerrorNativeQSort( unnamedPositions.Base(), unnamedPositions.Count(),
		sizeof( CSurvivorPosition * ), CompareDirectorSurvivorPositionOrder );
	int nextUnnamedPosition = 0;
	for ( int i = 0; i < escaped.Count(); ++i )
	{
		CTerrorPlayer *player = escaped[i];
		const char *name = player->GetCharacterDisplayName(); // Native player virtual+1448, NOT response-rule names.
		bool matched = false;
		for ( int j = 0; j < namedPositions.Count(); ++j )
		{
			CSurvivorPosition *position = namedPositions[j];
			if ( Q_stricmp( name, position->GetSurvivorName() ) ) continue;
			player->SetSurvivorPosition( position ); // Existing S3170F0 owner, including parent/control/HUD/weapon state.
			if ( position->GetSurvivorIntroSequence() )
			{
				const int sequence = player->LookupSequence( position->GetSurvivorIntroSequence() );
				if ( sequence != -1 )
				{
					player->GetTerrorAnimState()->ForceSequenceOverride( sequence );
					player->SetAbsAngles( position->GetAbsAngles() );
				}
			}
			else if ( position->GetSurvivorConcept() )
			{
				player->SpeakIfAllowed( position->GetSurvivorConcept(), 3, NULL, NULL, 0, NULL );
			}
			matched = true;
		}
		if ( !matched && nextUnnamedPosition < unnamedPositions.Count() )
			player->SetSurvivorPosition( unnamedPositions[nextUnnamedPosition++] );
	}
	if ( TerrorGameRules()->IsScavengeMode() )
	{
		CUtlVector< CTerrorPlayer * > ghosts;
		CollectTerrorPlayers( ghosts, TEAM_INFECTED, 4, 3, 9 );
		for ( int i = 0; i < ghosts.Count(); ++i )
		{
			if ( ghosts[i]->IsGhost() ) ghosts[i]->WarpGhostToInitialPosition( false );
		}
	}
}
