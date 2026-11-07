//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_SCAVENGE_MODE_H
#define DIRECTOR_SCAVENGE_MODE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "tier1/utlvector.h"
class CTerrorPlayer;
class CDirectorScavengeMode : public CMemZeroOnNew
{
public:
	CDirectorScavengeMode();
	~CDirectorScavengeMode();
	void Reset();
	int SpawnScavengeItems();         // S:260CA0/287230, replay policy is Director1253.
	int BuildClusters();
	bool SelectNextCluster();
	void RemoveItem( CBaseHandle item );
	void StartRound();
	void StartReadyCountdown();       // S:2863A0
	bool UpdateReadyCountdown();      // S:2867B0
	bool ShouldBlockStartAreaExit( CTerrorPlayer *player ); // S:285F70
	void OnGameplayStart();           // S:285720
	void OnGasCanPoured( int team );
	bool HasStarted() const { return m_bStarted; }
	void OnMissionStart();
	bool SwapRoles();
	bool AreTeamsFlipped() const { return m_bTeamsFlipped; }
	void HideScoreboards(); // native286540, actual scavenge mode guard
	void ResetTeams() { m_bTeamsFlipped = false; } // Native no-human restart's actual byte10 writer.
	void CheckMissionStatus();        // S:286B80, actual frame caller.
	bool HandleRoundEnd();            // S:286F50 ->286C60, actual world-death arbitration.
	void FinishRound();               // S:286C60.
	void EndRound();                  // S:2868B0, after the Director deadline.
	void ResetRound();                // S:285830 ->265200.
	void StartOvertime();             // S:286090.
	void EndOvertime( bool timeAdded ); // S:286110.
	bool UpdateOvertime();            // S:2868F0, true only after the no-item grace expires.
	bool BeginOvertime();             // S:2869A0, no item means immediate completion.
	void CancelReadyCountdown();      // S:286470.
	int GetReadyCountdownSeconds() const; // Native285890, widened difference/SSE2 signed32 conversion.
private:
	void ClearClusters();
	void UpdateTimeLevel();           // S:285FE0, real HUD/music/output producer.
	static bool AreNoScavengeItemsHeld(); // S:285C30/2856D0, active weapon ID16.
	static int CompareSpawnDistance( const EHANDLE *left, const EHANDLE *right );
	CUtlVector< CUtlVector< EHANDLE > > m_Clusters;
	CUtlVector< int > m_ClusterOrder;
	int m_nCurrentCluster;
	int m_nReplayIndex;
	bool m_bScenarioExitPending; // Native0: allocation0, mission0, EndRound true writer.
	bool m_bStarted; // Native8, NOT the native0 scenario-exit phase.
	bool m_bApproachingRoundWin; // Native9.
	bool m_bTeamsFlipped; // Native10; round Reset retains.
	bool m_bInOvertime; // Native11.
	bool m_bWasInOvertime; // Native12; StartRound false, StartOvertime true; Reset retains.
	float m_flLastPourTime; // Native4; allocation0/StartRound0/pour now; Reset RETAINS.
	int m_nIntensity; // Native40, actual time-level cursor; Reset RETAINS.
	CountdownTimer m_StartTimer; // Native16, Reset retains.
	CountdownTimer m_OvertimeGraceTimer; // Native28, Reset retains.
	Vector m_TargetPosition; // Native84, mission clears.
	friend class CDirector;
};
void Script_ForceScavengeStart();
#endif
