//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_L4D_GAMESTATS_H
#define C_L4D_GAMESTATS_H
#ifdef _WIN32
#pragma once
#endif
#include "gamestats.h"
#include "GameEventListener.h"
#include "steam/steam_api.h"

class C_BasePlayer;
class C_CL4DGameStats : public CBaseGameStats, public CGameEventListener, public CAutoGameSystem
{
public:
	C_CL4DGameStats();
	virtual bool Init() OVERRIDE;
	virtual bool UseOldFormat() OVERRIDE { return false; }
	virtual void Shutdown() OVERRIDE;
	virtual void Event_LevelInit() OVERRIDE;
	virtual void Event_LevelShutdown( float elapsed ) OVERRIDE;
	virtual bool AddDataForSend( KeyValues *data, StatSendType_t sendType ) OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	void ReceiveWeaponStats( bf_read &message );
	void RecordPZDamageStats( int type, C_BasePlayer *attacker, C_BasePlayer *victim );
	void RecordRematchVoteChoice( bool yes );
	void RecordRematchOutcome( bool passed );
	void ResetSteamStats();
	void PrintSteamStats();
private:
	struct VoteData_t { int id, yes, no, passed, failed, total, initiated; };
	struct SpecialStats_t
	{
		int mostDamage, averageLife, spawns, life, attacks, scavengeAttacks, scavengeDamage, interruptedPours;
	};
	void ResetSession();
	void ClearUserStats();
	void BeginSteamStatsDownload();
	void DownloadUserStats();
	void TransferUserStats( bool download, bool weaponsOnly, bool printing = false );
	void UploadWeaponStats();
	void UploadSteamUserStats();
	void StoreUserStats();
	void ComputeWeaponStatistics();
	void ComputeUserStatistics();
	void SubmitSessionData();
	void SubmitCompetitiveData();
	void SubmitRow( KeyValues *row, bool map );
	bool HaveSessionData() const;
	void RecordGamePlayed();
	void RecordCharacterPlayed( int character );
	void RecordSpecialLife( bool spawn );
	void RecordScavengeRound();
	int CurrentSpecialClass() const;
	int ScavengeMap() const;
	int Difficulty() const;
	void TransferInt( const char *key, int &value, bool download );
	void TransferFloat( const char *key, float &value, bool download );
	void FinishSteamStore( const char *filename );
	STEAM_CALLBACK( C_CL4DGameStats, OnUserStatsReceived, UserStatsReceived_t, m_statsReceived );
	STEAM_CALLBACK( C_CL4DGameStats, OnUserStatsStored, UserStatsStored_t, m_statsStored );
	SpecialStats_t m_special[7]; // Native nested40..263, Boomer/Hunter/Smoker/Tank/Spitter/Jockey/Charger.
	int m_favorite[8]; //264: two weapon IDs, two kill ratios, campaign, character, two play percentages.
	int m_recent[5]; //296: escaped, playtime, campaign, difficulty, mode. Native does NOT download this family.
	int m_versus[6]; //316: games, finales, maximum points, wins, losses, maximum-points class.
	float m_average[13]; //340. Entry0 is the native Steam average-rate statistic.
	int m_difficulty[3]; //392: difficulty at best revived/protected/FF count.
	int m_weaponStats[7][39]; //404; columns Shots/Head/Kills/Accuracy/HeadAccuracy/Hit/Damage.
	int m_total[22]; //1496, exact native name order in c_l4d_stat_names.inc.
	int m_games[20]; //1584: games, finales, fourteen campaign times, total time, Survival/Scavenge/Realism games.
	int m_teamRevived, m_teamProtected; //Native1668/1664 respectively; retained until ClearUserStats.
	int m_lifeDamage[7]; //1672..1687,1700..1711: same semantic seven classes, not physical padding.
	int m_previousScore; //1688.
	int m_friendlyDamage; //1692.
	int m_currentClass; //1696, constructor/reset -1.
	int m_poursInterrupted[7]; //1712, native readers but no increment in46B20.
	int m_matchScore; //1740.
	int m_scavenge[21][14]; //1744..2919; final row is unknown.
	int m_rematchStats[8]; //2920..2948, SAME store as PZ panel's direct writers.
	bool m_statsRequested, m_statsReady; //2952/2953.
	KeyValues *m_pSteamStatsDebug; //2956, shared general/weapon debug root.
	float m_lastWeaponMessageTime;
	VoteData_t m_voteData[8];
	int m_currentVoteType;
	int m_session[9]; //Outer608: FF hits taken, friends, difficulty, achievements, lobby leader, kicked, finale, seconds, mode.
	int m_survivorVersus[11]; //1060, native reset at42300.
	int m_zombieVersus[15]; //1104, same actual round reset.
	bool m_sessionEnded, m_playedSurvivor, m_playedInfected;
	char m_sessionStartMap[260], m_sessionEndMap[260];
	char m_textLanguage[64], m_audioLanguage[64], m_country[16];
	int m_config[8];
};
void TerrorRecordPZDamageStats( int type, C_BasePlayer *attacker, C_BasePlayer *victim );
void TerrorRecordRematchVoteChoice( bool yes );
void TerrorRecordRematchOutcome( bool passed );
#endif
