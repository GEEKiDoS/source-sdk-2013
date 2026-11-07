//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef SURVIVOR_SPEECH_H
#define SURVIVOR_SPEECH_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_speech.h"
#include "util_shared.h"

class CTerrorPlayer;
class CNavArea;

// Original player+12580 (S3571A0). This is the one player's native speech
// state, not a second expresser host or a game-event listener.
class CSurvivorSpeech
{
public:
	explicit CSurvivorSpeech( CTerrorPlayer *player );
	void Reset();
	// S3290A8 clears only this separate final-Spawn scenario latch.
	void ResetScenarioJoin() { m_HasJoinedScenario = false; }
	// S325E60/31B7B0/2B6C40/2B8B60 transfer the same speech-owned latch.
	bool HasJoinedScenario() const { return m_HasJoinedScenario; }
	void SetHasJoinedScenario( bool joined ) { m_HasJoinedScenario = joined; }
	void Update();
	bool TryUseSpeech();
	bool WarnAboutSpecial( CBaseCombatCharacter *target );
	void OnMoveBlocked( CBaseEntity *entity );
	void OnNavAreaChanged( CNavArea *newArea, CNavArea *oldArea );
	int GetLastWarnedActorID( int zombieClass ) const { return m_LastWarnedActorID[zombieClass]; }
private:
	bool Speak( AIConcept_t concept, int priority = 1, const char *modifiers = NULL );
	void OnEnteredNavArea( CNavArea *area );
	void AnnounceNearbyGoal( bool finale );
	CTerrorPlayer *m_Player;
	Vector m_LastEyeForward;
	IntervalTimer m_SteadyEyeTimer;
	CountdownTimer m_TargetScanTimer;
	IntervalTimer m_SpecialWarningTimer[9];
	int m_LastWarnedActorID[9];
	CountdownTimer m_LookOutTimer;
	CountdownTimer m_HeardZombieTimer;
	CountdownTimer m_GoingToDieTimer;
	CountdownTimer m_RescueVehicleTimer;
	EHANDLE m_LastWarnedSoundOwner;
	EHANDLE m_BlockingPlayer;
	EHANDLE m_BlockingEntity;
	IntervalTimer m_NoPlayerBlockTimer;
	IntervalTimer m_NoEntityBlockTimer;
	CountdownTimer m_BlockedSpeechTimer;
	EHANDLE m_LookTarget;
	CHandle< CTerrorPlayer > m_ItemTarget;
	bool m_HasJoinedScenario;
	bool m_HasAnnouncedNearbyGoal;
	const char *m_PendingGoalConcept;
};

#endif // SURVIVOR_SPEECH_H
