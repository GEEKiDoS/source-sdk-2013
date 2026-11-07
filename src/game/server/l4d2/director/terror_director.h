//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_DIRECTOR_H
#define TERROR_DIRECTOR_H
#ifdef _WIN32
#pragma once
#endif
#include "GameEventListener.h"
#include "vscript/ivscript.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/director/director_scenario.h"
#include "tier1/utlvector.h"
#include "tier1/utlmap.h"
#include "l4d2/vscript/terror_scripted_context.h"
#include "l4d2/director/info_director.h"
#include "l4d2/mem_zero_on_new.h"
class CTerrorPlayer;
class CNavArea;
class TerrorNavArea;
CTerrorPlayer *FindHighestFlowSurvivor( int flowDirection, int flags = 0 ); // native28FD50/25B980, sole frame owner
float GetLowestSurvivorFlow( int flags = 0 ); // native25B890, sole frame stored-range projection
CTerrorPlayer *FindLowestFlowSurvivor( int flags = 0 ); // same25B890 body, no NAV requery
class CBaseCombatCharacter;
class CTakeDamageInfo;
void NotifyZombieDeath( CBaseCombatCharacter *dead, int zombieClass ); // native304190/2D1910, sole death-intensity fanout
// Native25DB70, shared only by the existing Director-family diagnostic owners.
void DirectorDebugText( float x, float y, int line, const char *text,
	int red, int green, int blue, int alpha, float duration );
bool IsSurvivorInCombat(); // Native25F9B0's complement; existing Scenario producer.
bool IsInCommentaryMode( void ); // Existing server CommentarySystem.cpp provider.
class CSurvivorRescue;
class DirectorMusicBanks;
class CDirectorScavengeMode;
class CDirectorSurvivalMode;


class CDirectorScriptedMode : public CMemZeroOnNew
{
public:
	CDirectorScriptedMode();
	void Reset();
	void Init();
	void Activate();
	void Shutdown( int reason, const char *nextMap );
	void OnGameplayStart();
	bool HasNextScriptedStage() const { return m_hGetNextScriptedStage != NULL; }
	ScriptStatus_t GetNextScriptedStage();
	void ReleaseScriptFunctions();
	void UpdateForFrame();
	void ShowScoreboard();
	bool HandleRoundEnd( struct TerrorRoundEndInfo *info ) const; // native275D40 genuine constantfalse
	void HideScoreboard(); // native2767C0, active BYTE0 / message_box
	bool BotQuery( CBaseEntity *entity, int query, bool fallback );
	int AllowBash( CBaseEntity *pusher, CBaseEntity *target ); // Native27A840, real cached292 callback.
	bool AllowTakeDamage( CBaseEntity *victim, CTakeDamageInfo &info ); // Native27A420, cached284/table288.
	bool InterceptChat( CBaseEntity *speaker, const char *text ); // Native27A920, cached304.
	bool CanSurvivorBotBreak( CBaseEntity *entity );
	bool AllowWeaponSpawn( int weaponID ) const;
	bool ShouldAvoidItem( int weaponID ) const; // Native27A1A0, enabled-byte1 / real Challenge query.
	bool ConvertWeaponSpawn( int weaponID, char *destination, int size ) const; // Native279E80.
	bool AllowFallenSurvivorItem( int weaponID ) const;
	void ClearCachedBotQueries() { ++m_nBotQueryEpoch; }
	void OnZombieDeath( CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	bool IsTankRun() const { return m_bEnabled && QueryOptionBool( "cm_TankRun", false ); }
	bool CanTriggerVIP( CBaseEntity *entity ) const;
	bool HasRequiredGnome() const;
	void OnFinaleVehicleLeaving( const CUtlVector< CTerrorPlayer * > &escaped );
	bool TryEndScriptedMode();
	bool GiveDefaultItems( CTerrorPlayer *player ); // Native27A000, real DirectorOptions GetDefaultItem iterator.
	int ConvertZombieClass( int zombieClass ) const; // Native279F40, enabled DirectorOptions callback with original numeric-bit conversion.
	CTerrorScriptedHUD *GetHUD() { return &m_HUD; }
	CTerrorScriptedJournal *GetJournal() { return &m_Journal; }
	const char *GetScoreboardFilename() const { return m_szScoreboardFilename; }
	void SetSpawnSetRuleFromOptions();
	int GetSpawnRule() const { return m_nSpawnSetRule; }
	void SpawnMutationGnome();
	CBaseEntity *FindRescueTrigger();
	void RecordScriptSpawnedEntity( CBaseEntity *entity ); // Native27AE30, includes NULL/duplicate handles.
	void UserConsoleCommand( CBaseEntity *entity, const ScriptVariant_t &argument );
	void RemoveScriptSpawnedEntities(); // Native2791A0, real map-cleanup caller.
	bool IsEnabled() const { return m_bEnabled; }
	bool IsActive() const { return m_bActive; }
	bool QueryOptionBool( const char *name, bool fallback ) const;
	bool CanPickupObject( CBaseEntity *object ) const;
	bool ShouldPlayBossMusic( bool *result ) const;
	void SaveTable( const char *name, HSCRIPT table );
	void RestoreTable( const char *name, HSCRIPT table );
	void ClearSavedTables();
private:
	bool m_bEnabled;
	bool m_bActive;
	int m_nSpawnSetRule; // native +268
	HSCRIPT m_hGetNextScriptedStage; // native +272, cached at activation
	int m_nEndScriptReason; // native +4
	char m_szScoreboardFilename[MAX_PATH]; // native +8
	bool m_bDebugMode; // native +276
	HSCRIPT m_hDebugTick; // native +280
	HSCRIPT m_hAllowTakeDamage; // native +284
	HSCRIPT m_hDamageInfoTable; // native +288
	HSCRIPT m_hAllowBash; // native +292
	HSCRIPT m_hInterceptChat; // native +304
	HSCRIPT m_hUserConsoleCommand; // native +308
	EHANDLE m_hRescueTrigger; // native +316
	unsigned int m_nBotQueryEpoch; // native +312; constructor0, Init1, script invalidation++
	CUtlVector< EHANDLE > m_ScriptSpawnedEntities; // Native328, Init clears count; actual script-create wrappers write.
	CTerrorScriptedHUD m_HUD; // native +348: actual HUD owner
	CTerrorScriptedJournal m_Journal; // native +376: actual journal/file owner
	KeyValues *m_pSavedTables; // native +408; owned KeyValues snapshots, not VM handles
};

class CDirectorItemManager;
class CDirectorVersusMode;
class CDirector : public CGameEventListener, public CMemZeroOnNew
{
public:
	struct TankPropRemoval
	{
		EHANDLE m_hProp;
		float m_flEarliestRemovalTime, m_flForcedRemovalTime;
	};
	CDirector();
	~CDirector();
	const CUtlVector< EHANDLE > &GetCarAlarms() const;
	void RegisterCarAlarm( CBaseEntity *alarm );
	void UnregisterCarAlarm( CBaseEntity *alarm );
	void ScriptRegisterForbiddenTarget( HSCRIPT target );
	void ScriptUnregisterForbiddenTarget( HSCRIPT target );
	HSCRIPT ScriptGetHighestFlowSurvivor() const;
	HSCRIPT ScriptGetLowestFlowSurvivor() const;
	HSCRIPT ScriptGetClosestSurvivorOrigin( const Vector &position, bool includeImmobile, bool excludeRescueVehicle ) const;
	HSCRIPT ScriptGetRandomSurvivor() const;
	int ScriptGetCommonInfectedCount() const;
	void Init(); // Native260550; one-time DLLInit AFTER constructor, not a Reset-only substitute.
	virtual bool CanChangeLevel() const;
	void Reset();
	void InitScripts();
	void ShutdownScripts( int reason, const char *nextMap );
	void ReleaseScriptHandles();
	void ResetChallengeScope();
	void UpdateScripts();
	void UpdateProviders();
	void UpdateMobSpawning();
	void UpdateGamePlaying();
	void UpdateRescueSpots();
	void SelectRescueSpot( bool inFinale, CTerrorPlayer *survivor );
	void UpdateInfectedCounts();
	void UpdateSurvivorFlow();
	void UpdatePipeBombs();
	int GetActivePipeBombCount() const { return m_pipeBombs.Count(); }
	CBaseEntity *GetNearestPipeBomb( const Vector &position ) const; // native262B30
	void UpdateSurvivorSeparation();
	void UpdateDirectorTestLoop();
	void DrawDebugText();
	void UpdateTankOffer();
	virtual void UpdateSpecialSpawning(); // S26F910, original root virtual88.
	virtual bool AreSpecialSpawnsAllowed() const; // S264A50, original root virtual92.
	virtual bool IsZombieClassSpawnAllowed( int zombieClass ) const; // S2656B0, root virtual52.
	virtual int GetDominatorLimit() const; // S2656A0, original root virtual116.
	void UpdateTempHealthDecayRate();
	virtual void UpdateInfectedCulling(); // Native virtual108/26AB60, real two-pass body owned by M6ZombieRelocate.
	void OnNavAreaChanged( CTerrorPlayer *player, CNavArea *newArea, CNavArea *oldArea );
	void UpdateSpawnRemovalQueue();
	void OnInfectedKilled( CTerrorPlayer *player ); // native2707E0, BEFORE SetZombieState0
	virtual void OnClientDisconnected( CTerrorPlayer *player ); // S2708E0, native root slot+56.
	void RegisterTankProp( CBaseEntity *prop );
	void QueueTankPropRemoval( CBaseEntity *prop, float delay, float forceDelay );
	void OnLastTankKilled();
	void UpdateIntensity();
	void UpdateMob();
	void ResetMissionEndState();
	void EndRound( int reason );
	void SetMissionEndDelay( float delay ) { m_MissionEndTimer.Start( delay ); } // native286600
	float GetMissionEndDeadline() const { return m_MissionEndTimer.m_timestamp; }
	void InvalidateMissionEndTimer() { m_MissionEndTimer.Invalidate(); } // native291AA0 global Director writer
	void CheckScenarioExit();
	void RestartScenario();
	void OnScenarioRestart(); // Native virtual64/25C440: same score owners, real CInfoDirector queued input.
	void FinishScenarioExit();
	void StartEndGameStats(); // native265200, actual env_outtro_stats/PZEndGame; NOT RestartScenario2700D0
	void HideScoreboards(); // native25C570, scripted->versus->scavenge actual panels
	void RestartCampaign(); // native26D490, real reset/first-map/restart path
	void RestartScenarioFromVote( const char *map ); // native269260, actual map/transition lifecycle
	void ChangeMissionFromVote( const char *mission ); // native2618A0, real mission dictionary/map lifecycle
	void ChangeChapterFromVote( const char *map ); // native2619D0
	void RestartVersusRoundFromVote(); // native290530 parent644/ApplyRoundEnd tail, NOT EndRound269130
	void NotifyMissionWon(); // native25F050 -> player316D80, Music::OnMissionWon then mission stats
	void ApplyRoundEnd( struct TerrorRoundEndInfo *info );
	void BindDirectorOptions();
	void RunScript( const char *scriptList, int level, bool includeAddonScripts );
	int GetDirectorOptionInt( const char *name, int fallback ) const;
	float GetDirectorOptionFloat( const char *name, float fallback ) const;
	const char *GetDirectorOptionString( const char *name, const char *fallback, char *destination, size_t count ) const;
	void GetDirectorOptionVector( const char *name, Vector *value ) const;
	HSCRIPT GetScriptScope( int level ) const { return m_hScriptScope[level]; }
	HSCRIPT GetOptionsScope() const { return m_hDirectorOptions; }
	CTerrorModeDescription *GetModeDescription() { return &g_TerrorModes; }
	const CTerrorModeDescription *GetModeDescription() const { return &g_TerrorModes; }
	CDirectorScriptedMode *GetScriptedMode() { return m_pScriptedMode; }
	const CDirectorScriptedMode *GetScriptedMode() const { return m_pScriptedMode; }
	CSavedTerrorPlayerStates *GetSavedPlayerStates() { return &g_SavedTerrorPlayerStates; }
	const CSavedTerrorPlayerStates *GetSavedPlayerStates() const { return &g_SavedTerrorPlayerStates; }
	CDirectorSessionManager *GetSessionManager() { return m_pSessionManager; }
	const CDirectorSessionManager *GetSessionManager() const { return m_pSessionManager; }
	CDirectorScenario *GetScenario() { return m_pScenario; }
	const CDirectorScenario *GetScenario() const { return m_pScenario; }
	DirectorMusicBanks *GetMusicBanks() { return m_pMusicBanks; }
	CDirectorScavengeMode *GetScavengeMode() { return m_pScavengeMode; }
	CDirectorVersusMode *GetVersusMode() { return m_pVersusMode; }
	CDirectorSurvivalMode *GetSurvivalMode() { return m_pSurvivalMode; }
	bool AreTeamsFlipped() const;
	bool IsInSecondHalfOfRound() const { return m_bInSecondHalfOfRound; }
	bool IsTransitioningToNextMap() const { return m_bTransitioningToNextMap; } // native1254
	bool IsVersusVoteRestarting() const { return m_bVersusVoteRestarting; } // native277
	void SwapRoleProviders();
	void SwapTeams(); // native26D830, actual roster transfer after both role owners
	bool ShouldResetTeamsOnMissionStart() const; // native25C4D0
	int GetCampaignMapType() const; // native25E260
	CDirectorItemManager *GetItemManager() { return m_pItemManager; }
	const char *GetGameMode() const { return GetModeDescription()->GetName(); }
	const char *GetBaseGameMode() const { return GetModeDescription()->GetBaseName(); }
	int GetSurvivorSet() const;
	int GetDifficulty() const;
	void ScriptResetMobTimer(); // Script ResetMobTimer262E90 != gameplay26AAA0.
	void ResetSpecialTimers();
	void PlayMegaMobWarningSounds();
	float GetAveragedSurvivorSpan() const { return m_flSurvivorFlowSpan; }
	float GetAveragedSurvivorSpeed() const { return m_flSurvivorMovement; }
	float GetFurthestSurvivorFlow() const { return m_flMaximumSurvivorFlow; }
	const char *ScriptGetMapName() const;
	bool ScriptIsL4D1Campaign() const;
	bool ScriptIsPlayingOnConsole() const { return false; } // Genuine PC-native25BF40.
	bool IsAnySurvivorInCombat() const; // Sole existing scenario query body, see below.
	bool ScriptIsTankInPlay() const { return m_nTankCount > 0; }
	float ScriptGetHoldoutCooldownEndTime() const;
	void ScriptSetHoldoutCooldownEndTime( float time );
	void ScriptForceNextStage();
	void WarpAllSurvivorsToCheckpoint();
	void WarpAllSurvivorsToBattlefield();
	void WarpAllSurvivorsToFinale();
	bool AreAllSurvivorsInFinaleArea() const;
	bool ScriptIsAnySurvivorInStartArea() const;
	bool ScriptAreAllSurvivorsInBattlefield() const;
	HSCRIPT ScriptGetClosestCharacterWhoIsIT( HSCRIPT entity ) const;
	bool IsSpawnPositionInFog( const Vector &position ) const;
	float ScriptGetTimeSinceSpecialZombieKilled( int zombieClass ) const;
	HSCRIPT ScriptGetClosestActivePipeBomb( const Vector &position ) const;
	int ScriptGetPendingMobCount() const;
	void ScriptUserDefinedEvent1();
	void ScriptUserDefinedEvent2();
	void ScriptUserDefinedEvent3();
	void ScriptUserDefinedEvent4();
	CDirectorMapJournal *GetMapJournal() { return &m_MapJournal; } // Native1288, NOT scripted-context journal376.
	void PreferL4D1Survivor( int character )
	{
		m_nPreferredL4D1Survivor = character; // Native25E370 writes1280 BEFORE1284.
		m_bUpdatePreferredL4D1Survivor = true;
	}
	void ClearCachedBotQueries() { m_pScriptedMode->ClearCachedBotQueries(); }
	void OnZombieDeath( CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	void L4D1SurvivorGiveItem() { m_bL4D1SurvivorItemRequest = true; } // native25CDB0
	bool ConsumeL4D1SurvivorItemRequest()
	{
		if ( !m_bL4D1SurvivorItemRequest ) return false;
		m_bL4D1SurvivorItemRequest = false; return true;
	}
	bool HasAnySurvivorLeftSafeArea() const { return m_bHasAnySurvivorLeftSafeArea; }
	void OnSurvivorLeftSafeArea( CTerrorPlayer *player );
	bool HaveAllSurvivorsLeftSafeArea() const { return m_bHasAllSurvivorsLeftSafeArea; }
	void UpdateStartAreaExit();
	void OnAllSurvivorsLeftSafeArea();
	void OnPlayersConnected();
	void OnSurvivorsReady();
	bool IsFirstChapter() const { return m_nCurrentChapterIndex == m_nCampaignStartChapterIndex; }
	bool IsPlayingIntro() const;
	void SetPlayingIntro( bool playing ) { m_bPlayingIntro = playing; }
	bool IsSpeechDisabled() const { return m_bSpeechDisabled; } // native657, real outro TRUE writers
	bool IsFinale() const { return m_pScenario->GetFinaleType() != 5; }
	bool IsFinaleWon() const { return m_pScenario->GetFinaleType() != 5 && m_nGameState == 3; } // native25C620
	bool IsFinaleVehicleLeaving() const { return m_pScenario->GetFinaleType() != 5 && m_nGameState == 2; } // native25C640
	int GetIntensityState() const { return m_nIntensityState; } // native25B860, NOT Rules difficulty
	int GetSurvivorRescueCount() const { return m_activeRescueSpots.Count(); } // Actual native992 vector /1004 count.
	bool IsGamePlaying() const { return m_bGamePlaying; }
	bool IsRescueVehicleReady() const;
	int GetChapterNumber() const { return m_nCurrentChapterIndex; }
	bool IsBackgroundMap() const;
	bool IsChangingMapOrMission() const;
	bool IsInTransition() const;
	bool IsTransitionInProgress() const { return m_bTransitionInProgress; }
	bool IsTransitionRestoreActive() const;
	bool IsSwappingTeams() const { return m_bSwappingTeams; }
	bool IsRestoringByCharacter() const { return m_bRestoreByCharacter; }
	void MarkTransitionEntitiesRestored() { if ( !m_bTransitionEntitiesRestored ) m_bTransitionEntitiesRestored = true; }
	void UpdateHumanSurvivorCount();
	void UpdateCheckpointHumanSurvivorCount();
	bool IsPublicLiveSession() const;
	bool IsOfflineSession() const; // Native25BED0; explicit offline-launch MatchSession adaptation.
	int GetMissionID() const { return m_nMissionID; }
	int GetMissionDuration() const; // native25DB10, actual resource wire truncation; NOT x87 round_end arithmetic
	int GetMissionLossCount() const { return m_nMissionLossCount; } // native25C530, real1088
	int GetRoundEndReason() const { return m_nRoundEndReason; } // native +644
	int CountPlayersOnTeam( int team, bool aliveOnly ) const;
	int GetTeamRosterCount( int team, int countMode ) const; // Native262670; excludes saved/pending players, includes bots when countMode!=0.
	int GetTeamPlayerCount( int team, int countMode ) const; // Native2625F0, real saved/roster/stale-spectator producers; PostThink phase owner.
	bool IsTeamFull( int team, int countMode ) const; // Native2626D0; no implicit/default count mode or alternate cap state.
	float GetMobSpawnMinTime() const;
	float GetMobSpawnMaxTime() const;
	double GetMobSpawnTimeRemaining() const; // Native2601F0, SAME existing Mob timer.
	void CreateSurvivorBot( SurvivorCharacterType character );
	void JoinNewPlayer( const CHandle< CTerrorPlayer > &player );
	KeyValues *GetPlayerAvatarInfo( edict_t *player ) const; // native26CDD0, borrowed
	void OnPlayerAvatarInfoReceived( edict_t *player, KeyValues *info ); //274F90
	void UpdatePlayerAvatarInfo( uint64 identity, KeyValues *info ); //2740A0, owned copy/removal
	static void DecodePlayerAvatarInfo( KeyValues *info, int *team, SurvivorCharacterType *character ); //25C330
	bool AreSurvivorBotsDisabled() const;
	void RestoreSavedPlayers();
	void RestoreSavedSurvivorBots();
	bool ProcessTransitionRestoration();
	void UpdateMissionStatus();
	void ReacceptSavedTankOffer();
	void OnNoHumanPlayersLeft();
	void RestartAfterNoHumanPlayers(); // S:271BA0, actual campaign lifecycle
	void UpdateLowViolenceCombatCharacters();
	void DrawSessionDebugLine( int line, const char *text, int red, int green, int blue );
	void BeginGameSession(); // SDK GameInit consumes the one-shot offline launch setting.
	void OnNewGameSession();
	int GetSpecialClassRotationSeed() const { return m_nSpecialClassRotationSeed; } // Native1272 -> actual305850 next-class picker.
	float GetResponseSurvivorDistance( int from, int to, bool mapCharacters ) const;
	int GetClosestResponseSurvivor( int character ) const;
	int GetNumberInSafeSpot() const;
	int GetNumberOutsideSafeSpot() const;
	int GetCampaignRandomNumber() const;
	const char *GetIntroActorName() const;
	const char *GetScenarioName() const;
	void OnLevelShutdown();
	bool IsBleedoutSuppressed() const;
	bool IsRestartingScenario() const;
	bool IsMissionLostOrWon() const;
	bool ShouldPlayBossMusic() const;
	bool AllowSurvivorRescue( bool fallback ) const;
	void OnFinaleStarted( int type );
	bool OnScriptedPanicEvent( CTerrorPlayer *player, int reason );
	void ForceGauntletTank();
	void OnFinaleVehicleIncoming();
	void OnFinalePause();
	void StartScenarioExit();
	void BeginLocalScript( const char *script );
	void StopLocalScript();
	void OnFinaleVehicleReady();
	void OnFinaleVehicleLeaving( const CUtlVector< CTerrorPlayer * > &escaped );
	void OnFinaleWon();
	bool IsJimmyGibbsDay() const;
	void FireCustomStageOutput();
	void AddToSpecialSpawnTimers();
	int GetCommonLimit() const;
	int GetActiveCommonCount() const { return m_nActiveCommonCount; } // Native25C7B0.
	int GetNumReservedWanderers() const;
	int GetWitchLimit() const;
	int GetInfectedFlags() const;
	bool AreWitchesAllowedInCheckpoints() const;
	bool SpawnThreat( TerrorNavArea *area );
	virtual int GetMaxSpecialCount() const;
	bool IsAllBotSurvivorTeamAllowed() const; // native25E250, sole frame ConVar owner
	double GetWanderingZombieDensityModifier() const;
	bool ShouldSpawnWanderers() const; // Native265530, real scavenge/Tank/intensity/daytime arbitration.
	bool HasSurvivorsAdvancedBeyondTank() const; // Native25C740, sole M5 boss_bot_director_queries.cpp owner.
	float GetMaximumSurvivorIntensity() const; // Native260820/25E750, same raw12020 collector used by Frame.
	double GetSpecialRespawnInterval() const;
	double GetZombieDiscardRange() const; // Native2FDDC0, real ZombieDiscardRange option/z_discard_range.
	double GetInfectedSpawnDelay( float elapsed ); // native263000, real initial timer464 writer
	void OnPlayerDeath( CTerrorPlayer *player ); // native262100, actual player_death event timer writer
	void OnPlayerTeamChanged( CTerrorPlayer *player, int oldTeam ); // native2709D0, actual saved-Tank offer reacceptance
	bool IsTempoLocked() const; // native266500, sole frame implementation / genuine mode-state readers
	void DelayNextSpecialSpawn( float delay ); // native2608B0, actual828 special-timer vector writer
	double GetSpecialSlotCountdownTime() const;
	virtual int GetSpecialClassLimit( int zombieClass ) const;
	int GetTankLimit() const;
	bool AreBossesProhibited() const;
	double GetDirectorTimerRemaining( int index ) const; // native25D400 x87 return
	double GetSpecialSpawnTimerRemaining( int index ) const; // native260510 x87 return
	int GetSpecialSpawnTimerCount() const { return m_SpecialSpawnTimers.Count(); }
	int GetMegaMobSize() const;
	int GetMobSize() const;
	int GetTankCount() const;
	double GetTankHealth() const; // Native2661C0, real difficulty/PZ option multiplier.
	void OnPlayerSpawned( CTerrorPlayer *player ); // Native262080, timer364 then true3176F0 fan-out.
	int GetWitchCount() const;
	void OnWitchSpawn();
	void OnWitchRemoved();
	int GetChapterPanicWaveTier() const;
	void OfferTank( CTerrorPlayer *player, bool force );
	bool IsTankFrustrationEnabled() const { return m_bTankFrustrationEnabled; } // native476
	void EnableTankFrustration() { m_bTankFrustrationEnabled = true; } // native25CBB0, actual InfoDirector input
	void DisableTankFrustration() { m_bTankFrustrationEnabled = false; } // native25CBC0, actual InfoDirector input
	int GetTankControlPassCount() const { return m_nTankControlPassCount; } // native280, retained across Reset
	bool IsTankFrustrationSuppressed(); // native262390, BYREF checkpoint functor result, NOT literalfalse
	double GetTankOfferLotteryProgress() const; // native343130, timer376
	double GetTankControlTransferProgress() const; // native3431A0, timer388
	void ClearTankControlWinner() { m_nTankOfferWinner = 0; } // native469CC0 Tank death's FIRST unconditional writer
	int GetTankControlWinner() const { return m_nTankOfferWinner; } // native343340 real resource writer
	void ResetThreatPriorities( int previousThreat );
	void CommandInfectedToAttackSurvivors();
	void ResetMobTimer();
	void OnPanicEventCompleted();
	void EndScenario();
	void PositionFinaleEscapees( const CUtlVector< CTerrorPlayer * > &escaped );
	void IncrementTeamScore( int score, CBaseEntity *scorer ); // Native25D960; sole score/OnTeamScored/last-scorer owner.
	void SpawnScavengeItems(); // Native260CA0, no replay argument or extra replay storage.
	void OnRoundEnd( bool saveRemainingTime );
	void OnRoundStart();
	void OnLevelChangeComplete();
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;

	friend class CDirectorScenario;
	friend class CDirectorScriptedMode;
	friend class CDirectorScavengeMode; // Actual child reads928 and writes1252 at286C60.
	friend class COuttroStats; // native outro directly writes actual657; no parallel speech state
	friend class Infected;
	friend class ZombieManager;
private:
	bool TryTakeOverSurvivorCharacter( const CHandle< CTerrorPlayer > &player, SurvivorCharacterType character );
	bool TryTakeOverAvailableSurvivor( const CHandle< CTerrorPlayer > &player );
	int GetNextThreatType();
	void UpdateWorldMusic();
	void ResetRoundSpeech(); // Native265450, actual contexts/remarkables/response groups/pending queue.
	void FreezeRoundPlayers(); // native25EA40
	void ResetRoundReports(); // native25EAC0
	void PrepareFinaleLossPlayers(); // native267520 ->2652C0
	void EnableFinaleWinCamera(); // native25DA10
	void PrepareTankOffer( CTerrorPlayer *player, bool force ); // native260840
	void ClearTankOffer(); // native25D580, preserves stale-unresolved handle omission
	int m_nHumansNearCheckpoint; // native268; explicit ctor0 before matrix
	int m_nHumansAwayFromCheckpoint; // native272; explicit ctor0 before matrix
	float m_survivorSeparation[8][8]; // native12; early native memset256
	bool m_bSwappingTeams; // native276
	bool m_bVersusVoteRestarting; // native277
	int m_nTankControlPassCount; // native280; allocation0, NO ctor/Reset store
	CUtlMap< uint64, KeyValues * > m_playerAvatars; // native284; real comparator ctor
	CUtlVector< EHANDLE > m_TankProps; // native312
	CUtlVector< TankPropRemoval > m_TankPropRemovalQueue; // native332
	bool m_bHasAnySurvivorLeftSafeArea; // native352; allocation0, NO ctor store
	bool m_bHasAllSurvivorsLeftSafeArea; // native353; allocation0, NO ctor store
	int m_nGameState; // native356; allocation0, NO ctor store
	bool m_bForceGauntletTank; // native360
	CountdownTimer m_SpecialSpawnTimer; // native364
	CountdownTimer m_TankOfferLotteryTimer; // native376
	CountdownTimer m_TankControlTransferTimer; // native388
	EHANDLE m_hPendingTankOffer; // native400; default invalid, repeated late ctor clear
	int m_nIntensityState; // native404; allocation0, NO ctor store
	CountdownTimer m_IntensityStateTimer; // native408
	float m_flIntensityStartFlow; // native420; allocation0, NO ctor/Reset store
	// native424 vector is closed write-only; no target member or reset.
	int m_nWitchCount; // native444; explicit late ctor0, actual Witch lifetime
	int m_nTankCount; // native448; allocation0, actual frame count
	float m_flTankStartFlow; // native452; allocation0, NO ctor store
	int m_nSpecialCount; // native456; allocation0, NO ctor store
	int m_nHumanInfectedCount; // native460; allocation0, NO ctor store
	CountdownTimer m_InitialSpawnDelay; // native464
	bool m_bTankFrustrationEnabled; // native476; allocation0, Reset1
	int m_nHordeEscapeCommonLimit; // native480; allocation0, NO ctor store
	int m_nSurvivorFlowIndex; // native484; allocation0, NO ctor store
	float m_minSurvivorFlow[10]; // native488; allocation0, NO ctor store
	float m_maxSurvivorFlow[10]; // native528; allocation0, NO ctor store
	CountdownTimer m_SurvivorMovementTimer; // native568
	float m_flSurvivorFlowSpan; // native580; allocation0, NO ctor store
	float m_flSurvivorMovement; // native584; allocation0, NO ctor store
	float m_flMaximumSurvivorFlow; // native588; allocation0, NO ctor store
	CountdownTimer m_navAreaChangeTimer; // native592
	CountdownTimer m_MissionStatusTimer; // native604
	CountdownTimer m_MissionEndTimer; // native616
	CountdownTimer m_ScenarioExitTimer; // native628
	bool m_bGamePlaying; // native640; allocation0, NO ctor store
	bool m_bEverHadHumanSurvivor; // native641
	bool m_bEverHadHumanPlayer; // native642
	int m_nRoundEndReason; // native644; allocation0, NO ctor store
	int m_nHumanSurvivorCount; // native648; allocation0, NO ctor store
	bool m_bTransitionInProgress; // native652
	bool m_bTransitionEntitiesRestored; // native653
	bool m_bTransitionedToMap; // native654
	bool m_bRestoreByCharacter; // native655
	bool m_bPlayingIntro; // native656
	bool m_bSpeechDisabled; // native657
	CountdownTimer m_MobSpawnTimer; // native660
	// native672 timer is closed write-only; no target member or invalidation.
	IntervalTimer m_ClassSpawnIntervals[9]; // native684; construct elements0..8
	CountdownTimer m_BaseSpecialSpawnTimers[6]; // native756; construct0..5
	CUtlVectorFixedGrowable< CountdownTimer, 3 > m_SpecialSpawnTimers; // native828; fixed storage constructs3, Count stays0
	// native888 timer is closed write-only; no target member.
	float m_flMobSize; // native900; allocation0, NO ctor store
	float m_flMobSizeIncrement; // native904; allocation0, NO ctor store
	float m_flSavedMobInterval; // native908
	float m_flSavedSpecialInterval; // native912
	int m_nSavedTankControllerUserID; // native916
	int m_nTankOfferWinner; // native920; allocation0, NO ctor store
	int m_nActiveCommonCount; // Native924, allocation0, SetActive edges/Reset only.
	int m_nScavengeItemsRemaining; // native928; allocation0, NO ctor store
	EHANDLE m_hLastGasCanPourer; // native932; default invalid once
	IntervalTimer m_MissionTimer; // native936
	float m_flMissionElapsedTime; // native944
	char m_originalMapName[32]; // native948; ctor writes ONLY first char
	CountdownTimer m_RescueUpdateTimer; // native980
	CUtlVector< CHandle< CSurvivorRescue > > m_activeRescueSpots; // native992
	CountdownTimer m_SpecialZombieOfferTimer; // native1012
	CUtlVector< EHANDLE > m_SpecialZombieOfferPlayers; // native1024
	// native1044 owned Population KV is CInfectedPopulation::m_pPopulation.
	int m_nMissionID; // native1048
	int m_nAllLeftStartAreaMissionID; // native1052
	int m_nCurrentChapterIndex; // native1056
	int m_nCampaignStartChapterIndex; // native1060
	CountdownTimer m_ScriptedPanicCooldown; // native1064
	CountdownTimer m_PanicStartCooldown; // native1076
	int m_nMissionLossCount; // native1088
	int m_ThreatOrder[3]; // native1092..1100; allocation0, NO ctor store
	int m_nThreatOrderIndex; // native1104; allocation0, NO ctor store
	CUtlVector< EHANDLE > m_pipeBombs; // native1108
	CountdownTimer m_PipeBombScanTimer; // native1128
	CUtlVector< EHANDLE > m_carAlarms; // native1140; real registry, car_alarm packet
	// Existing explicit VM-scope ownership is retained, not shadowed by another
	// CScriptScope/cache. Native helper initialization1160..1219 is represented
	// by these five invalid scope handles and the existing release/rebind owner.
	HSCRIPT m_hScriptScope[4]; // native1160/1172/1184/1196 handle components
	HSCRIPT m_hDirectorOptions; // native1208 handle component
	HSCRIPT m_hScriptUpdate[4]; // native1220..1232; ALL FOUR are live
	CountdownTimer m_ScriptUpdateTimer; // native1236
	float m_flScriptUpdateInterval; // native1248
	bool m_bDelayMissionEndScoreboard; // native1252; allocation0, NO ctor store
	bool m_bInSecondHalfOfRound; // native1253
	bool m_bTransitioningToNextMap; // native1254
	bool m_bMissionEndBlocked; // native1255
	CountdownTimer m_HumanSurvivorGraceTimer; // native1256
	bool m_bL4D1SurvivorItemRequest; // native1268
	bool m_bAllowTankThreat; // Native1269, distinct from item-request1268; allocation0.
	int m_nSpecialClassRotationSeed; // native1272; allocation0, NO ctor RNG/store
	int m_nCampaignRandomNum; // native1276; ctor LAST RandomInt(0,100), NOT-1
	int m_nPreferredL4D1Survivor; // native1280; after RandomInt(0,1)
	bool m_bUpdatePreferredL4D1Survivor; // native1284; stored before1280
	CDirectorMapJournal m_MapJournal; // native1288; Main's sole vector/CFmtStrN owner
	// native1576 stateless receiver/allocation elided by Main; real queries use
	// the existing free-function owners, not an empty surrogate class.
	CDirectorItemManager *m_pItemManager; // native1580; first child new
	DirectorMusicBanks *m_pMusicBanks; // native1584
	CDirectorSessionManager *m_pSessionManager; // native1588
	CDirectorScenario *m_pScenario; // native1592
	CDirectorVersusMode *m_pVersusMode; // native1596
	CDirectorSurvivalMode *m_pSurvivalMode; // native1600
	CDirectorScavengeMode *m_pScavengeMode; // native1604
	CDirectorScriptedMode *m_pScriptedMode; // native1608; native dtor does NOT delete
	bool m_bOfflineSession; // SDK-owned session flag; persists across changelevel, not GameShutdown.
};
extern CDirector *TheDirector;
extern bool g_bTerrorTransitionRestore; // native global 83B1BD
extern bool g_bTerrorRestartAfterDeath; // native global 83B1BC
#endif
