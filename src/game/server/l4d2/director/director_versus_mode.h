//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_VERSUS_MODE_H
#define DIRECTOR_VERSUS_MODE_H
#ifdef _WIN32
#pragma once
#endif
#include "util_shared.h"
#include "l4d2/mem_zero_on_new.h"
class CTerrorPlayer;
CTerrorPlayer *PickLivingSurvivorForForceStart();

// The genuine Director+1596 owner. Mode metadata remains g_TerrorModes-owned;
// local state below is produced by its actual native round/phase lifecycle.
class CDirectorVersusMode : public CMemZeroOnNew
{
public:
	CDirectorVersusMode();
	void OnNewGame();
	void Reset();
	void OnLevelChangeComplete();
	void ResetCampaignScores( bool clear );
	void OnGameplayStart();
	void OnRoundStarted(); // native2905C0, actual start-area writer
	void RestartScenarioFromVote(); // native290530, actual saved-score restore and reason7 arbitration
	void FireGameEvent( IGameEvent *event ); // native291B80, actual map_transition
	void Update();
	void UpdateMissionEnd();
	void EndRound();
	void FinishRound();
	void FinishVersusRound( bool success );
	void HideScoreboards();
	bool OnScenarioExit(); // native291AA0, invalidates GLOBAL Director timer before mode dispatch
	bool IsActive() const;
	virtual bool CanChangeLevel() const;
	void OnFinaleTriggerSpawned();
	bool IsFinaleActive() const { return m_bRoundStarted; } // native field4, real gameplay/finish writers
	bool IsSacrificeActive() const { return m_bFinaleMap; } // native290700 field68
	int GetFinaleType() const { return m_nFinaleType; }
	double GetFinaleTravelCompletion() const { return m_flFinaleTravelCompletion; } // native290710
	bool OverrideSpecialRespawnInterval( float *interval ) const; // native290470
	bool SwapRoles();
	bool AreTeamsFlipped() const { return m_bTeamsFlipped; }
	bool IsVoteRestarting() const { return m_bIsVoteRestarting; } // Native24, Rules1175 replication reader.
	void ForceSurvivorsOutOfCheckpoint();
	void ConfigureBossSpawns();
private:
	void UpdateBossSpawning();
	void UpdateTravelMarkers();
	void AccumulateCampaignScores();
	bool m_bRoundStarted; // native +4
	bool m_bTeamsFlipped;
	bool m_bShutdownScoreboardShown; // native +6
	int m_TeamScore[2]; // native +8,+12
	int m_SavedTeamScore[2]; // native +16,+20
	bool m_bIsVoteRestarting; // Native24, ctor/newgame/finish false; vote restart true; Reset retains.
	CountdownTimer m_ForceStartTimer; // native +28
	float m_TankFlow[2]; // native +40,+44
	float m_WitchFlow[2]; // native +48,+52
	bool m_bTankPending[2]; // native +56,+57
	bool m_bWitchPending[2]; // native +58,+59
	float m_flFinaleTravelCompletion; // native +60
	int m_nFinaleType; // native +64, actual trigger_finale.m_type
	bool m_bFinaleMap; // native +68
	int m_nTravelMarker; // native +72
	int m_nTravelMarkerCount; // native +76
	CountdownTimer m_MarkerTimer; // native +80
};
#endif
