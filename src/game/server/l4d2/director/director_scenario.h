//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_DIRECTOR_SCENARIO_H
#define L4D2_DIRECTOR_SCENARIO_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "NextBot/NextBot.h"

class CTerrorPlayer;

// Reconstructed name: the native object at Director+1592 has no proven RTTI
// name or vtable. These are native states, NOT mutation IDs. The Director's
// separate +356 escape/win state does not live in this object.
class CDirectorScenario : public CMemZeroOnNew
{
public:
	enum FinaleStage
	{
		FINALE_GAUNTLET_1 = 0, FINALE_HORDE_ATTACK_1, FINALE_HALFTIME_BOSS,
		FINALE_GAUNTLET_2, FINALE_HORDE_ATTACK_2, FINALE_FINAL_BOSS,
		FINALE_HORDE_ESCAPE, FINALE_CUSTOM_PANIC, FINALE_CUSTOM_TANK,
		FINALE_CUSTOM_SCRIPTED, FINALE_CUSTOM_DELAY, FINALE_CUSTOM_CLEAROUT,
		FINALE_GAUNTLET_START, FINALE_GAUNTLET_HORDE,
		FINALE_GAUNTLET_HORDE_BONUSTIME, FINALE_GAUNTLET_BOSS_INCOMING,
		FINALE_GAUNTLET_BOSS, FINALE_GAUNTLET_ESCAPE, FINALE_CUSTOM_NONE,
		FINALE_CUSTOM_SETUP, FINALE_CUSTOM_SPECIAL_EVENT, FINALE_CUSTOM_ESCAPE,
		FINALE_CUSTOM_RESULTS
	};
	enum PanicStage
	{
		STAGE_INITIAL_DELAY = 0, STAGE_MEGA_MOB, STAGE_WAIT_FOR_COMBAT_TO_END,
		STAGE_PAUSE, STAGE_DONE
	};

	CDirectorScenario();                          // S:287DB0; not Reset
	void Reset();                                 // S:289340
	int DrawDebugText( int line ); // Native288520, existing actual Scenario owner.
	void OnMapStart();                            // S:287860
	void OnFinaleTriggerSpawned();                 // S:287A50
	void OnFinaleStarted( int type );              // S:28A7A0
	bool OnScriptedPanicEvent( CTerrorPlayer *player, int reason ); // S:28ACA0
	void StartPanicEvent( int waves, CTerrorPlayer *player = NULL ); // S:289FB0
	void CancelPanicEvent();                       // S:288E40
	void CancelFinale() { m_nFinaleType = 5; }      // S:287980
	void AdvanceStage();                          // S:28B570
	void ChangeFinaleStage( int stage, const char *music = NULL ); // S:28A2B0
	void ApplyCustomStage( int type, const char *music ); // S:28B190
	bool Update();                                // S:28C3E0
	bool UpdateFinale();                          // S:28B7C0
	void UpdatePanic();                           // S:28C400
	void UpdateGauntletMovement();                 // S:288F00
	void UpdateSacrifice();                       // S:28AED0
	void StopGauntletPanic();                      // S:28ABB0
	void OnEscapeVehicleReady();                  // S:289F00
	void OnSacrificeFailed() { m_bSacrificeFailed = true; } // S:287990
	void OnSacrificeSucceeded() { m_bSacrificeSucceeded = true; } // S:2879A0
	void QueueStageAdvance() { ++m_nQueuedStageAdvances; } // S:287830
	void ClearCustomFinaleChoice() { m_customFinaleChoice = 0; } // S:271BA0
	void SetCooldown( float worldTime );           // S:288460
	float GetStageTimeRemaining() const;           // S:287AD0
	float GetCustomScriptedStageProgress( float fallback ) const; // S:289D60
	double GetStageElapsedTime() const;            // S:2DA3C0, actual interval188
	bool ShouldAllowMobs() const;                  // S:289F30
	bool AreSurvivorsInFinaleAreaBeforePanic() const; // S:288E00
	bool IsEscapeStageOrCompletedPanic() const;    // S:3BACA0
	bool AreAllLivingSurvivorsInBattlefield( bool *noLivingSurvivors = NULL ) const; // S:287C70
	bool AreAllSurvivorsOutsideBattlefield() const; // S:287D30
	bool IsPanicForever() const;                   // S:287940
	void OverrideSpecialRespawnInterval( float *interval ) const; // S:287910
	void WarnMegaMobIncoming();                    // S:28B460

	int GetFinaleType() const { return m_nFinaleType; }
	int GetFinaleStage() const { return m_nFinaleStage; }
	int GetCustomStage() const { return m_nCustomStage; }
	int GetCustomStageCount() const { return m_nCustomStageCount; }
	int GetPanicStage() const { return m_nPanicStage; }
	bool IsPanicEventActive() const { return m_nPanicStage != STAGE_DONE; }
	bool IsFirstPanicEvent() const { return m_bFirstPanicEvent; }
	bool HasStartedFirstPanicEvent() const { return m_bStartedFirstPanicEvent; }
	bool IsGauntlet() const { return m_nFinaleType == 1; }
	bool IsSacrificeFinale() const { return m_bSacrificeFinale; }
	bool HasSacrificeSucceeded() const { return m_bSacrificeSucceeded; }
	bool HasSacrificeFailed() const { return m_bSacrificeFailed; }

private:
	int IncrementCustomStage();                    // S:289990
	void StartEscape();                           // S:289E40
	void StartCustomEscape();                     // S:28A5A0
	void UpdateEscape();                          // S:28A650
	void StartSetupStage();                       // S:288030
	bool UpdateCustomTank();                      // S:2881A0
	bool UpdateDelay();                           // S:288310
	bool UpdateResults();                         // S:2883B0
	void UpdateClearout();                        // S:28B660
	void OnPanicEventFinished();                   // S:28C380
	void PlayFinaleMusic( const char *event ) const; // S:2B0BC0
	void PrepareEscape();                         // S:287B80
	bool SpawnFinaleTank();
	void StartGauntletHorde();

	int m_nFinaleType;                             // native +0, sentinel 5
	int m_nFinaleStage;                            // native +4
	char m_stageValueName[128];                    // native +8
	bool m_bStopGauntlet;                          // +136
	bool m_bEscapePrepared;                        // +137 (Start, NOT Reset, clears)
	bool m_bBossSpawned;                           // +138
	bool m_bWaitingForBoss;                        // +139
	float m_flBossFlow;                            // +140
	CountdownTimer m_stageTimer;                   // +144
	float m_flGauntletMovementThreshold;            // +156
	float m_flGauntletMovementBonus;                // +160
	CountdownTimer m_gauntletMovementTimer;         // +164
	CountdownTimer m_gauntletBonusTimer;            // +176
	IntervalTimer m_stageInterval;                 // +188
	CountdownTimer m_clearoutTimer;                // +196
	int m_nLastClearoutCommons;                    // +208
	CountdownTimer m_nativeTimer212;               // +212; constructor reached
	int m_nLastClearoutSpecials;                   // +224
	int m_nCustomStageCount;                       // +228
	int m_nCustomStage;                            // +232; actual script stage counter
	int m_nStageValue;                             // +236
	char m_customFinaleChoice;                     // +240
	CountdownTimer m_bossTimer;                    // +244
	int m_nQueuedStageAdvances;                    // +256
	CountdownTimer m_minimumStageTimer;            // +260
	bool m_bStartedFirstPanicEvent;                // +272
	bool m_bFirstPanicEvent;                       // +273
	bool m_bFinishCooldown;                        // +274
	int m_nDiagnosticClearoutCommons; // Native276, Main-frozen read-live allocationzero; NO ctor/Reset stores.
	int m_nDiagnosticClearoutSpecials; // Native280, Main-frozen read-live allocationzero; NO setter/mirror.
	CountdownTimer m_nativeTimer284;               // +284; Reset invalidates
	int m_nPanicStage;                             // +296
	CountdownTimer m_panicTimer;                   // +300
	int m_nPanicWave;                              // +312
	int m_nPanicWaveCount;                         // +316
	bool m_bSacrificeFinale;                       // +320
	bool m_bSacrificeSucceeded;                    // +321
	bool m_bSacrificeFailed;                       // +322
	CountdownTimer m_sacrificeFailureTimer;        // +324
	bool m_bNative336;                            // +336; Reset writes false
};
#endif
