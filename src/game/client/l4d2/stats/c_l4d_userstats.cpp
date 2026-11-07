//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/c_l4d_gamestats.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_playerresource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "filesystem.h"
#include "KeyValues.h"
#include <limits.h>
#include <xmmintrin.h>
#include "l4d2/stats/c_l4d_stat_names.inc"
#include "tier0/memdbgon.h"

ConVar steam_stats_write_to_keyvalue( "steam_stats_write_to_keyvalue", "0", 0x0000000A,
	"If enabled, steam stats will be written to a keyvalue." );

namespace
{
	int Add32( int a, int b ) { return (int)( (unsigned int)a + (unsigned int)b ); }
	int Mul32( int a, int b ) { return (int)( (unsigned int)a * (unsigned int)b ); }
	int Truncate( float value ) { return _mm_cvtt_ss2si( _mm_set_ss( value ) ); }
	bool Divide( int numerator, int denominator, int &result )
	{
		// Native IDIV faults are undefined. Skip only the undefined quotient.
		if ( !denominator || ( numerator == INT_MIN && denominator == -1 ) ) return false;
		result = numerator / denominator;
		return true;
	}
}

void C_CL4DGameStats::ClearUserStats()
{
	// C2459F0. Keep the original debug-root leak: Clear, then lose the pointer.
	Q_memset( m_special, 0, sizeof( m_special ) );
	Q_memset( m_favorite, 0, sizeof( m_favorite ) );
	Q_memset( m_recent, 0, sizeof( m_recent ) );
	Q_memset( m_versus, 0, sizeof( m_versus ) );
	Q_memset( m_average, 0, sizeof( m_average ) );
	Q_memset( m_difficulty, 0, sizeof( m_difficulty ) );
	Q_memset( m_weaponStats, 0, sizeof( m_weaponStats ) );
	Q_memset( m_total, 0, sizeof( m_total ) );
	Q_memset( m_games, 0, sizeof( m_games ) );
	Q_memset( m_lifeDamage, 0, sizeof( m_lifeDamage ) );
	Q_memset( m_poursInterrupted, 0, sizeof( m_poursInterrupted ) );
	Q_memset( m_scavenge, 0, sizeof( m_scavenge ) );
	Q_memset( m_rematchStats, 0, sizeof( m_rematchStats ) );
	m_teamRevived = m_teamProtected = m_previousScore = m_friendlyDamage = m_matchScore = 0;
	m_currentClass = -1;
	m_statsRequested = m_statsReady = false;
	if ( m_pSteamStatsDebug ) m_pSteamStatsDebug->Clear();
	m_pSteamStatsDebug = NULL;
}

void C_CL4DGameStats::TransferInt( const char *key, int &value, bool download )
{
	ISteamUserStats *stats = steamapicontext ? steamapicontext->SteamUserStats() : NULL;
	if ( download )
	{
		if ( stats && !stats->GetStat( key, &value ) ) DevMsg( "Failed Reading Steam User Stats: %s\n", key );
	}
	else if ( steam_stats_write_to_keyvalue.GetBool() ) m_pSteamStatsDebug->SetInt( key, value );
	else if ( stats ) stats->SetStat( key, value );
}
void C_CL4DGameStats::TransferFloat( const char *key, float &value, bool download )
{
	ISteamUserStats *stats = steamapicontext ? steamapicontext->SteamUserStats() : NULL;
	if ( download )
	{
		if ( stats && !stats->GetStat( key, &value ) ) DevMsg( "Failed Reading Steam User Stats: %s\n", key );
	}
	else if ( steam_stats_write_to_keyvalue.GetBool() ) m_pSteamStatsDebug->SetFloat( key, value );
	else if ( stats ) stats->SetStat( key, value );
}

// C245CC0..2465A0 / C247650..248100. One typed implementation of the
// native tables; no raw-offset object overlay and no parallel persistence store.
void C_CL4DGameStats::TransferUserStats( bool download, bool weaponsOnly, bool printing )
{
	char key[128];
	if ( !weaponsOnly )
	{
		static int SpecialStats_t::*const members[8] =
		{
			&SpecialStats_t::mostDamage, &SpecialStats_t::averageLife, &SpecialStats_t::spawns, &SpecialStats_t::life,
			&SpecialStats_t::attacks, &SpecialStats_t::scavengeAttacks, &SpecialStats_t::scavengeDamage, &SpecialStats_t::interruptedPours
		};
		for ( int playerClass = 0; playerClass < 7; ++playerClass )
			for ( int stat = 0; stat < 8; ++stat )
			{
				Q_snprintf( key, sizeof( key ), "Stat.%s.%s", s_specialStatNames[stat], s_specialNames[playerClass] );
				TransferInt( key, m_special[playerClass].*members[stat], download );
			}
		for ( int i = 0; i < 8; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Fav", s_favoriteNames[i] ); TransferInt( key, m_favorite[i], download ); }
		if ( !download ) for ( int i = 0; i < 5; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Recent", s_recentNames[i] ); TransferInt( key, m_recent[i], false ); }
		for ( int i = 0; i < 6; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Versus", s_versusNames[i] ); TransferInt( key, m_versus[i], download ); }
		if ( !download )
		{
			// Native247990 always takes the actual Steam rate API, even in debug-KV mode.
			// The command can run with no Steam context. Skip only that native
			// NULL dereference, still generating every local debug field.
			if ( steamapicontext && steamapicontext->SteamUserStats() ) steamapicontext->SteamUserStats()->UpdateAvgRateStat( "Stat.InfectedKilledHour.Avg", (float)m_total[0], 3600.0 );
		}
		for ( int i = download ? 0 : 1; i < 13; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Avg", s_averageNames[i] ); TransferFloat( key, m_average[i], download ); }
		for ( int i = 0; i < 3; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Diff", s_difficultyNames[i] ); TransferInt( key, m_difficulty[i], download ); }
	}
	// Native general store deliberately DOES NOT write the weapon family.
	if ( download || weaponsOnly || printing )
	{
		for ( int column = 0; column < 7; ++column )
			for ( int weapon = 0; weapon < 39; ++weapon )
			{
				if ( weapon >= 20 && weapon <= 22 ) continue;
				Q_snprintf( key, sizeof( key ), "Stat.%s.%s.Total", s_statWeaponNames[weapon], s_statColumnNames[column] );
				TransferInt( key, m_weaponStats[column][weapon], download );
			}
		for ( int weapon = 20; weapon <= 22; ++weapon )
		{
			Q_snprintf( key, sizeof( key ), "Stat.%s.%s.Total", s_statWeaponNames[weapon], s_statColumnNames[0] );
			TransferInt( key, m_weaponStats[0][weapon], download );
			Q_snprintf( key, sizeof( key ), "Stat.%s.%s.Total", s_statWeaponNames[weapon], s_statColumnNames[2] );
			TransferInt( key, m_weaponStats[2][weapon], download );
		}
	}
	if ( weaponsOnly ) return;
	for ( int i = 0; i < 22; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Total", s_totalNames[i] ); TransferInt( key, m_total[i], download ); }
	for ( int i = 0; i < 20; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Total", s_gameNames[i] ); TransferInt( key, m_games[i], download ); }
	for ( int map = 0; map < 21; ++map )
		for ( int i = 0; i < 14; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.Scavenge.%s", s_scavengeNames[i], s_scavengeMaps[map] ); TransferInt( key, m_scavenge[map][i], download ); }
	for ( int i = 0; i < 8; ++i ) { Q_snprintf( key, sizeof( key ), "Stat.%s.VoteRematch", s_rematchNames[i] ); TransferInt( key, m_rematchStats[i], download ); }
}
void C_CL4DGameStats::DownloadUserStats() { TransferUserStats( true, false ); }
void C_CL4DGameStats::FinishSteamStore( const char *filename )
{
	if ( steam_stats_write_to_keyvalue.GetBool() && m_pSteamStatsDebug && filesystem )
	{
		m_pSteamStatsDebug->SaveToFile( filesystem, filename, "MOD" );
		m_pSteamStatsDebug->Clear();
	}
	else steamapicontext->SteamUserStats()->StoreStats();
}

void C_CL4DGameStats::ComputeWeaponStatistics()
{
	// C2474D0. Tiers are index ranges, NOT current weapon tiers or ammo types.
	for ( int tier = 0; tier < 2; ++tier )
	{
		int sum = 0, best = 0;
		for ( int i = tier ? 6 : 0; i < ( tier ? 19 : 6 ); ++i )
		{
			sum = Add32( sum, m_weaponStats[2][i] );
			if ( m_weaponStats[2][i] > best ) { best = m_weaponStats[2][i]; m_favorite[tier] = i; }
		}
		if ( sum ) Divide( Mul32( 100, best ), sum, m_favorite[tier + 2] );
	}
	for ( int weapon = 0; weapon < 39; ++weapon )
	{
		int shots = m_weaponStats[0][weapon];
		if ( weapon == 4 || weapon == 5 ) shots = Mul32( shots, 8 );
		else if ( weapon == 6 || weapon == 7 ) shots = Mul32( shots, 9 );
		if ( shots )
		{
			Divide( Mul32( 100, m_weaponStats[5][weapon] ), shots, m_weaponStats[3][weapon] );
			Divide( Mul32( 100, m_weaponStats[1][weapon] ), shots, m_weaponStats[4][weapon] );
		}
	}
}
void C_CL4DGameStats::ComputeUserStatistics()
{
	// Exact C248380 order:47380,46E90,46FB0,47180,473D0,47470.
	m_total[17] = Add32( m_total[17], m_teamProtected );
	if ( m_teamProtected > m_total[5] ) { m_total[5] = m_teamProtected; m_difficulty[1] = Difficulty(); }
	m_total[16] = Add32( m_total[16], m_teamRevived );
	if ( m_teamRevived > m_total[4] ) { m_total[4] = m_teamRevived; m_difficulty[0] = Difficulty(); }
	if ( m_games[0] )
	{
		static const int totalIndex[11] = { 1, 3, 2, 6, 7, 15, 16, 17, 18, 19, 20 };
		static const int averageIndex[11] = { 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12 };
		for ( int i = 0; i < 11; ++i ) m_average[averageIndex[i]] = (float)m_total[totalIndex[i]] / (float)m_games[0];
	}
	int sum = 0, best = 0;
	for ( int i = 0; i < 14; ++i ) { sum = Add32( sum, m_games[i + 2] ); best = MAX( best, m_games[i + 2] ); }
	if ( sum ) m_favorite[6] = Truncate( (float)( (float)best * 100.0f ) / (float)sum );
	m_favorite[4] = 0;
	for ( int i = 0; i < 14; ++i ) if ( best == m_games[i + 2] ) { m_favorite[4] = i + 1; break; }
	sum = 0; best = -1;
	for ( int i = 0; i < 4; ++i ) { sum = Add32( sum, m_total[i + 10] ); best = MAX( best, m_total[i + 10] ); }
	if ( sum > 0 ) m_favorite[7] = Truncate( (float)( (float)best * 100.0f ) / (float)sum );
	m_favorite[5] = 0;
	for ( int i = 0; i < 4; ++i ) if ( best == m_total[i + 10] ) { m_favorite[5] = i; break; }
	for ( int i = 0; i < 7; ++i )
	{
		if ( m_special[i].spawns ) Divide( m_special[i].life, m_special[i].spawns, m_special[i].averageLife );
		if ( g_TerrorModes.IsBaseMode( "scavenge" ) ) m_special[i].scavengeDamage = MAX( m_special[i].scavengeDamage, m_lifeDamage[i] );
		else m_special[i].mostDamage = MAX( m_special[i].mostDamage, m_lifeDamage[i] );
		m_special[i].interruptedPours = MAX( m_special[i].interruptedPours, m_poursInterrupted[i] );
	}
	best = m_versus[2];
	int bestClass = -1;
	for ( int i = 0; i < 7; ++i ) if ( m_special[i].mostDamage > best ) { best = m_special[i].mostDamage; bestClass = i; }
	if ( bestClass != -1 ) { m_versus[2] = best; m_versus[5] = bestClass; }
	m_total[8] = Add32( m_total[8], m_friendlyDamage );
	if ( m_friendlyDamage > m_total[9] ) { m_total[9] = m_friendlyDamage; m_difficulty[2] = Difficulty(); }
	int quotient;
	if ( Divide( m_total[8], m_games[0], quotient ) ) m_average[6] = (float)quotient; // Native integer division BEFORE float store.
}
void C_CL4DGameStats::UploadWeaponStats()
{
	// C2484E0 has NO stats-ready guard. A failed Steam call stays a failure.
	if ( !steamapicontext || !steamapicontext->SteamUserStats() ) return;
	if ( steam_stats_write_to_keyvalue.GetBool() && !m_pSteamStatsDebug ) m_pSteamStatsDebug = new KeyValues( "l4d_WeaponStatData" );
	ComputeWeaponStatistics();
	// C247720 writes favorites in the GENERAL family only, not this root.
	TransferUserStats( false, true );
	FinishSteamStore( "steamweaponstats.dat" );
}
void C_CL4DGameStats::UploadSteamUserStats()
{
	if ( !steamapicontext || !steamapicontext->SteamUser() || !steamapicontext->SteamUtils() || !steamapicontext->SteamFriends() || !steamapicontext->SteamUser()->BLoggedOn() )
	{
		DevMsg( "Not logged into steam\n" ); return;
	}
	if ( ( g_TerrorModes.IsPlayerControlledZombies() && ( !m_playedSurvivor || !m_playedInfected ) ) || m_session[7] < 120 ) { ClearUserStats(); return; }
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return;
	// C246740 mutates gameplay time BEFORE the C248380 readiness admission.
	m_games[16] = Add32( m_games[16], m_session[7] );
	if ( m_recent[2] >= 1 && m_recent[2] <= 14 ) m_games[m_recent[2] + 1] = Add32( m_games[m_recent[2] + 1], m_session[7] );
	m_recent[1] = m_session[7];
	if ( !g_TerrorModes.IsBaseMode( "survival" ) ) RecordCharacterPlayed( player->GetSurvivorCharacter() );
	StoreUserStats();
}
void C_CL4DGameStats::StoreUserStats()
{
	if ( !m_statsReady || !steamapicontext || !steamapicontext->SteamUserStats() ) return;
	if ( steam_stats_write_to_keyvalue.GetBool() && !m_pSteamStatsDebug ) m_pSteamStatsDebug = new KeyValues( "l4d_SteamStatData" );
	ComputeUserStatistics();
	TransferUserStats( false, false );
	FinishSteamStore( "steamstats.dat" );
}
void C_CL4DGameStats::OnUserStatsReceived( UserStatsReceived_t *received )
{
	if ( !m_statsRequested || !steamapicontext || !steamapicontext->SteamUserStats() ) return;
	if ( received->m_eResult == k_EResultOK ) { ClearUserStats(); DownloadUserStats(); m_statsReady = true; }
	else { ConVarRef lan( "sv_lan" ); if ( !lan.GetBool() ) DevMsg( "C_CL4DGameStats: failed to download stats from Steam, EResult %d\n", received->m_eResult ); }
}
void C_CL4DGameStats::OnUserStatsStored( UserStatsStored_t *stored )
{
	if ( stored->m_eResult != k_EResultOK ) DevMsg( "C_CL4DGameStats: failed to upload stats to Steam, EResult %d\n", stored->m_eResult );
}

void C_CL4DGameStats::ResetSteamStats()
{
	// C248700: the saved flag is read AFTER Clear, so it is always false.
	ClearUserStats();
	const bool ready = m_statsReady;
	m_statsReady = true;
	StoreUserStats();
	UploadWeaponStats();
	m_statsReady = ready;
}
void C_CL4DGameStats::PrintSteamStats()
{
	// C248730 does NOT recompute derived values or request/download stats.
	if ( !m_pSteamStatsDebug ) m_pSteamStatsDebug = new KeyValues( "l4d_SteamStatData" );
	const bool oldDebug = steam_stats_write_to_keyvalue.GetBool();
	steam_stats_write_to_keyvalue.SetValue( 1 );
	TransferUserStats( false, false, true );
	steam_stats_write_to_keyvalue.SetValue( oldDebug ? 1 : 0 );
	if ( m_pSteamStatsDebug && filesystem )
	{
		m_pSteamStatsDebug->SaveToFile( filesystem, "activesteamstats.dat", "MOD" );
		m_pSteamStatsDebug->Clear();
	}
}
