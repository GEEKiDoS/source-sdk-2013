//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef DIRECTOR_SURVIVAL_MODE_H
#define DIRECTOR_SURVIVAL_MODE_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/mem_zero_on_new.h"
#include "util_shared.h"
class CTerrorPlayer;
struct TerrorRoundEndInfo;
// Actual Director1600 owner; native zero-allocation state is initialized here.
class CDirectorSurvivalMode : public CMemZeroOnNew
{
public:
	CDirectorSurvivalMode();
	void Reset(); //28DF40 only started0/timer44
	void OnLevelChangeComplete(); //28DC40 clears restartPending1 only
	void OnGameplayStart(); //28DF80 setup clock
	void OnRoundStarted( int startType, bool startClock ); //28E560
	int OnScriptedPanicEvent( CTerrorPlayer *player, int reason ); //28E910 disposition
	bool HandleRoundEnd( TerrorRoundEndInfo *info ); //28EA20
	void EndRound(); //28E9D0
	void RestartRoundFromVote(); // native28E940: EndRound14 then unconditional global deadline1s override
	bool Update(); //28EE00
	int DrawDebugText( int line ); // Native28E080, not another survival state model.
	bool HasStarted() const { return m_bStarted; }
	// Native+4 is written by OnRoundStarted, independently of started byte0.
	int GetSpawnState() const { return m_nStartType; }
	bool OverrideSpecialRespawnInterval( float *interval ) const;
	bool OverrideMaxSpecials( int *count ) const;
	bool OverrideSpecialClassLimit( int zombieClass, int *count ) const;
	bool OverrideTankLimit( int *count ) const;
private:
	void SpawnHorde(); //28EC60
	bool m_bStarted; //0
	bool m_bRestartPending; //1
	bool m_bRoundClockStarted; //2
	int m_nStartType; //4
	CountdownTimer m_Timers[6]; //8,20,32,44,56,68
	int m_SpecialLimits[7]; //80..104: six class caps, then total specials
	int m_nTankLimit; //108
	int m_nMaxTankStagesPerWave; //112
	float m_flHordeInterval; //116
	float m_flTankInterval; //120
	float m_flSpecialSpawnInterval; //124
	float m_flLullDuration; //128
	int m_nWave; //132
	int m_nTanksThisStage; //136
	int m_nTanksSpawnedThisStage; //140
	int m_nTankStagesPerWave; //144
	int m_nCompletedTankStages; //148
	int m_nTotalTankStages; //152
	int m_nNextSetupAnnouncement; //156
};
#endif
