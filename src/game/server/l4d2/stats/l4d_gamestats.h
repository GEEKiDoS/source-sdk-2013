//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D_GAMESTATS_H
#define L4D_GAMESTATS_H
#ifdef _WIN32
#pragma once
#endif

#include "gamestats.h"
#include "GameEventListener.h"
#include "tier1/utllinkedlist.h"
#include "tier1/utlvector.h"
#include "util_shared.h"

class CTerrorPlayer;

// S2A33E0's forty values, in native order. The user ID and raw self-heal sum
// precede these in the native record and are not reported as named statistics.
enum L4DSurvivorStat_t
{
	L4DSTAT_MEDKITS_USED, L4DSTAT_PILLS_USED, L4DSTAT_DAMAGE_TAKEN, L4DSTAT_REVIVE_OTHER,
	L4DSTAT_INCAPS, L4DSTAT_DEATHS, L4DSTAT_INCAP_KILLS, L4DSTAT_FF_DAMAGE, L4DSTAT_FF_HITS,
	L4DSTAT_FF_INCAPS, L4DSTAT_SELF_HEAL_COUNT, L4DSTAT_FRIEND_HEAL_COUNT, L4DSTAT_AVERAGE_HEAL,
	L4DSTAT_MAX_DEAD_TIME, L4DSTAT_TOTAL_DEAD_TIME, L4DSTAT_PROTECTED, L4DSTAT_PILLS_SHARED,
	L4DSTAT_THIRD_STRIKE, L4DSTAT_ITEMS_SCAVENGED, L4DSTAT_TOTAL_ITEMS, L4DSTAT_FF_HITS_TAKEN,
	L4DSTAT_ZOMBIE_KILLS, L4DSTAT_FF_INCAPS_DEALT, L4DSTAT_ADRENALINE_USED, L4DSTAT_ADRENALINE_SHARED,
	L4DSTAT_DEFIBRILLATOR_USED, L4DSTAT_DEFIBRILLATED, L4DSTAT_PERCENT_TRAVELED,
	L4DSTAT_COMMON_KILLS, L4DSTAT_SMOKER_KILLS, L4DSTAT_BOOMER_KILLS, L4DSTAT_HUNTER_KILLS,
	L4DSTAT_SPITTER_KILLS, L4DSTAT_JOCKEY_KILLS, L4DSTAT_CHARGER_KILLS, L4DSTAT_WITCH_KILLS,
	L4DSTAT_TANK_KILLS, L4DSTAT_INCENDIARY_FIRED, L4DSTAT_EXPLOSIVE_FIRED, L4DSTAT_MELEE_KILLS,
	L4DSTAT_COUNT
};
struct L4DSurvivorStats_t
{
	int userID;
	int selfHealSum;
	int values[L4DSTAT_COUNT];
	void Clear();
};
struct L4DWeaponStats_t
{
	int shotsFired, hits, damage, headshots, kills;
};

// Native RTTI CL4DGameStats (S7AA984), singleton S840CF8. Local collection is
// unconditional; Steam/schema/service availability is not an eligibility gate.
class CL4DGameStats : public CBaseGameStats, public CGameEventListener, public CAutoGameSystem
{
public:
	CL4DGameStats();
	virtual bool Init() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual bool UseOldFormat() OVERRIDE { return false; }
	virtual void Clear() OVERRIDE;
	virtual void Event_LevelInit() OVERRIDE;
	virtual void Event_LevelShutdown( float elapsed ) OVERRIDE;
	virtual void Event_PlayerDisconnected( CBasePlayer *player ) OVERRIDE;
	virtual bool AddDataForSend( KeyValues *data, StatSendType_t sendType ) OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	void Event_FirstAidKitSpawned();
	void Event_DefibrillatorSpawned();
	void SetInitialHealthItemCount( int count );
	void Event_ZombieSpawned( int zombieClass, int count );
	void Event_MobSpawned();
	void Event_DirectorRelax();
	void Event_CarAlarmActive(); // Native2A3540: same map counter7/972.
	void Event_PanicEventFinished(); // Sole body moved from director_scenario.cpp.
	void OnVotePassed( const char *message ); // Native S2A5940, sole l4d_gamestats.cpp body.
	void Event_VomitJarHit( CTerrorPlayer *player );
	void Event_Kill( CBaseEntity *victim, const CTakeDamageInfo &info );
	void SendWeaponStats( CTerrorPlayer *player );
	void ReportBotsWontHelp( CTerrorPlayer *helper, CTerrorPlayer *target );
	void RecordStuckZombie( CTerrorPlayer *player ); // S2A5710, real native1200 vector, not Event_ZombieSpawned.
	L4DSurvivorStats_t *GetSurvivorStats( CTerrorPlayer *player );
	const L4DWeaponStats_t &GetWeaponStats( int weapon ) const { return m_weaponStats[weapon]; }
private:
	void RecordWeaponStat( CTerrorPlayer *player, int weapon, int stat, int value );
	int GetWeaponStatID( CTerrorPlayer *player, const char *weapon ) const;
	void SnapshotSurvivorStats( CTerrorPlayer *player, L4DSurvivorStats_t &stats );
	void ResetSurvivorStats( CTerrorPlayer *player );
	void RecordHurt( IGameEvent *event );
	void RecordIncapacitation( IGameEvent *event );
	void RecordRoundEnd( IGameEvent *event );
	void SubmitSurvivorStats( bool allTeams );
	void SubmitCompetitiveStats();
	void SubmitRow( KeyValues *row );
	void SubmitStuckZombies(); // S2A4840, called by AddDataForSend at S2A5F7C.
	void AccumulateSurvivorDamageByClass(); // S2A31B0, fourteen value stores then real Rules reset.
	struct StuckZombie_t { Vector position; int zombieClass; };
	CUtlVector< StuckZombie_t > m_stuckZombiePositions;
	struct DurationAttack_t { int type; float duration; bool saved; };
	CUtlVector< DurationAttack_t > m_durationAttacks;
	struct Death_t { Vector position; int victim; int killer; int damageType; int time; };
	CUtlVector< Death_t > m_deaths;
	CUtlLinkedList< L4DSurvivorStats_t > m_survivors;
	L4DWeaponStats_t m_weaponStats[39];
	L4DWeaponStats_t m_playerWeaponStats[32][39];
	int m_initialHealthItems;
	int m_zombieDeaths;
	int m_mapData[18];
	int m_zombieData[31];
	CUtlVector< int > m_achievements;
	int m_levelStartTime;
	int m_totalTime;
	int m_teamScore[2];
	int m_teamWins[2];
	int m_ties;
	int m_gameMode;
	bool m_collectionEnded;
	bool m_gameplayEligible; // Native940: sticky gameplay admission until Clear, not upload success.
	IntervalTimer m_botsHelpTimer;
};
CL4DGameStats &L4DGameStats();

#endif // L4D_GAMESTATS_H
