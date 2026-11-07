//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SURVIVOR_BOT_H
#define L4D2_SURVIVOR_BOT_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/terrorplayer.h"
#include "NextBot/Player/NextBotPlayer.h"
#include "l4d2/player/bots/survivor_body.h"
#include "l4d2/player/bots/survivor_locomotion.h"
#include "l4d2/player/bots/survivor_vision.h"
#include "l4d2/player/bots/survivor_intention.h"
#include "l4d2/player/bots/survivor_team_situation.h"
#include "NextBot/Path/NextBotPathFollow.h"

class SurvivorReplacementState;

class SurvivorBot : public NextBotPlayer< CTerrorPlayer >
{
public:
	DECLARE_CLASS( SurvivorBot, NextBotPlayer< CTerrorPlayer > );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

	SurvivorBot();
	virtual ~SurvivorBot();
	static void CC_sb_move_to_cursor();
	static CBasePlayer *AllocatePlayerEntity( edict_t *edict, const char *name );
	virtual SurvivorBot *MySurvivorBotPointer() const OVERRIDE;
	virtual void OnPlayerDisconnected( CTerrorPlayer *player ) OVERRIDE;
	virtual bool IsEnemy( const CBaseEntity *subject ) const OVERRIDE;
	virtual const char *GetDebugIdentifier() const OVERRIDE;
	virtual SurvivorLocomotion *GetLocomotionInterface() const OVERRIDE;
	virtual SurvivorBody *GetBodyInterface() const OVERRIDE;
	virtual SurvivorIntention *GetIntentionInterface() const OVERRIDE;
	virtual SurvivorVision *GetVisionInterface() const OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void PhysicsSimulate() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void OnNavAreaChanged( CNavArea *enteredArea, CNavArea *leftArea ) OVERRIDE;
	virtual void OnTeamChanged( int oldTeam ) OVERRIDE;
	virtual bool OnAwardEarned( int award, CBaseEntity *subject ) OVERRIDE;
	virtual void SaveForTransition( const char *targetMap ) OVERRIDE;
	virtual bool Weapon_Switch( CBaseCombatWeapon *weapon, int viewModelIndex = 0 ) OVERRIDE;

	bool SetHumanSpectator( CTerrorPlayer *human );
	bool SetHumanSpectator( int userID, const char *name );
	void ClearHumanSpectator();
	int GetHumanSpectatorUserID() const { return m_humanSpectatorUserID; }
	int GetHumanSpectatorEntIndex() const { return m_humanSpectatorEntIndex; }
	bool HasConnectedHumanSpectator() const;
	bool UpdateTransitionReservation();
	bool UpdatePerformanceCapture();

	SurvivorTeamSituation &GetTeamSituation() const;
	bool CanReachArea( CNavArea *from, CNavArea *to );
	bool CanReachSurvivor( CTerrorPlayer *player );
	bool IsReachable( CBaseEntity *entity );
	bool IsImmediateThreat( CBaseCombatCharacter *threat ) const;
	bool IsLowOnAmmo() const { return m_isLowOnAmmo; }
	const SurvivorKnownEntity *GetPrimaryThreat() const;
	bool IsNearCheckpoint() const;

	int GetDesiredPace() const;
	void ResolveStuckSituation( PathFollower *path );
	void UpdateEscapePath();
	PathFollower &GetEscapePath() { return m_escapePath; }
	void EnforceHumanProximity();
	bool IsRushing() const;
	bool ShouldMoveEvenIfRushing() const;
	bool IsFriendNearby( CTerrorPlayer *friendPlayer ) const;
	bool ShouldRespondToRescueVehicle() const;
	float GetThreatDangerRange( CBaseEntity *threat ) const;
	bool IsThreatWithinDangerRange( CBaseEntity *threat, float range ) const;

	void ReloadCombatWeapon();
	bool IsSafeForCombatAction() const;
	void LookAround();
	const IntervalTimer &GetWeaponSwitchAttemptTimer() const { return m_weaponSwitchAttemptTimer; }
	void SuppressFlashlight( float duration );
	void UpdateAreaClear();
	void SpeakConcept( const char *concept, int priority, float delay, CBaseEntity *target );
	void UpdatePendingSpeech();
	void UpdateHumanAttention();
	CountdownTimer &GetFriendActionTimer( SurvivorCharacterType character ) { return m_friendActionTimer[ static_cast< int >( character ) ]; }
	void NoteFriendDislodgeAttempt( SurvivorCharacterType character, float duration = 10.0f );
	ActionResult< SurvivorBot > TryHealFriends();
	ActionResult< SurvivorBot > TryReviveFriends();
	ActionResult< SurvivorBot > TryScavenge();
	ActionResult< SurvivorBot > TryUseObjects();
	ActionResult< SurvivorBot > TrySaveFriendsInImmediateTrouble();
	ActionResult< SurvivorBot > TryScavengeForL4D1Survivor();
	ActionResult< SurvivorBot > TryUseMinigun();

private:
	friend class SurvivorReplacementState;
	friend class SurvivorBehavior;
	friend class SurvivorLegsStayClose;
	friend class SurvivorCheckpointLeaving;
	SurvivorLocomotion *m_locomotor;
	SurvivorBody *m_body;
	SurvivorIntention *m_intention;
	SurvivorVision *m_vision;
	PathFollower m_movementPath;
	mutable SurvivorTeamSituation m_teamSituation;
	PathFollower m_escapePath;

	CNetworkVar( int, m_humanSpectatorUserID );
	CNetworkVar( int, m_humanSpectatorEntIndex );
	char m_humanSpectatorName[32];
	CNetworkVar( int, m_humanLoadingProgress ); // native17228, NOT an admission/userID alias
	bool m_isHumanTransitioning;
	bool m_hasHumanTransitionReservation;
	IntervalTimer m_creationTimer;
	bool m_isLowOnAmmo;
	bool m_resetIntention;
	bool m_alternateForcedFire;

	CountdownTimer m_debugMoveTimer;
	Vector m_debugMoveGoal;
	CountdownTimer m_lookAroundTimer;
	IntervalTimer m_lookAroundInterval;
	CountdownTimer m_witchFlashlightTimer;
	CUtlVector< CNavArea * > m_unclearedAreas;
	TerrorNavArea *m_farthestVisibleArea;
	TerrorNavArea *m_nearestVisibleArea;
	CountdownTimer m_escapePathTimer;
	bool m_isInNarrowCorridor;
	bool m_isReadyToLeaveCheckpoint;

	CountdownTimer m_friendActionTimer[8];
	CountdownTimer m_scavengeTimer;
	CountdownTimer m_useObjectTimer;
	CountdownTimer m_minigunTimer;
	CountdownTimer m_healFriendTimer;
	IntervalTimer m_weaponSwitchAttemptTimer;
	CountdownTimer m_attentionTimer;
	CHandle< CTerrorPlayer > m_attentionSubject;
	IntervalTimer m_attentionSubjectTimer;
	IntervalTimer m_humanProximityLookAtTimer;
	struct SpeechHistoryEntry
	{
		const char *concept;
		float time;
	};
	const char *m_pendingSpeechConcept;
	int m_pendingSpeechPriority;
	CountdownTimer m_pendingSpeechTimer;
	EHANDLE m_pendingSpeechTarget;
	SpeechHistoryEntry m_speechHistory[8];
	int m_speechHistoryIndex;
	int m_speechHistoryCount;
};

CTerrorPlayer *FindSurvivorByCharacter( SurvivorCharacterType character );
CTerrorPlayer *FindClosestSurvivorPlayer( const Vector &position, bool includeImmobile, bool excludeRescueVehicle );
int GetTerrorEntityZombieClass( CBaseEntity *entity );
const char *GetSurvivorBotSubjectName( CBaseEntity *entity );
bool IsSurvivorBehind( CTerrorPlayer *first, CTerrorPlayer *second );
bool AreSurvivorsInDistress();
bool IsNearLivingWitch( SurvivorBot *me, float range );
bool IsSafeToPopBoomer( CTerrorPlayer *boomer );

#endif // L4D2_SURVIVOR_BOT_H
