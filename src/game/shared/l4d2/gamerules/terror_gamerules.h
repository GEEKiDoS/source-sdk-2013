//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Terror rules, spawn placement and network proxy.
//
//=============================================================================//

#ifndef TERROR_GAMERULES_H
#define TERROR_GAMERULES_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/gamerules/cs_gamerules.h"
#include "l4d2/terror_shareddefs.h"
class KeyValues;
#ifndef CLIENT_DLL
#include "steam/steam_api_common.h"
#include "steam/isteamgameserver.h"
#include "vscript/ivscript.h"
class CTerrorPlayer;
class TerrorNavArea;
class IGameEvent;
class CWeaponSpawnConfigurable;
class CPropHealthCabinet;
struct Extent;
#endif

#ifdef CLIENT_DLL
#define CTerrorGameRules C_TerrorGameRules
#define CTerrorGameRulesProxy C_TerrorGameRulesProxy
#endif

class CTerrorGameRulesProxy : public CCSGameRulesProxy
{
public:
	DECLARE_CLASS( CTerrorGameRulesProxy, CCSGameRulesProxy );
	DECLARE_NETWORKCLASS();
};

class CTerrorGameRules : public CCSGameRules
{
public:
	DECLARE_CLASS( CTerrorGameRules, CCSGameRules );
#ifdef CLIENT_DLL
	DECLARE_CLIENTCLASS_NOBASE();
#else
	DECLARE_SERVERCLASS_NOBASE();
#endif

	CTerrorGameRules();
	virtual const CViewVectors *GetViewVectors() const OVERRIDE;
	virtual int Damage_GetTimeBased() OVERRIDE;
	virtual int Damage_GetShowOnHud() OVERRIDE;
	virtual int Damage_GetNoPhysicsForce() OVERRIDE;
	virtual bool Damage_IsTimeBased( int damageType ) OVERRIDE;
	virtual bool Damage_ShowOnHUD( int damageType ) OVERRIDE;
	virtual bool Damage_NoPhysicsForce( int damageType ) OVERRIDE;
	bool IsInIntro() const { return m_bInIntro; }
	bool IsDedicatedServer() const { return m_bIsDedicatedServer; }
	const char *GetActiveGameMode() const;
	bool IsCoopMode() const;
	bool IsRealismMode() const;
	bool IsVersusMode() const;
#ifndef CLIENT_DLL
	bool IsTankRun() const;
#endif
	bool IsSurvivalMode() const;
	bool IsScavengeMode() const;
	bool IsPlayerControlledZombies() const;
	bool IsWaterSlowdownEnabled() const { return m_bWaterSlowdownEnabled; }
	bool IsRoundInProgress() const;
	double GetRoundElapsedTime() const;
	double GetRoundTimeRemaining() const;
	int GetScavengeItemsGoal() const;
	int GetScavengeItemsRemaining() const;
	int GetScavengeRoundNumber() const;
	bool IsScavengeSecondHalf() const;
	bool IsInSecondHalfOfRound() const { return m_bInSecondHalfOfRound; }
	int GetScavengeRoundLimit() const;
	int GetVersusMaxCompletionScore() const;
	bool IsScriptedModeActive() const { return m_bChallengeModeActive; }
	float GetHoldoutCooldownEndTime() const { return m_flHoldoutCooldownEndTime; }
	int GetScriptedHUDInt(int slot) const { return m_iScriptedHUDInts[slot]; }
	float GetScriptedHUDFloat(int slot) const { return m_fScriptedHUDFloats[slot]; }
	int GetScriptedHUDFlags(int slot) const { return m_iScriptedHUDFlags[slot]; }
	float GetScriptedHUDPosX(int slot) const { return m_fScriptedHUDPosX[slot]; }
	float GetScriptedHUDPosY(int slot) const { return m_fScriptedHUDPosY[slot]; }
	float GetScriptedHUDWidth(int slot) const { return m_fScriptedHUDWidth[slot]; }
	float GetScriptedHUDHeight(int slot) const { return m_fScriptedHUDHeight[slot]; }
	int GetScriptedHUDTimerMode(int timer) const { return m_iScriptedHUDTimerMode[timer]; }
	float GetScriptedHUDTimerBase(int timer) const { return m_fScriptedHUDTimerBase[timer]; }
	float GetScriptedHUDTimerAdd(int timer) const { return m_fScriptedHUDTimerAdd[timer]; }
	double ReadScriptedHUDTimer( int timer ) const;
	const char *GetScriptedHUDString(int slot) const { return m_szScriptedHUDStringSet[slot]; }
	bool IsSacrificeEscapee( int playerIndex ) const;
	bool HasSacrificeEscapees() const { return m_iSacrificeEscapees != 0; }
	bool IsVersusVoteRestarting() const { return m_bIsVersusVoteRestarting; }

	int GetVersusScore( int team, bool campaign ) const;
	int GetScavengeTeamIndex( int team ) const;
	int GetScavengeTeamScore( int team, int round = -1 ) const;
	double GetTeamRoundDuration( int team ) const;
	double GetRoundStartTime() const;
	double GetTeamBestRoundTime() const;
	int GetScavengeScore( bool firstTeam, int round ) const;
	int GetScavengeMatchScore( int team ) const;
	int GetScavengeWinningTeam( int round ) const;
	int GetSurvivalMedalForTime( float time ) const;
	int GetVersusChapterScore( int team ) const;
	int GetVersusDistancePerSurvivor( int character, int team ) const;
	int GetVersusSurvivalMultiplier( int team ) const;
	int GetVersusDefibsUsed( int team ) const;
	int GetVersusSurvivorDeathDistance( int character, int team ) const;
	int GetVersusCompletionScore( int team ) const;
	int GetVersusChapterDamage( int team ) const;
	int GetNativeServerRank() const { return m_iServerRank; }
	int GetNativeServerPlayerCount() const { return m_iServerPlayerCount; }
	int GetNativeServerSteamGroupID() const { return m_iServerSteamGroupID; }
	bool IsNativeDedicatedServer() const { return m_bIsDedicatedServer; }
#ifdef CLIENT_DLL
	double GetRoundEndTime() const;
#endif
	virtual void LevelInitPreEntity() OVERRIDE;
	int GetSurvivorSet() const;
	void ResetSurvivorSetCache();
	KeyValues *GetChapterInfo() const;
#ifndef CLIENT_DLL
	virtual void LevelInitPostEntity() OVERRIDE;
	virtual const char *GetGameDescription() OVERRIDE;
	virtual void CreateStandardEntities() OVERRIDE;
	virtual float WeaponTraceEntity( CBaseEntity *entity, const Vector &start, const Vector &end, unsigned int mask, trace_t *trace ) OVERRIDE;
	virtual bool ClientConnected( edict_t *pEntity, const char *pszName, const char *pszAddress, char *pRejectReason, int nMaxRejectReason ) OVERRIDE;
	virtual void ClientDisconnected( edict_t *pClient ) OVERRIDE;
	virtual void ClientSettingsChanged( CBasePlayer *player ) OVERRIDE;
	virtual void Think() OVERRIDE;
	virtual CBaseEntity *GetPlayerSpawnSpot( CBasePlayer *pPlayer ) OVERRIDE;
	virtual void PlayerSpawn( CBasePlayer *pPlayer ) OVERRIDE;
	virtual bool FShouldSwitchWeapon( CBasePlayer *player, CBaseCombatWeapon *weapon ) OVERRIDE;
	virtual bool FPlayerCanRespawn( CBasePlayer *base ) OVERRIDE;
	virtual void PrepareForChangeLevel( const char *targetMap );
	bool AllowSurvivorRescue() const;
	bool CanPickupObject( CTerrorPlayer *player, CBaseEntity *object ) const;
	bool ShouldApplyVomitJarBile() const;
	bool IsHeadshotOnly() const;
	void SetScriptedEventCooldown( float time );
	virtual bool FPlayerCanTakeDamage( CBasePlayer *victim, CBaseEntity *attacker, const CTakeDamageInfo &info ) OVERRIDE;
	virtual void RadiusDamage( const CTakeDamageInfo &info, const Vector &source, float radius, int classIgnore, CBaseEntity *entityIgnore ) OVERRIDE;
	virtual float FlPlayerFallDamage( CBasePlayer *player ) OVERRIDE;
	void PopulateDeathEvent( KeyValues *event, CBaseEntity *attacker, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	virtual void DeathNotice( CBaseCombatCharacter *victim, const CTakeDamageInfo &info ) OVERRIDE;
	void PopulateDeathNotice( IGameEvent *event, CBaseEntity *scorer, CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	void DeathNoticeForEntity( CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	void DeathNoticeForInfected( CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	virtual void PlayerKilled( CBasePlayer *victim, const CTakeDamageInfo &info ) OVERRIDE;
	virtual bool CheckWinConditions();

	virtual int GetHumanPlayerLimit() const;
	bool GetPlayerSpawnPosition( int nCharacter, Vector *pOrigin, QAngle *pAngles, TerrorNavArea **pArea );
	void SetInIntro( bool bInIntro );
	int GetDifficultyLevel() const;
	void RegisterWeaponSpawn( CWeaponSpawnConfigurable *spawn );
	void RemoveItemFromSpawnManager( CBaseEntity *item );
	void OnHealthCabinetOpened( CPropHealthCabinet *cabinet );
	bool IsHealthCabinetPopulationSuppressed() const;
	bool GetHealthCabinetCheckpoint( const Vector &origin, Extent *bounds, bool *checkpointFlag ) const;
	bool ShouldOpenHealthCabinetsInCheckpoint() const;
	bool CanSpawnCSWeapon( int weaponID, bool ignoreChance ) const;
	bool CanSpawnMeleeWeapon( const char *name ) const;
	bool AllowWeaponSpawn( int weaponID ) const;
	bool GetWeaponSpawnReplacement( int weaponID, char *destination, int size ) const;
	float GetCommonAttackDamage( CBasePlayer *victim ) const;
	float GetAmountOfPointVisible( const Vector &source, const Vector &point, CBaseEntity *target );
	double GetAmountOfEntityVisible( const Vector &source, CBaseEntity *entity );
	KeyValues *GetMissionInfo() const;
	int GetMissionChapterCount() const;
	bool IsL4D1Campaign() const; // Native2DA0B0, shared mission metadata/cache owner.
	bool IsInfiniteChainsawFuel() const;
	bool IsVIPGnomeMutation() const;
	bool IsHealingGnomeMutation() const;
	virtual void RecordSurvivorDamage( CTerrorPlayer *attacker, int damage, bool assist );
	virtual int GetSurvivorDamageByClass( int zombieClass, bool assist ) const;
	virtual void ResetSurvivorDamageByClass();
	virtual ~CTerrorGameRules() OVERRIDE; // Native2E0EB0, real saved-convar revert before derived field destruction.
	void UpdatePlayerPhysicsCrush(); // Native2DEA20 ->2DD270.
	void UpdatePlayerPushPhysics(); // Native2E1032..1757, SAME m_pushEntity owner.
	void AddRecentlySpawnedPlayer( CBaseEntity *entity ); // Native2E0D20, SAME existing892 queue.
	void OnInfectedBotPathFailed( float pressure, CTerrorPlayer *player ); // Existing M5 sole2DC5D0.
	void UpdateInfectedBotPathFailures(); // Existing M5 sole2E18F5 block.
	bool GiveDefaultItemsFromScript( CTerrorPlayer *player ); // Native27A000 adapter to actual enabled scripted owner.
	void SetRoundStartTime( float time );
	void SetRoundEndTime( float time );
	void ResetRoundAccumulatedTime();
	void AddRoundAccumulatedTime( float time );
	void IncrementScavengeTeamScore( int team );
	void SetScavengeItemsRemaining( int count );
	void SetScavengeItemsGoal( int count );
	void SetScavengeTeamRoundTime( int teamIndex, float time );
	void SetTeamRoundDuration( int team, float duration );
	void SetScavengeWinningTeam( int teamIndex, int round );
	void IncrementScavengeMatchScore( int teamIndex );
	int GetDamageScoreTeamIndex() const;
	void UpdateDirectorRoundState();
	void AdvanceScavengeRoundNumber();
	void ResetScavengeRoundNumber();
	KeyValues *GetFirstChapterInfo( KeyValues **outMission = NULL ) const;
	KeyValues *GetNextChapterInfo( KeyValues **outMission = NULL ) const;
	bool IsFinalChapter() const;
	void SetVersusCampaignScores( int first, int second );
	void AccumulateVersusChapterScores();
	void ResetRoundDurations();
	void ResetScavengeScores( bool match );
	void ResetVersusRoundScores( bool campaign );
	int CalculateVersusPlayerDistanceScore( CTerrorPlayer *player ) const;
	int UpdateVersusDistanceCache( bool markDeath );
	void CacheVersusScores( bool markDeath );
	void IncrementVersusDefibsUsed(); // native2DC270, called only by the real defibrillator restoration
	void BreakVersusTie();
	void RecordFinaleEscapees( const CUtlVector< CTerrorPlayer * > &players );
	void UpdateVersusSurvivalMultiplier( bool completed );
	void SetScriptedModeActive(bool active);
	void ResetScriptedHUD();
	void SetScriptedHUDString(int slot,const char *text);
	void SetScriptedHUDLayout(int slot,float x,float y,float width,float height);
	void SetScriptedHUDTimerMode(int timer,int mode);
	void ManageScriptedHUDTimer(int timer,int command,float value);
	int UpdateScriptedHUDField(HSCRIPT table);
	void SetScriptedHUDFlags(int slot,int flags) { m_iScriptedHUDFlags.Set(slot,flags); }
	bool IsBleedoutSuppressed() const;
	int GetSurvivorMaxIncapacitatedCount() const;
	bool IsTempHealthOnly() const;
	void GetRestoringPlayerCounts( int team, int *players, int *restoring ) const;
	virtual void TerminateRound( float delay, int reason );
	virtual void RestartRound();
	virtual void CleanUpMap();
	bool IsHolidayGiftSeason() const { return m_bHolidayGiftSeason; }
	bool IsSacrificeEscapee( CTerrorPlayer *player ) const;
	void UpdateSacrificeEscapees();

private:
	void OnGSGameplayStats( GSGameplayStats_t *stats ); // S2DF790, nonempty native callback.
	CCallback< CTerrorGameRules, GSGameplayStats_t, true > m_GSGameplayStatsCallback; // Native768, before recent-spawn vector892.
	int m_survivorDirectDamageByClass[9]; // Native820..852, plain genuine direct damage counters.
	int m_survivorAssistDamageByClass[9]; // Native856..888, distinct plain assist counters.
	struct SpawnedPlayer_t
	{
		EHANDLE m_hPlayer;
		float m_flSpawnTime;
	};
	CUtlVector< SpawnedPlayer_t > m_SpawnedPlayers;
	float m_flNextVersusScoreUpdate; // Native3576, allocation-zero; actual Think producer.
	EHANDLE m_hSurvivalExploitEntity; // Native792, genuine crush/exploit state owner.
	float m_flSurvivalExploitAccumulator;
	float m_flPreviousSurvivalExploitAccumulator;
	float m_flSurvivalExploitDecayTime;
	float m_flSurvivalExploitDamageTime;
	float m_flSurvivalExploitSpitTime;
	int m_nSurvivalExploitSpitCount;
	bool m_bHolidayGiftSeason; // Native3580, actual constructor calendar snapshot.
#endif

private:
	friend class CDirector;
	CNetworkArray( int, m_iSurvivorScore, 2 );
	CNetworkArray( int, m_iCampaignScore, 2 );
	CNetworkArray( int, m_iChapterScore, 2 );
	CNetworkArray( int, m_iChapterDamage, 2 );
	CNetworkArray( int, m_iScavengeTeamScore, 10 );
	CNetworkArray( int, m_iScavengeMatchScore, 2 );
	CNetworkArray( float, m_flTeamRoundTime, 2 );
	CNetworkArray( int, m_iVersusDistancePerSurvivor, 8 );
	CNetworkArray( int, m_iVersusSurvivorDeathDistance, 8 );
	CNetworkArray( int, m_iVersusSurvivalMultiplier, 2 );
	CNetworkArray( int, m_iVersusDefibsUsed, 2 );
	CNetworkVar( float, m_flRoundStartTime );
	CNetworkVar( float, m_flRoundEndTime );
	CNetworkVar( float, m_flAccumulatedTime );
	CNetworkVar( int, m_nRoundNumber );
	CNetworkVar( int, m_nRoundLimit );
	CNetworkArray( int, m_iWinningTeamNumber, 5 );
	CNetworkVar( float, m_flTeamBestRoundTime );
	CNetworkArray( float, m_flRoundDuration, 2 );
	CNetworkVar( int, m_nScavengeItemsRemaining );
	CNetworkVar( int, m_nScavengeItemsGoal );
	CNetworkVar( bool, m_bAreTeamsFlipped );
	CNetworkVar( bool, m_bInSecondHalfOfRound );
	CNetworkVar( bool, m_bIsTransitioningToNextMap );
	CNetworkVar( bool, m_bIsVersusVoteRestarting );
	int m_nVersusMaxCompletionScore; // Plain native S3584/C2752, NOT a wire.
#ifndef CLIENT_DLL
	int m_nCachedVersusDistanceScore; // Plain native1068.
	CNetworkVar( int, m_nNativeRoundScore1088 ); // Native setter dirties state; NO inferred sendprop.
	bool m_finaleEscapees[4]; // Same native1064..1067, NOT raw survivor-position13240.
#endif
	CNetworkVar(bool,m_bChallengeModeActive);
	CNetworkVar(float,m_flHoldoutCooldownEndTime);
	CNetworkArray(int,m_iScriptedHUDInts,15);
	CNetworkArray(float,m_fScriptedHUDFloats,15);
	CNetworkArray(int,m_iScriptedHUDFlags,15);
	CNetworkArray(float,m_fScriptedHUDPosX,15);
	CNetworkArray(float,m_fScriptedHUDPosY,15);
	CNetworkArray(float,m_fScriptedHUDWidth,15);
	CNetworkArray(float,m_fScriptedHUDHeight,15);
	CNetworkArray(int,m_iScriptedHUDTimerMode,4);
	CNetworkArray(float,m_fScriptedHUDTimerBase,4);
	CNetworkArray(float,m_fScriptedHUDTimerAdd,4);
	char m_szScriptedHUDStringSet[15][128];
	CNetworkVar( int, m_iServerSteamGroupID );
	CNetworkVar( int, m_iSacrificeEscapees );
	CNetworkVar( bool, m_bWaterSlowdownEnabled );
	CNetworkVar( bool, m_bInIntro );
	CNetworkVar( int, m_iServerRank );
	CNetworkVar( int, m_iServerPlayerCount );
	CNetworkVar( bool, m_bIsDedicatedServer );
};

inline CTerrorGameRules *TerrorGameRules()
{
	return static_cast<CTerrorGameRules *>( g_pGameRules );
}

extern ConVar z_difficulty;

#ifndef CLIENT_DLL
extern ConVar fall_speed_safe;
double CalculateTerrorFallDamage( float excessSpeed );
void TerrorRadiusDamage( const CTakeDamageInfo &info, float innerRadius, float outerRadius, int type ); // native3824E0
#endif

#endif // TERROR_GAMERULES_H
