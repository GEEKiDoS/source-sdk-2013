//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/terror_melee_weapon_parse.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "KeyValues.h"
#include "steamworks_gamestats.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/nav/terror_nav.h"
#include <xmmintrin.h>
extern bool IsInCommentaryMode();
#include "tier0/memdbgon.h"

static CL4DGameStats g_L4DGameStats;
CL4DGameStats &L4DGameStats() { return g_L4DGameStats; }

static ConVar l4d_stats_track( "l4d_stats_track", "1", 0x00000016, "Turn on//off L4D stats tracking." );
static ConVar l4d_stats_nogameplaycheck( "l4d_stats_nogameplaycheck", "0", 0x00002006, "Disable normal check for valid gameplay, send stats regardless." );

static const char *const s_weaponNames[39] =
{
	"pistol", "smg", "smg_silenced", "smg_mp5", "pumpshotgun", "shotgun_chrome", "autoshotgun", "shotgun_spas",
	"rifle", "rifle_desert", "rifle_ak47", "rifle_sg552", "hunting_rifle", "sniper_military", "sniper_awp", "sniper_scout",
	"pistol_magnum", "grenade_launcher", "rifle_m60", "machinegun", "molotov", "pipe_bomb", "vomitjar", "baseball_bat",
	"chainsaw", "cricket_bat", "crowbar", "electric_guitar", "fireaxe", "frying_pan", "katana", "machete", "tonfa",
	"golfclub", "pitchfork", "shovel", "knife", "incendiary_ammo", "explosive_ammo"
};
static const char *const s_survivorNames[L4DSTAT_COUNT] =
{
	"MedkitsUsed", "PillsUsed", "DamageTaken", "ReviveOther", "Incaps", "Deaths", "IncapKills", "FFDamage", "FFHits", "FFIncaps",
	"SelfHealCount", "FriendHealCount", "AverageHealAmount", "MaxDeadTime", "TotalDeadTime", "Protected", "PillsShared", "ThirdStrike",
	"ItemsScavenged", "TotalItems", "FFHitsTaken", "ZombieKills", "FFIncapsDealt", "AdrenalineUsed", "AdrenalineShared",
	"DefibrillatorUsed", "Defibrillated", "PercentTraveled", "CommonKills", "SmokerKills", "BoomerKills", "HunterKills",
	"SpitterKills", "JockeyKills", "ChargerKills", "WitchKills", "TankKills", "IncendiaryBulletsFired", "ExplosiveBulletsFired", "TotalMeleeKills"
};
static const char *const s_mapNames[18] =
{
	"WitchHarassed", "WitchSpawn", "SpawnMOB", "DirectorRelax", "LastSurvivor", "LastSurvivorPanic", "CarAlarm", "CarAlarmActive",
	"InClosetMapEnd", "Rescued", "Commentary", "MissionLost", "StarRating", "PillsConvertedToHealth", "AliveSurvivors", "TanksSpawned", "GameMode", "PillsConvertedToDefib"
};
static CTerrorPlayer *StatsPlayer( IGameEvent *event, const char *field )
{
	return dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByUserId( event->GetInt( field ) ) );
}

void L4DSurvivorStats_t::Clear()
{
	selfHealSum = 0;
	Q_memset( values, 0, sizeof( values ) );
}
CL4DGameStats::CL4DGameStats() : CAutoGameSystem( "CL4DGameStats" ) { Clear(); }
bool CL4DGameStats::Init()
{
	gamestats = this;
	static const char *const events[] =
	{
		"weapon_fire", "infected_hurt", "player_hurt", "weapon_given", "witch_harasser_set", "revive_success", "create_panic_event",
		"survivor_rescued", "item_pickup", "achievement_earned", "heal_success", "player_bot_replace", "bot_player_replace", "player_team",
		"player_incapacitated_start", "award_earned", "choke_stopped", "pounce_stopped", "player_spawn", "player_now_it", "player_first_spawn",
		"map_transition", "round_end", "finale_vehicle_leaving", "mission_lost", "jockey_ride_end", "adrenaline_used", "defibrillator_used"
	};
	for ( int i = 0; i < ARRAYSIZE( events ); ++i ) ListenForGameEvent( events[i] );
	return true;
}
void CL4DGameStats::Shutdown() { StopListeningForAllEvents(); }
void CL4DGameStats::Clear()
{
	m_initialHealthItems = m_zombieDeaths = m_levelStartTime = m_totalTime = m_ties = 0;
	m_teamScore[0] = m_teamScore[1] = -1;
	m_teamWins[0] = m_teamWins[1] = 0;
	Q_memset( m_weaponStats, 0, sizeof( m_weaponStats ) );
	Q_memset( m_playerWeaponStats, 0, sizeof( m_playerWeaponStats ) );
	Q_memset( m_mapData, 0, sizeof( m_mapData ) );
	Q_memset( m_zombieData, 0, sizeof( m_zombieData ) );
	m_survivors.RemoveAll();
	m_achievements.RemoveAll();
	m_durationAttacks.RemoveAll();
	m_deaths.RemoveAll();
	m_stuckZombiePositions.Purge(); // S2A55D0: count and owned allocation are both cleared.
	m_gameMode = 0;
	m_collectionEnded = false;
	m_gameplayEligible = false;
}
void CL4DGameStats::Event_LevelInit()
{
	Clear();
	m_levelStartTime = static_cast< int >( gpGlobals->curtime );
	m_botsHelpTimer.Start();
	if ( TerrorGameRules()->IsCoopMode() ) m_gameMode = engine->IsDedicatedServer() ? 0 : 5;
	else if ( TerrorGameRules()->IsVersusMode() ) m_gameMode = 1;
	else if ( TerrorGameRules()->IsSurvivalMode() ) m_gameMode = 2;
	else if ( TerrorGameRules()->IsScavengeMode() ) m_gameMode = 3;
	else if ( TerrorGameRules()->IsRealismMode() ) m_gameMode = 4;
	ConVarRef mode( "mp_gamemode" );
	const char *name = mode.GetString();
	static const char *const stock[] = { "coop", "realism", "survival", "versus", "teamversus", "scavenge", "teamscavenge" };
	bool standard = false;
	for ( int i = 0; i < ARRAYSIZE( stock ); ++i ) if ( !Q_stricmp( stock[i], name ) ) standard = true;
	if ( !Q_stricmp( name, "mutation12" ) ) m_gameMode = 7;
	else if ( !standard ) m_gameMode = 6;
	m_mapData[16] = m_gameMode;
}
void CL4DGameStats::Event_LevelShutdown( float elapsed )
{
	m_totalTime += static_cast< int >( gpGlobals->curtime - static_cast< float >( m_levelStartTime ) );
}
void CL4DGameStats::Event_PlayerDisconnected( CBasePlayer *player )
{
	if ( player && player->entindex() >= 1 && player->entindex() <= 32 )
		Q_memset( m_playerWeaponStats[player->entindex() - 1], 0, sizeof( m_playerWeaponStats[0] ) );
	if ( m_collectionEnded || TerrorGameRules()->IsPlayerControlledZombies() ) return;
	int humans = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		const CTerrorPlayer *other = dynamic_cast< const CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( other && other->IsConnected() && other->GetTeamNumber() == TEAM_SURVIVOR && !other->IsBot() ) ++humans;
	}
	if ( !humans )
	{
		m_collectionEnded = true;
		m_totalTime += static_cast< int >( gpGlobals->curtime ) - m_levelStartTime;
		m_levelStartTime = static_cast< int >( gpGlobals->curtime );
		AddDataForSend( NULL, STATSEND_LEVELSHUTDOWN );
		m_collectionEnded = true;
	}
}
void CL4DGameStats::Event_FirstAidKitSpawned() { ++m_mapData[13]; }
void CL4DGameStats::Event_DefibrillatorSpawned() { ++m_mapData[17]; }
void CL4DGameStats::SetInitialHealthItemCount( int count ) { m_initialHealthItems = count; }
void CL4DGameStats::Event_ZombieSpawned( int zombieClass, int count )
{
	// S2A3180 accepts signed class<9; a negative index is outside the native
	// array. Main's standing OOB rule skips only that undefined write.
	if ( (unsigned int)zombieClass < 9u )
		m_zombieData[zombieClass] = (int)( (unsigned int)m_zombieData[zombieClass] + (unsigned int)count );
}
void CL4DGameStats::Event_MobSpawned() { ++m_mapData[2]; }
void CL4DGameStats::Event_DirectorRelax() { ++m_mapData[3]; }
void CL4DGameStats::Event_CarAlarmActive()
{
	m_mapData[7] = static_cast< int >( static_cast< unsigned int >( m_mapData[7] ) + 1u );
}

void CL4DGameStats::ReportBotsWontHelp( CTerrorPlayer *helper, CTerrorPlayer *target )
{
	if ( !helper || !target ) return;
	if ( m_botsHelpTimer.GetStartTime() > 0.0f && m_botsHelpTimer.GetElapsedTime() < 120.0f ) return;
	Vector helperOrigin = helper->GetAbsOrigin();
	Vector targetOrigin = target->GetAbsOrigin();
	KeyValues *row = new KeyValues( "L4D2BotsWontHelp" );
	row->SetInt( "TimeSubmitted", static_cast< int >( GetSteamWorksSGameStatsUploader().GetTimeSinceEpoch() ) );
	row->SetString( "MapID", STRING( gpGlobals->mapname ) );
	row->SetString( "HelperName", helper->GetDebugName() );
	row->SetFloat( "HelperX", helperOrigin.x );
	row->SetFloat( "HelperY", helperOrigin.y );
	row->SetFloat( "HelperZ", helperOrigin.z );
	row->SetString( "TargetName", target->GetDebugName() );
	row->SetFloat( "TargetX", targetOrigin.x );
	row->SetFloat( "TargetY", targetOrigin.y );
	row->SetFloat( "TargetZ", targetOrigin.z );
	m_botsHelpTimer.Start();
	EResult result = GetSteamWorksSGameStatsUploader().AddStatsForUpload( row );
	if ( result != k_EResultOK ) row->deleteThis();
}

L4DSurvivorStats_t *CL4DGameStats::GetSurvivorStats( CTerrorPlayer *player )
{
	if ( !player ) return NULL;
	int userID = player->GetUserID();
	FOR_EACH_LL( m_survivors, i ) if ( m_survivors[i].userID == userID ) return &m_survivors[i];
	int index = m_survivors.AddToTail();
	m_survivors[index].userID = userID;
	m_survivors[index].Clear();
	return &m_survivors[index];
}
void CL4DGameStats::ResetSurvivorStats( CTerrorPlayer *player )
{
	L4DSurvivorStats_t *stats = GetSurvivorStats( player );
	if ( stats ) stats->Clear();
}

// S2A3A70 bounds the edict slot, weapon row and column before BOTH mutations.
void CL4DGameStats::RecordWeaponStat( CTerrorPlayer *player, int weapon, int stat, int value )
{
	if ( !player || static_cast< unsigned int >( player->entindex() - 1 ) > 31u ||
		static_cast< unsigned int >( weapon ) > 38u || static_cast< unsigned int >( stat ) > 4u ) return;
	L4DWeaponStats_t &aggregate = m_weaponStats[weapon];
	L4DWeaponStats_t &local = m_playerWeaponStats[player->entindex() - 1][weapon];
	switch ( stat )
	{
	case 0: aggregate.shotsFired += value; local.shotsFired += value; break;
	case 1: aggregate.hits += value; local.hits += value; break;
	case 2: aggregate.damage += value; local.damage += value; break;
	case 3: aggregate.headshots += value; local.headshots += value; break;
	case 4: aggregate.kills += value; local.kills += value; break;
	}
}
int CL4DGameStats::GetWeaponStatID( CTerrorPlayer *player, const char *weapon ) const
{
	if ( !weapon || !player || player->GetTeamNumber() == TEAM_INFECTED ) return -1;
	if ( player->m_usingMountedWeapon || !Q_stricmp( weapon, "prop_minigun" ) || !Q_stricmp( weapon, "prop_minigun_l4d1" ) ) return 19;
	if ( !Q_strncmp( weapon, "weapon_", 7 ) ) weapon += 7;
	if ( !Q_stricmp( weapon, "melee" ) )
	{
		CTerrorMeleeWeapon *melee = dynamic_cast< CTerrorMeleeWeapon * >( player->GetActiveWeapon() );
		if ( melee && melee->GetMeleeInfo() ) weapon = melee->GetMeleeInfo()->m_szName;
	}
	for ( int i = 0; i < ARRAYSIZE( s_weaponNames ); ++i ) if ( !Q_stricmp( s_weaponNames[i], weapon ) ) return i;
	return -1;
}
void CL4DGameStats::Event_VomitJarHit( CTerrorPlayer *player )
{
	if ( player && player->GetTeamNumber() == TEAM_SURVIVOR ) RecordWeaponStat( player, 22, 1, 1 );
}
void CL4DGameStats::SendWeaponStats( CTerrorPlayer *player )
{
	// Original PC VEngineServer022 slot126 is engine1319E0, xor al,al;ret.
	// It is not SDK IsDedicatedServer; PC dedicated clients also receive data.
	if ( !player || player->IsBot() || static_cast< unsigned int >( player->entindex() - 1 ) > 31u ) return;
	L4DWeaponStats_t *rows = m_playerWeaponStats[player->entindex() - 1];
	int first = 0;
	while ( first < 39 && !rows[first].shotsFired ) ++first;
	if ( first == 39 ) return;
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, "SteamWeaponStatData" );
	for ( int i = 0; i < 39; ++i )
	{
		if ( rows[i].shotsFired )
		{
			WRITE_BYTE( i );
			WRITE_BYTE( rows[i].shotsFired );
			WRITE_BYTE( rows[i].hits );
			WRITE_BYTE( rows[i].headshots );
			WRITE_BYTE( rows[i].kills );
			WRITE_BYTE( rows[i].damage );
		}
		Q_memset( &rows[i], 0, sizeof( rows[i] ) );
	}
	MessageEnd();
}

void CL4DGameStats::RecordHurt( IGameEvent *event )
{
	CTerrorPlayer *attacker = StatsPlayer( event, "attacker" );
	if ( !event->GetInt( "attacker" ) ) return;
	bool playerHurt = !Q_stricmp( event->GetName(), "player_hurt" );
	if ( !attacker || attacker->GetTeamNumber() == TEAM_INFECTED )
	{
		if ( playerHurt && attacker && attacker->IsBot() )
		{
			static const int columns[9] = { -1, 17, 18, 19, 22, 21, 23, -1, 20 };
			int zombieClass = attacker->GetZombieClass();
			if ( zombieClass >= 0 && zombieClass < 9 && columns[zombieClass] >= 0 ) m_zombieData[columns[zombieClass]] += event->GetInt( "dmg_health" );
		}
		return;
	}
	int type = event->GetInt( "type" );
	int damage = event->GetInt( playerHurt ? "dmg_health" : "amount" );
	bool headshot = event->GetInt( "hitgroup" ) == 1;
	int weapon = -1;
	if ( type & 2 )
	{
		if ( type & 8 ) weapon = 37;
		else if ( type & 0x40 ) weapon = 38;
	}
	else if ( type & 8 ) weapon = 20;
	else if ( type & 0x40 ) weapon = 21;
	if ( weapon == 37 || weapon == 38 )
	{
		RecordWeaponStat( attacker, weapon, 1, 1 );
		RecordWeaponStat( attacker, weapon, 2, damage );
		if ( headshot ) RecordWeaponStat( attacker, weapon, 3, 1 );
		weapon = -1;
	}
	if ( weapon < 0 && attacker->GetActiveWeapon() ) weapon = GetWeaponStatID( attacker, attacker->GetActiveWeapon()->GetClassname() );
	if ( weapon >= 0 )
	{
		RecordWeaponStat( attacker, weapon, 1, 1 );
		RecordWeaponStat( attacker, weapon, 2, damage );
		if ( headshot ) RecordWeaponStat( attacker, weapon, 3, 1 );
	}
	if ( playerHurt )
	{
		CTerrorPlayer *victim = StatsPlayer( event, "userid" );
		if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR )
		{
			++GetSurvivorStats( victim )->values[L4DSTAT_FF_HITS_TAKEN];
			if ( attacker->GetTeamNumber() == TEAM_SURVIVOR )
			{
				L4DSurvivorStats_t *stats = GetSurvivorStats( attacker );
				++stats->values[L4DSTAT_FF_HITS];
				stats->values[L4DSTAT_FF_DAMAGE] += damage;
			}
		}
	}
}

void CL4DGameStats::Event_Kill( CBaseEntity *victim, const CTakeDamageInfo &info )
{
	CTerrorPlayer *attacker = dynamic_cast< CTerrorPlayer * >( info.GetAttacker() );
	CTerrorPlayer *victimPlayer = dynamic_cast< CTerrorPlayer * >( victim );
	CBaseCombatCharacter *combatVictim = victim ? victim->MyCombatCharacterPointer() : NULL;
	int zombieClass = combatVictim && ( !victimPlayer || victimPlayer->GetTeamNumber() == TEAM_INFECTED ) ? combatVictim->GetZombieClass() : -1;
	if ( zombieClass >= 0 ) ++m_zombieDeaths;
	if ( victimPlayer || ( combatVictim && zombieClass != 0 ) || !combatVictim )
	{
		Death_t death;
		death.position.Init();
		death.victim = 0;
		if ( victimPlayer )
		{
			death.position = victimPlayer->GetAbsOrigin();
			if ( victimPlayer->GetTeamNumber() == TEAM_INFECTED ) death.victim = victimPlayer->GetZombieClass() | 0x40000000;
			else if ( victimPlayer->GetTeamNumber() == TEAM_SURVIVOR ) death.victim = victimPlayer->GetSurvivorCharacter();
		}
		else if ( combatVictim )
		{
			death.position = victim->GetAbsOrigin();
			death.victim = zombieClass | 0x40000000;
		}
		death.killer = 0x3fffffff;
		if ( attacker )
		{
			if ( attacker->GetTeamNumber() == TEAM_INFECTED ) death.killer = attacker->GetZombieClass() | 0x40000000;
			else if ( attacker->GetTeamNumber() == TEAM_SURVIVOR ) death.killer = attacker->GetSurvivorCharacter();
			else death.killer = 0;
		}
		else if ( info.GetAttacker() && info.GetAttacker()->MyCombatCharacterPointer() ) death.killer = info.GetAttacker()->MyCombatCharacterPointer()->GetZombieClass() | 0x40000000;
		death.damageType = info.GetDamageType();
		death.time = static_cast< int >( GetSteamWorksSGameStatsUploader().GetTimeSinceEpoch() );
		m_deaths.AddToTail( death );
	}
	if ( !attacker || attacker->GetTeamNumber() != TEAM_SURVIVOR ) return;
	L4DSurvivorStats_t *stats = GetSurvivorStats( attacker );
	if ( zombieClass >= 0 && zombieClass <= 8 ) ++stats->values[L4DSTAT_COMMON_KILLS + zombieClass];
	++stats->values[L4DSTAT_ZOMBIE_KILLS];
	int type = info.GetDamageType();
	int weapon = -1;
	if ( ( type & 0x40 ) && ( type & 0x08000000 ) ) weapon = 21;
	else if ( ( type & 0x40 ) && ( type & 0x01000000 ) ) weapon = 17;
	else if ( !( type & 2 ) && ( type & 8 ) ) weapon = 20;
	else
	{
		if ( ( type & 2 ) && ( type & 8 ) ) RecordWeaponStat( attacker, 37, 4, 1 );
		else if ( ( type & 2 ) && ( type & 0x40 ) ) RecordWeaponStat( attacker, 38, 4, 1 );
		if ( attacker->GetActiveWeapon() )
		{
			if ( FClassnameIs( attacker->GetActiveWeapon(), "weapon_melee" ) ) ++stats->values[L4DSTAT_MELEE_KILLS];
			weapon = GetWeaponStatID( attacker, attacker->GetActiveWeapon()->GetClassname() );
		}
	}
	if ( weapon >= 0 ) RecordWeaponStat( attacker, weapon, 4, 1 );
}

void CL4DGameStats::RecordIncapacitation( IGameEvent *event )
{
	CTerrorPlayer *victim = StatsPlayer( event, "userid" );
	if ( !victim ) return;
	int column = -1;
	if ( victim->m_pounceAttacker.Get() ) column = 9;
	else if ( victim->m_tongueOwner.Get() ) column = 10;
	else if ( victim->IsIT() ) column = 12;
	else if ( victim->m_jockeyAttacker.Get() ) column = 14;
	else
	{
		CTerrorPlayer *attacker = StatsPlayer( event, "attacker" );
		if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED )
		{
			switch ( attacker->GetZombieClass() ) { case 8: column = 13; break; case 4: column = 16; break; case 6: column = 15; break; }
		}
		else if ( attacker && attacker->GetTeamNumber() == TEAM_SURVIVOR ) ++GetSurvivorStats( attacker )->values[L4DSTAT_FF_INCAPS];
		else
		{
			CBaseEntity *entity = UTIL_EntityByIndex( event->GetInt( "attackerentid" ) );
			if ( entity && FClassnameIs( entity, "witch" ) ) column = 11;
		}
	}
	if ( column >= 0 ) ++m_zombieData[column];
}

#pragma float_control( precise, on, push )
void CL4DGameStats::SnapshotSurvivorStats( CTerrorPlayer *player, L4DSurvivorStats_t &stats )
{
	stats.values[L4DSTAT_MEDKITS_USED] = player->m_checkpointMedkitsUsed;
	stats.values[L4DSTAT_PILLS_USED] = player->m_checkpointPillsUsed;
	stats.values[L4DSTAT_INCAPS] = player->m_checkpointIncaps;
	stats.values[L4DSTAT_DEATHS] = player->m_checkpointDeaths;
	stats.values[L4DSTAT_MELEE_KILLS] = player->m_checkpointMeleeKills;
	stats.values[L4DSTAT_DAMAGE_TAKEN] = player->m_checkpointDamageTaken;
	stats.values[L4DSTAT_INCAP_KILLS] = player->GetIncapacitatedZombieKillCount();
	stats.values[L4DSTAT_MAX_DEAD_TIME] = MAX( 0, _mm_cvtt_ss2si( _mm_set_ss( player->m_maxDeadDuration.Get() ) ) );
	stats.values[L4DSTAT_TOTAL_DEAD_TIME] = MAX( 0, _mm_cvtt_ss2si( _mm_set_ss( player->m_totalDeadDuration.Get() ) ) );
	stats.values[L4DSTAT_AVERAGE_HEAL] = stats.values[L4DSTAT_SELF_HEAL_COUNT] ? stats.selfHealSum / stats.values[L4DSTAT_SELF_HEAL_COUNT] : 0;
	stats.values[L4DSTAT_TOTAL_ITEMS] = m_initialHealthItems;
	if ( TerrorGameRules()->IsVersusMode() )
	{
		float maximum = static_cast< float >( TerrorGameRules()->GetVersusMaxCompletionScore() >> 2 );
		float percent = 0.0f;
		if ( maximum != 0.0f ) percent = ( static_cast< float >( TerrorGameRules()->GetVersusDistancePerSurvivor( player->GetSurvivorCharacter(), player->GetTeamNumber() ) ) / maximum ) * 100.0f;
		stats.values[L4DSTAT_PERCENT_TRAVELED] = MAX( 0, _mm_cvtt_ss2si( _mm_set_ss( percent ) ) );
	}
	else if ( TheNavMesh )
	{
		float flow = player->GetFlowDistance( FLOW_TOWARD_GOAL );
		if ( !( flow >= 0.0f ) ) flow = 0.0f;
		float fraction = flow / TheTerrorNavMesh()->GetMaxNonCheckpointFlowDistance();
		float bounded = 1.0f;
		if ( fraction <= 1.0f ) bounded = fraction >= 0.0f ? fraction : 0.0f;
		stats.values[L4DSTAT_PERCENT_TRAVELED] = static_cast< int >( bounded * 100.0f );
	}
}
#pragma float_control( pop )

void CL4DGameStats::RecordRoundEnd( IGameEvent *event )
{
	if ( !Q_stricmp( event->GetName(), "mission_lost" ) ) ++m_mapData[11];
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR && ( !player->IsAlive() || !player->IsGhost() ) ) SendWeaponStats( player );
	}
	if ( Q_stricmp( event->GetName(), "round_end" ) ) return;
	int reason = event->GetInt( "reason" );
	if ( TerrorGameRules()->IsVersusMode() )
	{
		if ( reason == 5 ) m_teamScore[TerrorGameRules()->IsScavengeSecondHalf() ? 1 : 0] = TerrorGameRules()->GetVersusScore( TerrorGameRules()->GetScavengeTeamIndex( TEAM_SURVIVOR ), false );
		else if ( reason == 6 )
		{
			if ( m_teamScore[0] > m_teamScore[1] ) ++m_teamWins[0];
			else if ( m_teamScore[0] < m_teamScore[1] ) ++m_teamWins[1];
			else ++m_ties;
			SubmitSurvivorStats( true );
		}
	}
	else if ( TerrorGameRules()->IsScavengeMode() && ( reason == 9 || reason == 12 ) )
	{
		int score = TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 );
		if ( TerrorGameRules()->IsScavengeSecondHalf() )
		{
			m_teamScore[1] = score;
			if ( m_teamScore[0] > score ) ++m_teamWins[0];
			else if ( m_teamScore[0] < score ) ++m_teamWins[1];
			else ++m_ties;
			SubmitCompetitiveStats();
			SubmitSurvivorStats( true );
			if ( reason == 12 )
			{
				m_totalTime += static_cast< int >( gpGlobals->curtime ) - m_levelStartTime;
				m_levelStartTime = static_cast< int >( gpGlobals->curtime );
				AddDataForSend( NULL, STATSEND_LEVELSHUTDOWN );
				Event_LevelInit();
			}
		}
		else m_teamScore[0] = score;
	}
	if ( TerrorGameRules()->IsPlayerControlledZombies() && ( reason == 13 || reason == 8 ) )
	{
		m_collectionEnded = true;
		m_totalTime += static_cast< int >( gpGlobals->curtime ) - m_levelStartTime;
		m_levelStartTime = static_cast< int >( gpGlobals->curtime );
		AddDataForSend( NULL, STATSEND_LEVELSHUTDOWN );
	}
}

void CL4DGameStats::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	// Native registers this event but has no matching mutation branch.
	if ( !Q_stricmp( name, "player_first_spawn" ) ) return;
	if ( !Q_stricmp( name, "player_hurt" ) || !Q_stricmp( name, "infected_hurt" ) ) { RecordHurt( event ); return; }
	if ( !Q_stricmp( name, "weapon_fire" ) )
	{
		CTerrorPlayer *player = StatsPlayer( event, "userid" );
		if ( !player || player->GetTeamNumber() == TEAM_INFECTED || !player->GetActiveWeapon() ) return;
		CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( player->GetActiveWeapon() );
		if ( weapon && weapon->HasUpgrade( 0 ) )
		{
			RecordWeaponStat( player, 37, 0, 1 );
			if ( player->GetTeamNumber() == TEAM_SURVIVOR ) ++GetSurvivorStats( player )->values[L4DSTAT_INCENDIARY_FIRED];
		}
		else if ( weapon && weapon->HasUpgrade( 1 ) )
		{
			RecordWeaponStat( player, 38, 0, 1 );
			if ( player->GetTeamNumber() == TEAM_SURVIVOR ) ++GetSurvivorStats( player )->values[L4DSTAT_EXPLOSIVE_FIRED];
		}
		int id = GetWeaponStatID( player, player->GetActiveWeapon()->GetClassname() );
		if ( id >= 0 ) RecordWeaponStat( player, id, 0, 1 );
		return;
	}
	if ( !Q_stricmp( name, "player_bot_replace" ) ) { ResetSurvivorStats( StatsPlayer( event, "player" ) ); return; }
	if ( !Q_stricmp( name, "bot_player_replace" ) ) { ResetSurvivorStats( StatsPlayer( event, "bot" ) ); return; }
	if ( !Q_stricmp( name, "player_team" ) ) { ResetSurvivorStats( StatsPlayer( event, "userid" ) ); return; }
	if ( !Q_stricmp( name, "achievement_earned" ) ) { m_achievements.AddToTail( event->GetInt( "achievement" ) ); return; }
	if ( !Q_stricmp( name, "create_panic_event" ) ) { ++m_mapData[6]; return; }
	if ( !Q_stricmp( name, "witch_harasser_set" ) ) { ++m_mapData[0]; return; }
	if ( !Q_stricmp( name, "survivor_rescued" ) ) { ++m_mapData[9]; return; }
	if ( !Q_stricmp( name, "player_incapacitated_start" ) ) { RecordIncapacitation( event ); return; }
	if ( !Q_stricmp( name, "round_end" ) || !Q_stricmp( name, "map_transition" ) || !Q_stricmp( name, "finale_vehicle_leaving" ) || !Q_stricmp( name, "mission_lost" ) )
	{ RecordRoundEnd( event ); return; }
	if ( !Q_stricmp( name, "player_spawn" ) )
	{
		CTerrorPlayer *player = StatsPlayer( event, "userid" );
		if ( player && player->GetTeamNumber() == TEAM_INFECTED && TerrorGameRules()->IsPlayerControlledZombies() && player->GetZombieClass() < 9 ) ++m_zombieData[player->GetZombieClass()];
		return;
	}
	if ( !Q_stricmp( name, "player_now_it" ) )
	{
		if ( event->GetBool( "by_boomer" ) )
		{
			CTerrorPlayer *victim = StatsPlayer( event, "userid" );
			if ( victim ) { DurationAttack_t sample = { 2, victim->GetBileRemainingTimeForStats(), false }; m_durationAttacks.AddToTail( sample ); }
		}
		else Event_VomitJarHit( StatsPlayer( event, "attacker" ) );
		return;
	}
	if ( !Q_stricmp( name, "pounce_stopped" ) || !Q_stricmp( name, "choke_stopped" ) )
	{
		CTerrorPlayer *victim = StatsPlayer( event, "victim" );
		if ( !victim ) return;
		CTerrorPlayer *rescuer = StatsPlayer( event, "userid" );
		bool saved = rescuer && ( rescuer->GetTeamNumber() == TEAM_SURVIVOR || rescuer->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
		bool pounce = !Q_stricmp( name, "pounce_stopped" );
		DurationAttack_t sample = { pounce ? 0 : 1, pounce ? victim->GetPounceElapsedTimeForStats() : victim->GetTongueElapsedTimeForStats(), saved };
		m_durationAttacks.AddToTail( sample );
		return;
	}
	if ( !Q_stricmp( name, "jockey_ride_end" ) )
	{
		event->GetFloat( "ride_length" ); // Native ignored-return bug: duration stays zero.
		DurationAttack_t sample = { 3, 0.0f, event->GetInt( "rescuer" ) != 0 };
		m_durationAttacks.AddToTail( sample );
		return;
	}
	if ( !Q_stricmp( name, "defibrillator_used" ) )
	{
		CTerrorPlayer *user = StatsPlayer( event, "userid" );
		CTerrorPlayer *subject = StatsPlayer( event, "subject" );
		if ( user && user->GetTeamNumber() == TEAM_SURVIVOR ) ++GetSurvivorStats( user )->values[L4DSTAT_DEFIBRILLATOR_USED];
		if ( subject && subject->GetTeamNumber() == TEAM_SURVIVOR ) ++GetSurvivorStats( subject )->values[L4DSTAT_DEFIBRILLATED];
		return;
	}
	CTerrorPlayer *player = StatsPlayer( event, "userid" );
	if ( !Q_stricmp( name, "weapon_given" ) ) player = StatsPlayer( event, "giver" );
	if ( !Q_stricmp( name, "revive_success" ) ) player = StatsPlayer( event, "subject" );
	if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR ) return;
	L4DSurvivorStats_t *stats = GetSurvivorStats( player );
	if ( !Q_stricmp( name, "heal_success" ) )
	{
		if ( event->GetInt( "userid" ) == event->GetInt( "subject" ) )
		{
			++stats->values[L4DSTAT_SELF_HEAL_COUNT];
			stats->selfHealSum += event->GetInt( "health_restored" ) + 1;
		}
		else ++stats->values[L4DSTAT_FRIEND_HEAL_COUNT];
	}
	else if ( !Q_stricmp( name, "award_earned" ) && event->GetInt( "award" ) == 67 ) ++stats->values[L4DSTAT_PROTECTED];
	else if ( !Q_stricmp( name, "weapon_given" ) )
	{
		if ( event->GetInt( "weapon" ) == 15 ) ++stats->values[L4DSTAT_PILLS_SHARED];
		else if ( event->GetInt( "weapon" ) == 23 ) ++stats->values[L4DSTAT_ADRENALINE_SHARED];
	}
	else if ( !Q_stricmp( name, "item_pickup" ) && gpGlobals->curtime - static_cast< float >( m_levelStartTime ) > 60.0f ) ++stats->values[L4DSTAT_ITEMS_SCAVENGED];
	else if ( !Q_stricmp( name, "revive_success" ) && event->GetBool( "lastlife" ) ) ++stats->values[L4DSTAT_THIRD_STRIKE];
	else if ( !Q_stricmp( name, "adrenaline_used" ) ) ++stats->values[L4DSTAT_ADRENALINE_USED];
}

void CL4DGameStats::SubmitRow( KeyValues *row )
{
	row->SetString( "MapID", STRING( gpGlobals->mapname ) );
	row->SetInt( "TimeSubmitted", static_cast< int >( GetSteamWorksSGameStatsUploader().GetTimeSinceEpoch() ) );
	EResult result = GetSteamWorksSGameStatsUploader().AddStatsForUpload( row );
	// The SDK implementation returns k_EResultFail without consuming its input.
	// Do not turn that real service failure into a successful local backend.
	if ( result != k_EResultOK ) row->deleteThis();
}

void CL4DGameStats::SubmitSurvivorStats( bool allTeams )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsConnected() || player->IsBot() || ( !allTeams && player->GetTeamNumber() != TEAM_SURVIVOR ) ) continue;
		L4DSurvivorStats_t *stats = GetSurvivorStats( player );
		SnapshotSurvivorStats( player, *stats );
		KeyValues *survivor = new KeyValues( "L4D2SurvivorData" );
		survivor->SetInt( "IsBot", 0 );
		int character = player->GetSurvivorCharacter();
		if ( TerrorGameRules()->IsPlayerControlledZombies() ) character += 4 * player->GetTeamNumber() - 8;
		survivor->SetInt( "CharacterID", character );
		survivor->SetInt( "Difficulty", TerrorGameRules()->GetDifficultyLevel() );
		const CSteamID *id = engine->GetClientSteamID( player->edict() );
		survivor->SetUint64( "AccountIDPlayer", id ? id->ConvertToUint64() : 0 );
		for ( int stat = 0; stat < L4DSTAT_COUNT; ++stat ) survivor->SetInt( s_survivorNames[stat], stats->values[stat] );
		SubmitRow( survivor );
		stats->Clear();
	}
}

void CL4DGameStats::SubmitCompetitiveStats()
{
	KeyValues *row = new KeyValues( "L4D2CompetitiveStats" );
	row->SetInt( "GameMode", m_gameMode );
	row->SetInt( "Team1Wins", m_teamWins[0] );
	row->SetInt( "Team2Wins", m_teamWins[1] );
	row->SetInt( "Team1Score", m_teamScore[0] );
	row->SetInt( "Team2Score", m_teamScore[1] );
	row->SetInt( "TeamsTie", m_ties );
	row->SetInt( "XboxDedicated", 0 ); // Pinned PC engine slot126 is proven false.
	SubmitRow( row );
	m_teamScore[0] = m_teamScore[1] = -1;
}

// Existing native S2A31B0 owner: src/game/server/l4d2/l4d_gamestats.cpp.
// Call exactly once at native S2A3AE0 entry, before creating L4D2ZombieData.
void CL4DGameStats::AccumulateSurvivorDamageByClass()
{
	if ( !TerrorGameRules() )
		return;
	m_zombieData[17] = static_cast< int >( static_cast< unsigned int >( m_zombieData[17] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 1, false ) ) );
	m_zombieData[18] = static_cast< int >( static_cast< unsigned int >( m_zombieData[18] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 2, false ) ) );
	m_zombieData[19] = static_cast< int >( static_cast< unsigned int >( m_zombieData[19] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 3, false ) ) );
	m_zombieData[21] = static_cast< int >( static_cast< unsigned int >( m_zombieData[21] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 5, false ) ) );
	m_zombieData[22] = static_cast< int >( static_cast< unsigned int >( m_zombieData[22] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 4, false ) ) );
	m_zombieData[23] = static_cast< int >( static_cast< unsigned int >( m_zombieData[23] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 6, false ) ) );
	m_zombieData[20] = static_cast< int >( static_cast< unsigned int >( m_zombieData[20] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 8, false ) ) );
	m_zombieData[24] = static_cast< int >( static_cast< unsigned int >( m_zombieData[24] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 1, true ) ) );
	m_zombieData[25] = static_cast< int >( static_cast< unsigned int >( m_zombieData[25] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 2, true ) ) );
	m_zombieData[26] = static_cast< int >( static_cast< unsigned int >( m_zombieData[26] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 3, true ) ) );
	m_zombieData[28] = static_cast< int >( static_cast< unsigned int >( m_zombieData[28] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 5, true ) ) );
	m_zombieData[29] = static_cast< int >( static_cast< unsigned int >( m_zombieData[29] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 4, true ) ) );
	m_zombieData[30] = static_cast< int >( static_cast< unsigned int >( m_zombieData[30] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 6, true ) ) );
	m_zombieData[27] = static_cast< int >( static_cast< unsigned int >( m_zombieData[27] ) + static_cast< unsigned int >( TerrorGameRules()->GetSurvivorDamageByClass( 8, true ) ) );
	TerrorGameRules()->ResetSurvivorDamageByClass();
}

bool CL4DGameStats::AddDataForSend( KeyValues *data, StatSendType_t sendType )
{
	if ( sendType != STATSEND_LEVELSHUTDOWN ) return false;
	// Native2A5DC0's chapter/session phase. The target's already approved
	// mounted mission dictionary substitutes for match-session chapter metadata.
	if ( !m_collectionEnded && ( TerrorGameRules()->IsVersusMode() || TerrorGameRules()->IsCoopMode() || TerrorGameRules()->IsRealismMode() ) && TerrorGameRules()->IsFinalChapter() )
		m_collectionEnded = true;
	// The native no-active-match-session query and uploader end-reason2/6
	// setters have no target session/backend. Omit only those unavailable calls.
	int humans = 0, alive = 0;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR || ( player->IsAlive() && player->IsGhost() ) ) continue;
		if ( !player->IsBot() ) ++humans;
		if ( player->IsAlive() ) ++alive;
	}
	if ( ( m_totalTime >= 90 && m_zombieDeaths >= 30 && humans >= 1 ) || l4d_stats_nogameplaycheck.GetBool() || engine->IsDedicatedServer() ) m_gameplayEligible = true;
	if ( !m_totalTime ) m_gameplayEligible = false;
	if ( !( m_gameplayEligible && l4d_stats_track.GetBool() ) && !m_collectionEnded ) { Clear(); return false; }
	if ( data ) { ConVarRef cheats( "sv_cheats" ); data->SetInt( "Cheats", cheats.GetInt() ); }
	m_mapData[1] = m_zombieData[7];
	m_mapData[8] = TheDirector ? TheDirector->GetSurvivorRescueCount() : 0; // Native5050 reads the REAL vector992 count1004.
	m_mapData[10] = IsInCommentaryMode();
	m_mapData[14] = alive;
	m_mapData[15] = m_zombieData[8];
	KeyValues *map = new KeyValues( "L4D2MapData" );
	map->SetInt( "TotalTime", m_totalTime );
	map->SetInt( "Difficulty", TerrorGameRules()->GetDifficultyLevel() );
	for ( int i = 0; i < 18; ++i ) map->SetInt( s_mapNames[i], m_mapData[i] );
	SubmitRow( map );
	static const char *const zombieNames[31] =
	{
		"SpawnNormal", "SpawnSmoker", "SpawnBoomer", "SpawnHunter", "SpawnSpitter", "SpawnJockey", "SpawnCharger", "SpawnWitch", "SpawnTank",
		"PounceIncaps", "ChokeIncaps", "WitchIncaps", "BoomerIncaps", "TankIncaps", "JockeyIncaps", "ChargerIncaps", "SpitterIncaps",
		"SmokerDamage", "BoomerDamage", "HunterDamage", "TankDamage", "JockeyDamage", "SpitterDamage", "ChargerDamage",
		"SmokerAssistDamage", "BoomerAssistDamage", "HunterAssistDamage", "TankAssistDamage", "JockeyAssistDamage", "SpitterAssistDamage", "ChargerAssistDamage"
	};
	AccumulateSurvivorDamageByClass();
	KeyValues *zombies = new KeyValues( "L4D2ZombieData" );
	for ( int i = 0; i < 31; ++i ) zombies->SetInt( zombieNames[i], m_zombieData[i] );
	SubmitRow( zombies );
	if ( !TerrorGameRules()->IsPlayerControlledZombies() ) SubmitSurvivorStats( false );
	for ( int i = 0; i < 39; ++i )
	{
		KeyValues *weapon = new KeyValues( "WeaponData" );
		weapon->SetInt( "weaponid", i );
		weapon->SetInt( "ShotsFired", m_weaponStats[i].shotsFired );
		weapon->SetInt( "Hit", m_weaponStats[i].hits );
		weapon->SetInt( "Damage", m_weaponStats[i].damage );
		weapon->SetInt( "Headshots", m_weaponStats[i].headshots );
		weapon->SetInt( "Kills", m_weaponStats[i].kills );
		SubmitRow( weapon );
	}
	FOR_EACH_VEC( m_achievements, i )
	{
		KeyValues *achievement = new KeyValues( "AchievementData" );
		achievement->SetInt( "AchievementID", m_achievements[i] );
		SubmitRow( achievement );
	}
	FOR_EACH_VEC( m_durationAttacks, i )
	{
		KeyValues *duration = new KeyValues( "L4D2DurationAttacks" );
		duration->SetInt( "TypeID", m_durationAttacks[i].type );
		duration->SetFloat( "Duration", m_durationAttacks[i].duration );
		duration->SetInt( "WasSaved", m_durationAttacks[i].saved );
		SubmitRow( duration );
	}
	FOR_EACH_VEC( m_deaths, i )
	{
		KeyValues *death = new KeyValues( "Deaths" );
		death->SetInt( "XCoord", static_cast< int >( m_deaths[i].position.x ) );
		death->SetInt( "YCoord", static_cast< int >( m_deaths[i].position.y ) );
		death->SetInt( "ZCoord", static_cast< int >( m_deaths[i].position.z ) );
		death->SetInt( "Team", m_deaths[i].victim );
		death->SetInt( "SessionTime", m_deaths[i].time );
		death->SetInt( "DeathCause", m_deaths[i].killer );
		death->SetInt( "DeathParam", m_deaths[i].damageType );
		death->SetString( "DeathMap", STRING( gpGlobals->mapname ) );
		EResult result = GetSteamWorksSGameStatsUploader().AddStatsForUpload( death );
		if ( result != k_EResultOK ) death->deleteThis();
	}
	SubmitStuckZombies(); // S2A5F7C: AFTER native2A4700 Deaths, BEFORE versus/2A3440.
	if ( TerrorGameRules()->IsVersusMode() ) SubmitCompetitiveStats();
	if ( m_collectionEnded ) GetSteamWorksSGameStatsUploader().EndSession();
	Clear();
	return true;
}

// S2A5710..2A5786. The caller already latched its PRIMARY actor flag; this
// genuine recorder independently rejects NULL/noninfected, but not dead actors.
void CL4DGameStats::RecordStuckZombie( CTerrorPlayer *player )
{
	if ( !player || player->GetTeamNumber() != TEAM_INFECTED )
		return;
	const Vector &origin = player->GetAbsOrigin();
	const int zombieClass = player->GetZombieClass();
	StuckZombie_t record;
	record.position = origin;
	record.zombieClass = zombieClass;
	m_stuckZombiePositions.AddToTail( record );
}

// S2A4840..2A4945. Real native1200 vector reader; retain entries until Clear.
// CVTTSS2SI preserves the pinned NaN/out-of-range INT_MIN result, not C++ UB.
void CL4DGameStats::SubmitStuckZombies()
{
	FOR_EACH_VEC( m_stuckZombiePositions, i )
	{
		KeyValues *row = new KeyValues( "L4D2Stuck" );
		row->SetInt( "XCoord", _mm_cvttss_si32( _mm_set_ss( m_stuckZombiePositions[i].position.x ) ) );
		row->SetInt( "YCoord", _mm_cvttss_si32( _mm_set_ss( m_stuckZombiePositions[i].position.y ) ) );
		row->SetInt( "ZCoord", _mm_cvttss_si32( _mm_set_ss( m_stuckZombiePositions[i].position.z ) ) );
		row->SetInt( "Type", m_stuckZombiePositions[i].zombieClass );
		// Existing SubmitRow adds MapID, then TimeSubmitted, then transfers the
		// real row to the existing uploader. No added eligibility or send gate.
		SubmitRow( row );
	}
}

// Native reached panic-completion statistics, moved intact from Director TU.
void CL4DGameStats::Event_PanicEventFinished()
{
	CUtlVector< CTerrorPlayer * > survivors;
	CollectTerrorPlayers( survivors, TEAM_SURVIVOR, 2, 3, 9 );
	if ( survivors.Count() == 1 ) m_mapData[5] = (int)( (unsigned int)m_mapData[5] + 1u );
}
void CL4DGameStats::OnVotePassed( const char *message )
{
	// S2A5940. Main-approved target capability gap: the SDK uploader has no
	// session row/backend, so omit ONLY SetSessionEndReason(4/3). No fake setter.
	if ( !message || ( Q_stricmp( message, "#L4D_vote_passed_return_to_lobby" ) && Q_stricmp( message, "#L4D_vote_passed_mission_change" ) ) ) return;
	m_collectionEnded = true;
	const int now = _mm_cvtt_ss2si( _mm_set_ss( gpGlobals->curtime ) );
	m_totalTime = (int)( (unsigned int)m_totalTime + (unsigned int)now - (unsigned int)m_levelStartTime );
	m_levelStartTime = now;
	AddDataForSend( NULL, STATSEND_LEVELSHUTDOWN );
}
