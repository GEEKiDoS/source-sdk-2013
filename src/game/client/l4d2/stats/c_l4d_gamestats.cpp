//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/c_l4d_gamestats.h"
#include "cliententitylist.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_playerresource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "usermessages.h"
#include "steamworks_gamestats.h"
#include <emmintrin.h>
#include "l4d2/stats/c_l4d_stat_names.inc"
#include "tier0/memdbgon.h"

static C_CL4DGameStats g_ClientL4DGameStats;
static void __MsgFunc_SteamWeaponStatData( bf_read &message ) { g_ClientL4DGameStats.ReceiveWeaponStats( message ); }
void TerrorRecordPZDamageStats( int type, C_BasePlayer *attacker, C_BasePlayer *victim ) { g_ClientL4DGameStats.RecordPZDamageStats( type, attacker, victim ); }
void TerrorRecordRematchVoteChoice( bool yes ) { g_ClientL4DGameStats.RecordRematchVoteChoice( yes ); }
void TerrorRecordRematchOutcome( bool passed ) { g_ClientL4DGameStats.RecordRematchOutcome( passed ); }
CON_COMMAND_F( Reset_Steam_Stats, "Resets your currently signed in Steam stats!!!", 0x0000000A ) { g_ClientL4DGameStats.ResetSteamStats(); }
CON_COMMAND_F( Print_Steam_Stats, "Prints the current state of the steam stats to file.", 0x0000000A ) { g_ClientL4DGameStats.PrintSteamStats(); }

namespace
{
	int Add32( int a, int b ) { return (int)( (unsigned int)a + (unsigned int)b ); }
	int Truncate( float value ) { return _mm_cvtt_ss2si( _mm_set_ss( value ) ); }
	bool LoggedIntoSteam()
	{
		return steamapicontext && steamapicontext->SteamUser() && steamapicontext->SteamUtils()
			&& steamapicontext->SteamFriends() && steamapicontext->SteamUser()->BLoggedOn();
	}
	C_TerrorPlayer *LocalStatsPlayer() { return ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); }
	C_TerrorPlayer *EventStatsPlayer( IGameEvent *event, const char *key ) { return ToTerrorPlayer( UTIL_PlayerByIndex( engine->GetPlayerForUserID( event->GetInt( key ) ) ) ); }
	bool Versus() { return g_TerrorModes.IsBaseMode( "versus" ); }
	bool Scavenge() { return g_TerrorModes.IsBaseMode( "scavenge" ); }
	bool Survival() { return g_TerrorModes.IsBaseMode( "survival" ); }
	int StandingSurvivors()
	{
		// C244080 full ASM, not its incorrect no-argument decompilation.
		int count = 0;
		if ( !g_PR ) return count;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && !player->IsDormant() && g_PR->IsConnected( i ) && !player->IsBot() && player->GetTeamNumber() == TEAM_SURVIVOR
				&& player->m_lifeState == LIFE_ALIVE && !player->m_isGhost && !player->m_isIncapacitated ) ++count;
		}
		return count;
	}
}

C_CL4DGameStats::C_CL4DGameStats() : CAutoGameSystem( "C_CL4DGameStats" ),
	m_statsReceived( this, &C_CL4DGameStats::OnUserStatsReceived ), m_statsStored( this, &C_CL4DGameStats::OnUserStatsStored ),
	m_pSteamStatsDebug( NULL ), m_lastWeaponMessageTime( -1.0f )
{
	ClearUserStats(); ResetSession();
	Q_memset( m_survivorVersus, 0, sizeof( m_survivorVersus ) );
	Q_memset( m_zombieVersus, 0, sizeof( m_zombieVersus ) );
}
bool C_CL4DGameStats::Init()
{
	// Native2423B0's handler/listener admission is genuine Steam login, not a
	// pretend successful download. The HUD's direct PZ statistics entry is ungated.
	if ( LoggedIntoSteam() )
	{
		gamestats = this; ResetSession();
		static const char *const events[] =
		{
			"finale_win", "finale_vehicle_leaving", "achievement_earned", "ghost_spawn_time", "player_hurt_concise",
			"player_incapacitated", "player_death", "vote_cast_no", "vote_cast_yes", "vote_failed", "vote_passed", "vote_started",
			"tongue_grab", "lunge_pounce", "player_now_it", "choke_start", "heal_success", "tank_spawn", "weapon_given", "defibrillator_used",
			"gas_can_forced_drop", "gascan_pour_completed", "jockey_ride", "entered_spit", "scavenge_round_start", "scavenge_round_finished",
			"scavenge_match_finished", "survival_round_start", "adrenaline_used", "pills_used", "scavenge_gas_can_destroyed",
			"difficulty_changed", "player_first_spawn", "round_end", "player_spawn", "gascan_pour_interrupted", "charger_charge_start", "spit_burst"
		};
		// Native registers jockey_ride twice; target listener registration is idempotent.
		for ( int i = 0; i < ARRAYSIZE( events ); ++i ) ListenForGameEvent( events[i] );
	}
	return true;
}
void C_CL4DGameStats::Shutdown() { StopListeningForAllEvents(); }
void C_CL4DGameStats::ResetSession()
{
	for ( int i = 0; i < 8; ++i ) { Q_memset( &m_voteData[i], 0, sizeof( m_voteData[i] ) ); m_voteData[i].id = i; }
	Q_memset( m_session, 0, sizeof( m_session ) ); Q_memset( m_config, 0, sizeof( m_config ) );
	m_currentVoteType = 0; m_sessionEnded = m_playedSurvivor = m_playedInfected = false;
	m_sessionStartMap[0] = m_sessionEndMap[0] = m_textLanguage[0] = m_audioLanguage[0] = m_country[0] = '\0';
}
int C_CL4DGameStats::Difficulty() const
{
	ConVarRef difficulty( "z_difficulty" );
	const char *name = difficulty.GetString();
	return !Q_stricmp( name, "Easy" ) ? 0 : !Q_stricmp( name, "Normal" ) ? 1 : !Q_stricmp( name, "Hard" ) ? 2 : !Q_stricmp( name, "Impossible" ) ? 3 : 1;
}
void C_CL4DGameStats::BeginSteamStatsDownload()
{
	// C1AC240 appends just like the target. Retain the native repeated hook
	// at each fresh session, including its curtime0/clock-rewind edge behavior.
	usermessages->HookMessage( "SteamWeaponStatData", __MsgFunc_SteamWeaponStatData );
	if ( steamapicontext && steamapicontext->SteamUserStats() )
	{
		steamapicontext->SteamUserStats()->RequestCurrentStats(); m_statsRequested = true;
	}
}
void C_CL4DGameStats::Event_LevelInit()
{
	if ( !m_sessionStartMap[0] )
	{
		ResetSession(); m_recent[3] = Difficulty();
		// C246C00 really feeds strstr's "L4D2C..." pointer directly to atoi;
		// it does NOT skip the prefix. Keep that defined native -1 result.
		m_recent[2] = -1;
		BeginSteamStatsDownload();
		V_FileBase( engine->GetLevelName(), m_sessionStartMap, sizeof( m_sessionStartMap ) );
		Q_strncpy( m_sessionEndMap, m_sessionStartMap, sizeof( m_sessionEndMap ) );
		if ( steamapicontext && steamapicontext->SteamApps() && steamapicontext->SteamUtils() )
		{
			Q_strncpy( m_country, steamapicontext->SteamUtils()->GetIPCountry(), 3 );
			engine->GetUILanguage( m_textLanguage, sizeof( m_textLanguage ) );
			const char *audio = steamapicontext->SteamApps()->GetCurrentGameLanguage();
			Q_strncpy( m_audioLanguage, audio ? audio : "unknown", sizeof( m_audioLanguage ) );
		}
		else
		{
			Q_strncpy( m_textLanguage, "unknown", sizeof( m_textLanguage ) );
			Q_strncpy( m_audioLanguage, "unknown", sizeof( m_audioLanguage ) );
		}
		ConVarRef pitch( "m_pitch", true ), console( "con_enable", true ), caption( "closecaption", true ), colorblind( "cl_colorblind", true ), vox( "voice_vox", true );
		ConVarRef joystick( "joystick", true ), wounds( "z_wound_client_disabled", true );
		if ( pitch.IsValid() && pitch.GetFloat() < 0.0f ) m_config[0] = 1;
		if ( console.IsValid() && console.GetBool() ) m_config[1] = 1;
		if ( joystick.IsValid() && joystick.GetBool() ) m_config[2] = 1;
		if ( wounds.IsValid() ) m_config[3] = wounds.GetInt();
		if ( vox.IsValid() ) m_config[4] = vox.GetInt();
		if ( colorblind.IsValid() ) m_config[5] = colorblind.GetInt();
		if ( caption.IsValid() ) m_config[6] = caption.GetInt();
		m_config[7] = UTIL_IsLowViolence();
		// Match-framework lobby-leader metadata has no target owner. Omit its
		// unavailable query only; retain native reset value and real row field.
		if ( LoggedIntoSteam() && steamapicontext->SteamMatchmaking() )
		{
			FriendGameInfo_t mine; Q_memset( &mine, 0, sizeof( mine ) );
			ISteamFriends *friends = steamapicontext->SteamFriends();
			friends->GetFriendGamePlayed( steamapicontext->SteamUser()->GetSteamID(), &mine );
			CSteamID owner = steamapicontext->SteamMatchmaking()->GetLobbyOwner( mine.m_steamIDLobby );
			const int count = friends->GetFriendCount( k_EFriendFlagImmediate );
			for ( int i = 0; i < count; ++i )
			{
				FriendGameInfo_t other; Q_memset( &other, 0, sizeof( other ) );
				if ( friends->GetFriendGamePlayed( friends->GetFriendByIndex( i, k_EFriendFlagImmediate ), &other ) && other.m_gameID.IsValid() )
				{
					CSteamID friendOwner = steamapicontext->SteamMatchmaking()->GetLobbyOwner( other.m_steamIDLobby );
					if ( friendOwner.IsValid() && owner == friendOwner ) m_session[1] = Add32( m_session[1], 1 );
				}
			}
		}
		m_session[8] = g_TerrorModes.IsBaseMode( "coop" ) ? 5 : Versus() ? 1 : Survival() ? 2 : Scavenge() ? 3 : g_TerrorModes.IsBaseMode( "realism" ) ? 4 : 0;
		// Native LIVE promotion of coop5->0 is match-session metadata unavailable
		// on target; no invented LIVE/lobby/session identity.
		const char *mode = g_TerrorModes.GetName();
		static const char *const stock[] = { "coop", "realism", "survival", "versus", "teamversus", "scavenge", "teamscavenge" };
		bool standard = false;
		for ( int i = 0; i < ARRAYSIZE( stock ); ++i ) if ( !Q_stricmp( mode, stock[i] ) ) standard = true;
		if ( !Q_stricmp( mode, "mutation12" ) ) m_session[8] = 7; else if ( !standard ) m_session[8] = 6;
	}
	m_lastWeaponMessageTime = gpGlobals->curtime;
	V_FileBase( engine->GetLevelName(), m_sessionEndMap, sizeof( m_sessionEndMap ) );
}
void C_CL4DGameStats::RecordCharacterPlayed( int character )
{
	if ( (unsigned int)character <= 3u ) m_total[10 + character] = Add32( m_total[10 + character], 1 );
}
void C_CL4DGameStats::RecordGamePlayed()
{
	m_recent[4] = Versus() ? 1 : Survival() ? 2 : Scavenge() ? 3 : g_TerrorModes.IsBaseMode( "realism" ) ? 4 : 0;
	if ( Versus() ) m_versus[0] = Add32( m_versus[0], 1 );
	else if ( Survival() ) m_games[17] = Add32( m_games[17], 1 );
	else if ( Scavenge() ) m_games[18] = Add32( m_games[18], 1 );
	else if ( g_TerrorModes.IsBaseMode( "realism" ) ) m_games[19] = Add32( m_games[19], 1 );
	m_games[0] = Add32( m_games[0], 1 );
}
void C_CL4DGameStats::Event_LevelShutdown( float elapsed )
{
	C_TerrorPlayer *player = LocalStatsPlayer();
	if ( player )
	{
		int kills = 0; for ( int i = 0; i < 9; ++i ) kills = Add32( kills, player->m_checkpointZombieKills[i] );
		m_total[0] = Add32( m_total[0], kills );
		m_teamRevived = Add32( m_teamRevived, player->m_checkpointReviveOtherCount );
		int SpecialStats_t::*attack = Versus() ? &SpecialStats_t::attacks : &SpecialStats_t::scavengeAttacks;
		m_special[1].*attack = Add32( m_special[1].*attack, player->m_checkpointPZPounces );
		m_special[0].*attack = Add32( m_special[0].*attack, player->m_checkpointPZBombed );
		m_special[2].*attack = Add32( m_special[2].*attack, Add32( player->m_checkpointPZHung, player->m_checkpointPZPulled ) );
		m_special[3].*attack = Add32( m_special[3].*attack, player->m_checkpointPZTankThrows );
		m_special[6].*attack = Add32( m_special[6].*attack, player->m_checkpointPZNumChargeVictims );
	}
	if ( g_PR && Versus() )
	{
		KeyValues *details = TerrorBuildConsumedClientGameProjection();
		bool finalChapter = true;
		if ( details ) { details->SetInt( "Game/chapter", Add32( details->GetInt( "Game/chapter" ), 1 ) ); finalChapter = !g_TerrorModes.GetChapterInfo( details, NULL ); details->deleteThis(); }
		if ( finalChapter && player && TerrorGameRules() )
		{
			const int mine = player->m_iVersusTeam, other = 2 - ( mine != 1 );
			// Native indexes1/2; scripts outside that array have only their
			// undefined reads skipped under the standing out-of-array decision.
			if ( (unsigned int)( mine - 1 ) < 2u )
			{
				const int score = TerrorGameRules()->GetVersusScore( mine, true ), enemy = TerrorGameRules()->GetVersusScore( other, true );
				m_total[14] = MAX( m_total[14], score );
				if ( score > enemy ) m_versus[3] = Add32( m_versus[3], 1 ); else if ( enemy > score ) m_versus[4] = Add32( m_versus[4], 1 );
			}
		}
	}
	m_session[7] = Add32( m_session[7], Truncate( elapsed ) );
}

#pragma float_control( precise, on, push )
void C_CL4DGameStats::ReceiveWeaponStats( bf_read &message )
{
	if ( m_lastWeaponMessageTime > 0.0f ) { const float elapsed = gpGlobals->curtime - m_lastWeaponMessageTime; if ( elapsed < 5.0f && elapsed >= 0.0f ) return; }
	m_lastWeaponMessageTime = gpGlobals->curtime;
	while ( message.GetNumBitsLeft() >= 8 )
	{
		const int weapon = message.ReadByte();
		if ( weapon <= 38 )
		{
			m_weaponStats[0][weapon] = Add32( m_weaponStats[0][weapon], message.ReadByte() );
			m_weaponStats[5][weapon] = Add32( m_weaponStats[5][weapon], message.ReadByte() );
			m_weaponStats[1][weapon] = Add32( m_weaponStats[1][weapon], message.ReadByte() );
			m_weaponStats[2][weapon] = Add32( m_weaponStats[2][weapon], message.ReadByte() );
			m_weaponStats[6][weapon] = Add32( m_weaponStats[6][weapon], message.ReadByte() );
		}
	}
	ConVarRef cheats( "sv_cheats" ), bypass( "l4d_stats_nogameplaycheck", true );
	if ( !cheats.GetBool() || ( bypass.IsValid() && bypass.GetBool() ) ) UploadWeaponStats();
}
#pragma float_control( pop )
void C_CL4DGameStats::RecordPZDamageStats( int type, C_BasePlayer *attacker, C_BasePlayer *victim )
{
	// C242F10 is called by the sole HUD record entry after its actor-null gate.
	C_TerrorPlayer *local = LocalStatsPlayer();
	if ( !local ) return;
	if ( type == 18 ) { if ( local == attacker ) m_teamProtected = Add32( m_teamProtected, 1 ); else if ( local == victim ) m_total[7] = Add32( m_total[7], 1 ); }
	else if ( type == 15 && local == victim && local->m_lifeState == LIFE_ALIVE && local->m_isIncapacitated ) m_total[6] = Add32( m_total[6], 1 );
}
void C_CL4DGameStats::RecordRematchVoteChoice( bool yes )
{
	const int index = yes ? 4 : 6;
	if ( Versus() ) m_rematchStats[index] = Add32( m_rematchStats[index], 1 ); else if ( Scavenge() ) m_rematchStats[index + 1] = Add32( m_rematchStats[index + 1], 1 );
}
void C_CL4DGameStats::RecordRematchOutcome( bool passed )
{
	const int index = passed ? 0 : 2;
	if ( Versus() ) m_rematchStats[index] = Add32( m_rematchStats[index], 1 ); else if ( Scavenge() ) m_rematchStats[index + 1] = Add32( m_rematchStats[index + 1], 1 );
}
int C_CL4DGameStats::CurrentSpecialClass() const
{
	C_TerrorPlayer *player = LocalStatsPlayer();
	if ( !player ) return -1;
	switch ( player->GetZombieClass() ) { case 2: return 0; case 3: return 1; case 1: return 2; case 8: return 3; case 4: return 4; case 5: return 5; case 6: return 6; default: return -1; }
}
void C_CL4DGameStats::RecordSpecialLife( bool spawn )
{
	m_currentClass = CurrentSpecialClass();
	C_TerrorPlayer *player = LocalStatsPlayer();
	if ( m_currentClass == -1 || !player ) return;
	if ( g_PR )
	{
		const int score = g_PR->GetPlayerScore( player->entindex() );
		const int damage = (int)( (unsigned int)score - (unsigned int)m_previousScore );
		m_lifeDamage[m_currentClass] = MAX( m_lifeDamage[m_currentClass], damage ); m_previousScore = score;
	}
	if ( Versus() )
	{
		if ( spawn ) m_special[m_currentClass].spawns = Add32( m_special[m_currentClass].spawns, 1 );
		const float elapsed = gpGlobals->curtime - player->m_flStatsLifeStartTime;
		m_special[m_currentClass].life = Truncate( elapsed + (float)m_special[m_currentClass].life );
	}
}
int C_CL4DGameStats::ScavengeMap() const
{
	char map[64]; V_FileBase( engine->GetLevelName(), map, sizeof( map ) );
	for ( int i = 0; i < 21; ++i ) if ( !Q_stricmp( map, s_scavengeMaps[i] ) ) return i;
	return 20;
}
void C_CL4DGameStats::RecordScavengeRound()
{
	C_TerrorPlayer *local = LocalStatsPlayer();
	if ( !local || !g_PR || !TerrorGameRules() ) return;
	const int team = g_PR->GetTeam( local->entindex() ), enemy = ( team == TEAM_SURVIVOR ) ? TEAM_INFECTED : TEAM_SURVIVOR;
	int *row = m_scavenge[ScavengeMap()];
	const int score = TerrorGameRules()->GetScavengeTeamScore( team, -1 ), enemyScore = TerrorGameRules()->GetScavengeTeamScore( enemy, -1 );
	m_matchScore = Add32( m_matchScore, score ); row[12] = MAX( row[12], score );
	if ( score == TerrorGameRules()->GetScavengeItemsGoal() ) row[11] = Add32( row[11], 1 );
	int adjusted = score;
	if ( score == enemyScore )
	{
		const int sign = score ? 1 : -1;
		// C242D08/C242D2C use __ftol2_sse's double-spill/SSE2 signed32
		// conversion. Do not add an intermediate float spill.
		const int duration = _mm_cvttsd_si32( _mm_set_sd( TerrorGameRules()->GetTeamRoundDuration( team ) ) );
		const int enemyDuration = _mm_cvttsd_si32( _mm_set_sd( TerrorGameRules()->GetTeamRoundDuration( enemy ) ) );
		adjusted = Add32( score, duration >= enemyDuration ? -sign : sign );
	}
	row[adjusted <= enemyScore ? 1 : 0] = Add32( row[adjusted <= enemyScore ? 1 : 0], 1 ); // Native never increments RoundsTied.
}
void C_CL4DGameStats::SubmitRow( KeyValues *row, bool map )
{
	if ( map ) row->SetString( "MapID", m_sessionEndMap );
	row->SetInt( "TimeSubmitted", (int)GetSteamWorksSGameStatsUploader().GetTimeSinceEpoch() );
	if ( GetSteamWorksSGameStatsUploader().AddStatsForUpload( row ) != k_EResultOK ) row->deleteThis();
}
void C_CL4DGameStats::SubmitCompetitiveData()
{
	C_TerrorPlayer *local = LocalStatsPlayer();
	if ( local )
	{
		m_survivorVersus[3] = local->m_checkpointPillsUsed; m_survivorVersus[4] = local->m_checkpointMedkitsUsed;
		m_survivorVersus[5] = local->m_checkpointMolotovsUsed; m_survivorVersus[6] = local->m_checkpointPipebombsUsed;
		m_survivorVersus[8] = local->GetSurvivorCharacter(); m_survivorVersus[9] = local->m_checkpointIncaps; m_survivorVersus[10] = local->m_checkpointDeaths;
		m_zombieVersus[2] = local->m_checkpointPZKills; m_zombieVersus[3] = Truncate( local->m_maxDeadDuration );
		m_zombieVersus[4] = Truncate( local->m_totalDeadDuration ); m_zombieVersus[6] = local->m_checkpointDeaths;
	}
	KeyValues *survivor = new KeyValues( "L4D2SurvivorVersusData" );
	for ( int i = 0; i < 11; ++i ) survivor->SetInt( s_survivorVersusNames[i], m_survivorVersus[i] ); SubmitRow( survivor, true );
	KeyValues *infected = new KeyValues( "L4D2ZombieVersusData" );
	for ( int i = 0; i < 15; ++i ) infected->SetInt( s_zombieVersusNames[i], m_zombieVersus[i] ); SubmitRow( infected, true );
	Q_memset( m_survivorVersus, 0, sizeof( m_survivorVersus ) ); Q_memset( m_zombieVersus, 0, sizeof( m_zombieVersus ) );
}
void C_CL4DGameStats::SubmitSessionData()
{
	for ( int i = 1; i < 8; ++i )
	{
		const VoteData_t &vote = m_voteData[i]; KeyValues *row = new KeyValues( "L4D2VoteData" );
		row->SetInt( "VoteID", vote.id ); row->SetInt( "VoteYes", vote.yes ); row->SetInt( "VoteNo", vote.no );
		row->SetInt( "VotePass", vote.passed ); row->SetInt( "VoteFail", vote.failed ); row->SetInt( "VotesTotal", vote.total ); row->SetInt( "VotesInitiated", vote.initiated ); SubmitRow( row, false );
	}
	m_session[2] = Difficulty(); KeyValues *session = new KeyValues( "L4D2SessionData" );
	for ( int i = 0; i < 9; ++i ) session->SetInt( s_sessionNames[i], m_session[i] );
	session->SetString( "MapStartID", m_sessionStartMap ); session->SetString( "MapEndID", m_sessionEndMap ); SubmitRow( session, false );
	KeyValues *config = new KeyValues( "L4D2ConfigData" );
	if ( m_textLanguage[0] ) config->SetString( "TextLanguage", m_textLanguage ); if ( m_audioLanguage[0] ) config->SetString( "AudioLanguage", m_audioLanguage );
	config->SetString( "CountryCode", m_country[0] ? m_country : "00" );
	for ( int i = 0; i < 8; ++i ) config->SetInt( s_configNames[i], m_config[i] ); SubmitRow( config, false );
}
bool C_CL4DGameStats::HaveSessionData() const
{
	if ( m_session[7] ) return true;
	for ( int i = 1; i < 8; ++i ) if ( m_voteData[i].total ) return true; // Native apparent7-column loop inspects only VotesTotal.
	return false;
}
bool C_CL4DGameStats::AddDataForSend( KeyValues *data, StatSendType_t sendType )
{
	if ( !m_sessionEnded )
	{
		if ( Versus() || g_TerrorModes.IsBaseMode( "coop" ) || g_TerrorModes.IsBaseMode( "realism" ) )
		{
			KeyValues *details = TerrorBuildConsumedClientGameProjection();
			if ( details ) { details->SetInt( "Game/chapter", Add32( details->GetInt( "Game/chapter" ), 1 ) ); if ( !g_TerrorModes.GetChapterInfo( details, NULL ) ) m_sessionEnded = true; details->deleteThis(); }
		}
		if ( !m_sessionEnded ) { char map[261]; V_FileBase( engine->GetLevelName(), map, sizeof( map ) ); if ( !map[0] ) m_sessionEnded = true; }
		// Native uploader session-end reasons2/6: SDK has no session-row/backend.
		// Main-approved capability omission is ONLY those setter calls.
	}
	if ( sendType == STATSEND_LEVELSHUTDOWN ) { if ( !m_sessionEnded ) return true; }
	else if ( sendType != STATSEND_APPSHUTDOWN || !HaveSessionData() ) return true;
	UploadSteamUserStats(); SubmitSessionData(); ResetSession();
	GetSteamWorksSGameStatsUploader().EndSession(); // Existing target backend, not a successful native session claim.
	return true;
}

void C_CL4DGameStats::FireGameEvent( IGameEvent *event )
{
	if ( !event || engine->IsPlayingDemo() ) return;
	const char *name = event->GetName(); C_TerrorPlayer *local = LocalStatsPlayer();
	C_TerrorPlayer *user = EventStatsPlayer( event, "userid" );
	VoteData_t &vote = m_voteData[m_currentVoteType];
	if ( !Q_stricmp( name, "vote_cast_no" ) ) { if ( engine->GetLocalPlayer() == event->GetInt( "entityid" ) ) vote.no = Add32( vote.no, 1 ); return; }
	if ( !Q_stricmp( name, "vote_cast_yes" ) ) { if ( engine->GetLocalPlayer() == event->GetInt( "entityid" ) ) vote.yes = Add32( vote.yes, 1 ); return; }
	if ( !Q_stricmp( name, "vote_failed" ) ) { vote.failed = Add32( vote.failed, 1 ); m_currentVoteType = 0; return; }
	if ( !Q_stricmp( name, "vote_passed" ) )
	{
		vote.passed = Add32( vote.passed, 1 );
		if ( m_currentVoteType == 4 || m_currentVoteType == 5 ) m_sessionEnded = true;
		if ( !Q_stricmp( event->GetString( "details", "" ), "#L4D_vote_passed_kick_player" ) )
		{
			player_info_t info;
			// Failed GetPlayerInfo leaves native name uninitialized: skip only comparison.
			if ( engine->GetPlayerInfo( engine->GetLocalPlayer(), &info ) && !Q_stricmp( info.name, event->GetString( "param1", "" ) ) ) { m_session[5] = 1; m_sessionEnded = true; }
		}
		// Main-approved unavailable uploader reason3/4/5 setter calls only omitted.
		m_currentVoteType = 0; return;
	}
	if ( !Q_stricmp( name, "vote_started" ) )
	{
		static const char *const issues[8] = { "", "#L4D_vote_kick_player", "#L4D_vote_change_difficulty", "#L4D_vote_restart_game", "#L4D_vote_return_to_lobby", "#L4D_vote_mission_change", "#L4D_vote_alltalk_change", "#L4D_vote_versus_level_restart" };
		m_currentVoteType = 0; for ( int i = 1; i < 8; ++i ) if ( !Q_stricmp( event->GetString( "issue", "" ), issues[i] ) ) { m_currentVoteType = i; break; }
		VoteData_t &started = m_voteData[m_currentVoteType]; started.total = Add32( started.total, 1 );
		if ( engine->GetLocalPlayer() == event->GetInt( "initiator" ) ) started.initiated = Add32( started.initiated, 1 ); return;
	}
	if ( !Q_stricmp( name, "player_hurt_concise" ) )
	{
		if ( event->GetInt( "attackerentid" ) && event->GetInt( "dmg_health" ) )
		{
			C_TerrorPlayer *attacker = ToTerrorPlayer( ClientEntityList().GetBaseEntity( event->GetInt( "attackerentid" ) ) );
			if ( attacker && ( attacker->GetTeamNumber() == TEAM_SURVIVOR || attacker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && user && ( user->GetTeamNumber() == TEAM_SURVIVOR || user->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
			{
				if ( local == user ) m_session[0] = Add32( m_session[0], 1 ); else if ( local == attacker ) m_friendlyDamage = Add32( m_friendlyDamage, event->GetInt( "dmg_health" ) );
			}
		}
		return;
	}
	if ( !Q_stricmp( name, "achievement_earned" ) ) { if ( engine->GetLocalPlayer() == event->GetInt( "player" ) ) m_session[3] = Add32( m_session[3], 1 ); return; }
	if ( !Q_stricmp( name, "player_incapacitated" ) ) { if ( g_TerrorModes.IsPlayerControlledZombies() && local && local->GetTeamNumber() != TEAM_SURVIVOR && local == EventStatsPlayer( event, "attacker" ) ) m_zombieVersus[1] = Add32( m_zombieVersus[1], 1 ); return; }
	if ( !Q_stricmp( name, "player_death" ) )
	{
		if ( g_TerrorModes.IsPlayerControlledZombies() && local )
		{
			if ( local == user && local->GetTeamNumber() == TEAM_INFECTED ) RecordSpecialLife( true );
			if ( local == EventStatsPlayer( event, "attacker" ) && user && local != user && local->GetTeamNumber() == TEAM_SURVIVOR && user->GetTeamNumber() == TEAM_INFECTED ) m_survivorVersus[2] = Add32( m_survivorVersus[2], 1 );
		}
		return;
	}
	if ( !Q_stricmp( name, "finale_vehicle_leaving" ) )
	{
		if ( local && local->GetTeamNumber() == TEAM_SURVIVOR ) { if ( StandingSurvivors() > 0 ) m_session[6] = 1; if ( Versus() ) m_versus[1] = Add32( m_versus[1], 1 ); m_games[1] = Add32( m_games[1], 1 ); m_recent[0] = 1; } return;
	}
	if ( !Q_stricmp( name, "round_end" ) )
	{
		if ( !local || !TerrorGameRules() ) return;
		if ( g_TerrorModes.IsPlayerControlledZombies() )
		{
			if ( local->GetTeamNumber() == TEAM_SURVIVOR )
			{
				if ( Versus() && (unsigned int)( local->m_iVersusTeam - 1 ) < 2u ) m_survivorVersus[1] = TerrorGameRules()->GetVersusScore( local->m_iVersusTeam, false );
				else if ( Scavenge() && g_PR ) m_survivorVersus[1] = TerrorGameRules()->GetScavengeTeamScore( g_PR->GetTeam( local->entindex() ), -1 );
				if ( StandingSurvivors() ) m_survivorVersus[0] = 1;
			}
			else if ( local->GetTeamNumber() == TEAM_INFECTED )
			{
				const int enemy = 2 - ( local->m_iVersusTeam != 1 );
				if ( Versus() ) m_zombieVersus[11] = TerrorGameRules()->GetVersusScore( enemy, false ); else if ( Scavenge() ) m_zombieVersus[11] = TerrorGameRules()->GetScavengeTeamScore( enemy, -1 );
				if ( !StandingSurvivors() ) m_zombieVersus[0] = 1;
			}
		}
		const int reason = event->GetInt( "reason" );
		if ( ( reason == 5 || reason == 9 || reason == 12 ) && g_TerrorModes.IsPlayerControlledZombies() && local->GetTeamNumber() == TEAM_INFECTED )
		{
			if ( local->m_lifeState == LIFE_ALIVE && !local->m_isGhost ) RecordSpecialLife( true );
			if ( TerrorGameRules()->IsScavengeSecondHalf() ) SubmitCompetitiveData();
		}
		return;
	}
	if ( !Q_stricmp( name, "player_spawn" ) )
	{
		if ( g_TerrorModes.IsPlayerControlledZombies() && local )
		{
			if ( !m_playedInfected && local->GetTeamNumber() == TEAM_INFECTED ) m_playedInfected = true;
			else if ( !m_playedSurvivor && local->GetTeamNumber() == TEAM_SURVIVOR ) { m_playedSurvivor = true; RecordGamePlayed(); }
		}
		return;
	}
	if ( !Q_stricmp( name, "heal_success" ) ) { if ( local && local == user ) { const int i = local == EventStatsPlayer( event, "subject" ) ? 1 : 3; m_total[i] = Add32( m_total[i], 1 ); } return; }
	if ( !Q_stricmp( name, "tank_spawn" ) ) { if ( local && ( ( local == user && !local->m_isGhost ) || ( ( local != user || local->m_isGhost ) && local->GetTeamNumber() == TEAM_INFECTED && local->GetZombieClass() == 8 ) ) ) RecordSpecialLife( true ); return; }
	if ( !Q_stricmp( name, "weapon_given" ) ) { if ( local && local == EventStatsPlayer( event, "giver" ) ) { const int w = event->GetInt( "weapon" ); if ( w == 15 ) m_total[15] = Add32( m_total[15], 1 ); else if ( w == 23 ) m_total[19] = Add32( m_total[19], 1 ); } return; }
	if ( !Q_stricmp( name, "defibrillator_used" ) ) { if ( local && local == user ) m_total[20] = Add32( m_total[20], 1 ); else if ( local && local == EventStatsPlayer( event, "subject" ) ) m_total[21] = Add32( m_total[21], 1 ); return; }
	if ( !Q_stricmp( name, "pills_used" ) ) { if ( local && local == EventStatsPlayer( event, "subject" ) ) m_total[2] = Add32( m_total[2], 1 ); return; } // Native subject bug.
	if ( !Q_stricmp( name, "adrenaline_used" ) ) { if ( local && local == user ) m_total[18] = Add32( m_total[18], 1 ); return; }
	if ( !Q_stricmp( name, "difficulty_changed" ) ) { m_recent[3] = Difficulty(); return; }
	if ( !Q_stricmp( name, "survival_round_start" ) ) { if ( Survival() ) { RecordGamePlayed(); if ( local ) RecordCharacterPlayed( local->GetSurvivorCharacter() ); } return; }
	if ( !Q_stricmp( name, "player_first_spawn" ) ) { if ( local && local == user && !Survival() && !g_TerrorModes.IsPlayerControlledZombies() ) RecordGamePlayed(); return; }
	if ( !Q_stricmp( name, "ghost_spawn_time" ) ) { if ( local && local == user ) m_zombieVersus[5] = Add32( m_zombieVersus[5], event->GetInt( "spawntime" ) ); return; }
	if ( !Q_stricmp( name, "jockey_ride" ) ) { if ( local && local == user ) { int &count = Versus() ? m_special[5].attacks : m_special[5].scavengeAttacks; count = Add32( count, 1 ); } return; } // Earlier native branch shadows later Rides increment.
	if ( !Q_stricmp( name, "entered_spit" ) ) { if ( local && user != local && local->GetTeamNumber() == TEAM_INFECTED && local->GetZombieClass() == 4 ) { int &count = Versus() ? m_special[4].attacks : m_special[4].scavengeAttacks; count = Add32( count, 1 ); } return; }
	if ( !Q_stricmp( name, "gascan_pour_interrupted" ) ) return; // Native46B20 queries class only; NO increment.
	if ( local )
	{
		int *row = m_scavenge[ScavengeMap()];
		if ( !Q_stricmp( name, "gas_can_forced_drop" ) ) { if ( Scavenge() && user == local ) row[8] = Add32( row[8], 1 ); return; }
		if ( !Q_stricmp( name, "gascan_pour_completed" ) ) { if ( Scavenge() && user && user->GetTeamNumber() == local->GetTeamNumber() ) row[7] = Add32( row[7], 1 ); return; }
		if ( !Q_stricmp( name, "scavenge_round_start" ) ) { if ( local->GetTeamNumber() == TEAM_SURVIVOR ) row[6] = Add32( row[6], 1 ); return; }
		if ( !Q_stricmp( name, "scavenge_round_finished" ) ) { RecordScavengeRound(); if ( local->GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules() ) row[5] = Add32( row[5], Truncate( (float)TerrorGameRules()->GetTeamRoundDuration( TEAM_SURVIVOR ) ) ); return; }
		if ( !Q_stricmp( name, "scavenge_match_finished" ) )
		{
			RecordScavengeRound(); row[13] = MAX( row[13], m_matchScore ); m_matchScore = 0;
			// Only the native undefined resource/rules reads are skipped if absent.
			if ( g_PR && TerrorGameRules() )
			{
				const int i = event->GetInt( "winners" ) == TerrorGameRules()->GetScavengeTeamIndex( g_PR->GetTeam( local->entindex() ) ) ? 3 : 4;
				row[i] = Add32( row[i], 1 );
			}
			return;
		}
		if ( !Q_stricmp( name, "scavenge_gas_can_destroyed" ) ) { if ( Scavenge() && user == local ) { if ( local->GetTeamNumber() == TEAM_SURVIVOR ) row[9] = Add32( row[9], 1 ); else if ( local->GetTeamNumber() == TEAM_INFECTED ) { row[10] = Add32( row[10], 1 ); if ( local->GetZombieClass() == 4 ) { int &count = Versus() ? m_special[4].attacks : m_special[4].scavengeAttacks; count = Add32( count, 1 ); } } } return; }
	}
	if ( g_TerrorModes.IsPlayerControlledZombies() && local )
	{
		if ( !Q_stricmp( name, "player_now_it" ) ) { if ( event->GetBool( "by_boomer" ) && local == EventStatsPlayer( event, "attacker" ) ) m_zombieVersus[7] = Add32( m_zombieVersus[7], 1 ); }
		else if ( local == user )
		{
			int index = !Q_stricmp( name, "lunge_pounce" ) ? 8 : !Q_stricmp( name, "tongue_grab" ) ? 9 : !Q_stricmp( name, "choke_start" ) ? 10 : !Q_stricmp( name, "charger_charge_start" ) ? 12 : !Q_stricmp( name, "spit_burst" ) ? 13 : -1;
			if ( index >= 0 ) m_zombieVersus[index] = Add32( m_zombieVersus[index], 1 );
		}
	}
}
