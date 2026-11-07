//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef CS_PLAYER_H
#define CS_PLAYER_H
#ifdef _WIN32
#pragma once
#endif

#include "basemultiplayerplayer.h"
#include "l4d2/cs_shareddefs.h"
#include "utldict.h"
#include "Multiplayer/multiplayer_animstate.h"
// Actual CUtlLinkedList definition; reused also by derived dragpath
#include "tier1/utllinkedlist.h"

// Existing actual queue type; pointerfield9112, not new queue
class CHintMessageQueue;

class CCSPlayer;

// Native 248F10 table rows: state, name, enter, leave, preThink.
// Leave receives the NEXT state (the native leave thunks take it; most discard it).
struct CCSPlayerStateInfo
{
	CSPlayerState state;
	const char *name;
	void ( CCSPlayer::*enter )();
	void ( CCSPlayer::*leave )( CSPlayerState nextState );
	void ( CCSPlayer::*preThink )();
};

struct TerrorDamageRecord_t
{
	char name[32];       // +0  V_strncpy( name, source, 32 ); match is V_strnicmp( source, name, 32 )
	int damage;          // +32 new record = damage; a repeat ALWAYS adds damage
	int hits;            // +36 new record = 1; a repeat adds 1 only when bulletSerial differs
	uint32 bulletSerial; // +40 GetTerrorBulletSerial() at the last add (native global 10837750)
};
class CCSPlayer : public CBaseMultiplayerPlayer
{
public:
	DECLARE_CLASS( CCSPlayer, CBaseMultiplayerPlayer );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CCSPlayer();
	virtual ~CCSPlayer();
	bool HasShield() const { return false; } // Genuine S23FA30, no shield field.
	bool IsShieldDrawn() const { return false; } // Genuine S23FA40.
	void SetShieldDrawn( bool drawn ) {} // Genuine native nullsub17.
	virtual void OnTerrorReloadStarted( bool shotgun, int oldClip, bool partial ) {} // Genuine three-arg S249D40; Terror307DF0 overrides.
	virtual bool IsGivingNamedItem() const { return m_bGivingNamedItem; } // Native509/S249CF0.
	virtual void InitialSpawn();
	virtual void Spawn();
	virtual void Precache() OVERRIDE;
	virtual bool ClientCommand( const CCommand &args );
	virtual void PlayerRunCommand( CUserCmd *pCommand, IMoveHelper *pMoveHelper );
	virtual void PostThink();
	// S24B800/S24B810 are genuine CCS RET leaves, inherited by Terror.
	// Do not run SDK movement/rung timers or emit a second footstep sound.
	virtual void UpdateStepSound( surfacedata_t *, const Vector &, const Vector & ) OVERRIDE {}
	virtual void PlayStepSound( Vector &, surfacedata_t *, float, bool ) OVERRIDE {}
	virtual void HandleAnimEvent( animevent_t *event ) OVERRIDE;
	virtual surfacedata_t *GetTerrorFootstepSurface( int &surfaceIndex ) OVERRIDE;
	virtual bool IsTerrorFootstepSurfaceValid( float speed, bool force ) const OVERRIDE;
	virtual void OnDamagedByExplosion( const CTakeDamageInfo &info ) OVERRIDE;
	void UpdateExplosionDSP();
	virtual void SelectDeathPose( const CTakeDamageInfo &info );
	float GetLastDamageTime() const { return m_lastDamageTime; }
	const CTakeDamageInfo &GetLastDamageInfo() const { return m_lastDamageInfo; }
	virtual int FlashlightIsOn() OVERRIDE;
	virtual bool FlashlightTurnOn( bool playSound = true ) OVERRIDE;
	virtual void FlashlightTurnOff( bool playSound = true ) OVERRIDE;
	virtual bool CanUseFlashlight() const { return true; } // S249E20.
	virtual void ChangeTeam( int iTeam );
	virtual void PlayerDeathThink();
	virtual void SetAnimation( PLAYER_ANIM playerAnim );
	virtual bool StartReplayMode( float delay, float duration, int entity ) OVERRIDE;
	virtual void StopReplayMode() OVERRIDE;

	CSPlayerState State_Get() const;
	virtual bool IsBusy() const;
	virtual bool IsProgressBarActive() const;
	void SetProgressBarTime( float duration );
	void SetProgressBarStartTime( float startTime );
	void State_Transition( CSPlayerState state );
	virtual bool HandleCommand_JoinTeam( int iTeam, int iCharacter, bool bForce = false );
	virtual bool ShouldAutoSelectTeam();
	virtual void OnTeamChanged( int iOldTeam ) = 0;
	void ShowMotD();
	virtual bool ShouldShowMotD() { return true; } // S249D00 genuine slot522 constant.
	void NoteTerrorWeaponFired();
	int GetLastTerrorWeaponFireCommand() const { return m_iLastWeaponFireUsercmd; }
	void RecordLastStandingInfo( const Vector &position, const Vector &velocity, const QAngle &angles );
	void RecordLadderInfo( const Vector &position, const Vector &normal, const Vector &velocity, const QAngle &angles );
	double GetTimeSinceLadderInteraction() const;
	bool CanRegrabLadder( const Vector &position, const Vector &normal ) const;
	const Vector &GetLastLadderNormal() const { return m_lastLadderNormal; }
	void RecordDamageGiven( const char *name, int damage );
	virtual void RecordDamageTaken( const char *name, int damage );
	friend class CCSGameMovement;
	friend class CTerrorGameMovement;

	float GetStamina() const { return m_flStamina; }
	void SetStamina( float flStamina ) { m_flStamina = flStamina; }
	float GetVelocityModifier() const { return m_flVelocityModifier; }
	void SetVelocityModifier( float flModifier ) { m_flVelocityModifier = flModifier; }
	bool IsDuckUntilOnGround() const { return m_duckUntilOnGround; }
	void SetDuckUntilOnGround( bool bDuck ) { m_duckUntilOnGround = bDuck; }

	CNetworkVar( int, m_iPlayerState );
protected:
	IntervalTimer m_stateEnterTimer;
public:
	EHANDLE m_hIntroCamera;
	CNetworkHandle( CBaseEntity, m_hRagdoll );
protected:
	IntervalTimer m_lastStandingTimer;
	IntervalTimer m_ladderInteractionTimer;
public:
	CTakeDamageInfo m_lastDamageInfo;
	CUtlLinkedList< TerrorDamageRecord_t *, int > m_damageGiven;
	CUtlLinkedList< TerrorDamageRecord_t *, int > m_damageTaken;
protected:
	CUtlDict< float, int > m_CommandRateLimit;
public:
	CountdownTimer m_cycleLatchTimer;
	CNetworkQAngle( m_angEyeAngles );
	CNetworkVar( float, m_fServerAnimStartTime );
	CNetworkArray( int, m_NetGestureSequence, GESTURE_SLOT_COUNT );
	CNetworkArray( int, m_NetGestureActivity, GESTURE_SLOT_COUNT );
	CNetworkArray( float, m_NetGestureStartTime, GESTURE_SLOT_COUNT );
	CNetworkVar( float, m_flStamina );
	CNetworkVar( int, m_iDirection );
	CNetworkVar( int, m_iShotsFired );
	bool m_bResumeZoom; // S9188: actual CS Deploy state, no wire/save/constructor reset.
	int m_iLastZoom; // S9192.
	CNetworkVar( float, m_flVelocityModifier );
	CNetworkVar( bool, m_duckUntilOnGround );
	CNetworkVar( float, m_flProgressBarStartTime );
	CNetworkVar( float, m_flProgressBarDuration );

	// S:0x24A020 stages these per-hit integers before lower damage dispatch.
	int m_iDamageHealth;
	int m_iDamageArmor;

	float m_flLastActivityTime;

	QAngle m_lastActivityAngles;



	float m_lastDamageTime;


	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;

	virtual void PreThink() OVERRIDE;

	void MoveToNextIntroCamera();

	void HintMessage( const char *message, bool displayIfDead, bool overrideClientSettings, bool clearQueue );

	bool HandleCommand_JoinClass( int playerClass );

	virtual void RecalculateMaxSpeed();

	virtual bool GetIntoGame();

	void ClearFlashbangScreenFade();
	virtual void Blind( float holdTime, float fadeTime, float startingAlpha );

	void RestoreViewForRoundStart();

	virtual void RoundRespawn();

	void ObserverRoundRespawn();

	void StockPlayerAmmo( CBaseCombatWeapon *newWeapon );

	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;

	void PrintDamageGiven();

	void PrintDamageTaken();

	virtual void CreateRagdollEntity( const CTakeDamageInfo &info );

	virtual void OnLeaveActive(); // S246D90; genuine slot524, not an empty base.

	virtual void OnPreThinkObserverMode() {} // S249D60 genuine RETN.

	virtual void OnEnterWaitingForRescue() {} // S249D70 genuine RETN.

	virtual void OnPreThinkWaitingForRescue() {} // S249D80 genuine RETN.

	virtual void OnEnterGhost() {} // S249DA0 genuine RETN.

	virtual void OnPreThinkGhost() {} // S249DB0 genuine RETN.

	virtual void OnLeaveGhost() {} // S249DC0 genuine RETN.

	virtual void OnEnterIntroCamera() {} // S249DD0 genuine RETN.

	virtual void OnPreThinkIntroCamera() {} // S249DE0 genuine RETN.

	virtual void OnLeaveIntroCamera() {} // S249DF0 genuine RETN.

	virtual void OnLeaveDeathAnim() {} // S249E00 genuine RETN.

	virtual void OnLeaveDeathWaitForKey() {} // S249E10 genuine RETN.

	// EXACT PROVIDED native constant249E30 inline; no invented fallback/base body.
	virtual int GetRagdollType() const { return 1; }

	// Main explicitly authorizes pure CCS interface; never add empty base body or a compatibility overload.
	virtual void DropWeapons( bool dead ) = 0;

	void EmitPrivateSound( const char *sound );

	CNetworkVar( int, m_iAccount );
	CNetworkVar( bool, m_bInBombZone );
	CNetworkVar( bool, m_bInBuyZone );
	CNetworkVar( bool, m_bInHostageRescueZone );
	CNetworkVar( int, m_iThrowGrenadeCounter );
	CNetworkVar( int, m_iNightVisionScale );


	CNetworkVar( bool, m_bHasHelmet );

	CNetworkVar( bool, m_bHasDefuser );

	CNetworkVar( bool, m_bNightVisionOn );

	CNetworkVar( bool, m_bHasNightVision );

	float m_blindUntilTime;

	float m_blindStartTime;

	CNetworkVar( float, m_flFlashDuration );

	CNetworkVar( float, m_flFlashMaxAlpha );

	int m_iDeathPose;

	int m_iDeathFrame;

	Vector m_vecTotalBulletForce;

	virtual bool CanChangeName();

	virtual void ChangeName( const char *name );

	int GetNumSpawns() const { return m_iNumSpawns; }

	void ResetNumSpawns() { m_iNumSpawns = 0; }

	// Keep sole current target helper; Main PreThink invokes it once after base. Parent explicitly corrected earlier standalone-RVA wording.
	void UpdateLastActivity();

	// EXACT PROVIDED inline accessor; no new clock/provider.
	float GetLastActivityTime() const { return m_flLastActivityTime; }

	// NO SECOND ARMOR FIELD. Reuse exact current m_iMaxHealth derived-notification pattern in terrorplayer.h; macro only hooks the already inherited field.
	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_ArmorValue );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	char m_szNewName[MAX_PLAYER_NAME_LENGTH];

	// Retain existing D name, migrate Main; no alias or second time field.
	float m_explosionDSPTime;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_bDeathCleanup;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CNetworkVar( bool, m_bIsDefusing );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_bShowHints;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_bTeamChanged;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CNetworkVar( int, m_cycleLatch );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_flLastRadarUpdateTime;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_flNextIntroCameraTime;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_flRadioTime;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CNetworkVar( int, m_iClass );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_iDisplayHistoryBits;
	float m_lastFriendlyAttackAnnouncement; // native9124, allocation-zero omission; no ctor/Spawn store

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_iOldTeam;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_iRadioMessages;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_nativeCSSpeedEquipmentFlag;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CHintMessageQueue *m_pHintMessageQueue;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_receivesMoneyNextRound;

	// Use frozen EB4C whole existing-owner payload; no duplicate animation update.
	virtual void UpdateAddonBits();

	CNetworkVar( int, m_iAddonBits );

	CNetworkArray( int, m_AddonModelWeaponIDs, 5 );

	int m_explosionDSP;
	bool m_hasEscaped;
	bool m_isVIP;
	int m_teamKillCount;
	int m_nextMapQueryTime;
	bool m_ignoreRadioMessages;
protected:
	CMultiPlayerAnimState *m_PlayerAnimState; // Native10524; Terror constructs, CCS releases.
	bool m_bGivingNamedItem;
	// TWO genuine nativevirtuals: keep this AND ApplySpawnModel554. Main latest nativeproof supersedes earlier single-interface interpretation. No collapse/removal.
	virtual void SetModelFromClass();
	bool ShouldRunRateLimitedCommand( const CCommand &args );
	void State_Enter( CSPlayerState state );
	void State_Enter_ACTIVE();
	void State_Enter_WELCOME();
	void State_Enter_PICKINGTEAM();
	void State_Enter_OBSERVER_MODE();
	Vector m_lastStandingPosition;
	Vector m_lastStandingVelocity;
	QAngle m_lastStandingAngles;
	CNetworkVector( m_lastLadderNormal );
	Vector m_lastLadderPosition;
	Vector m_lastLadderVelocity;
	QAngle m_lastLadderAngles;
	const CCSPlayerStateInfo *m_pStateInfo;

	static const CCSPlayerStateInfo *State_LookupInfo( CSPlayerState state );

	void State_Leave( CSPlayerState nextState );

	void State_Leave_ACTIVE( CSPlayerState nextState );

	void State_PreThink_ACTIVE();

	void State_Enter_DEATH_ANIM();

	void State_PreThink_DEATH_ANIM();

	void State_Leave_DEATH_ANIM( CSPlayerState nextState );

	void State_Enter_DEATH_WAIT_FOR_KEY();

	void State_Leave_DEATH_WAIT_FOR_KEY( CSPlayerState nextState );

	void State_PreThink_DEATH_WAIT_FOR_KEY();

	void State_PreThink_WELCOME();

	void State_PreThink_OBSERVER_MODE();

	void State_Enter_WAITING_FOR_RESCUE();

	void State_PreThink_WAITING_FOR_RESCUE();

	void State_Enter_GHOST();

	void State_PreThink_GHOST();

	void State_Leave_GHOST( CSPlayerState nextState );

	void State_Enter_INTRO_CAMERA();

	void State_PreThink_INTRO_CAMERA();

	void State_Leave_INTRO_CAMERA( CSPlayerState nextState );

	void State_Enter_PICKINGCLASS();

	// BothGENUINEmodelvirtuals retained. Not a compatibilityalias: distinct nativeentries/callbackgates. No bodycollapse.
	virtual void ApplySpawnModel();

	void PushawayThink();
	void RescueZoneTouch( inputdata_t &inputdata );

	float m_nameChangeHistory[5];

	// Exact Main typed field/ctor/lifetime freeze. Z is allocation-only omission; no extra initializer or transport.
	int m_iNumSpawns;
private:
	int m_iLastWeaponFireUsercmd;
};


inline CCSPlayer *ToCSPlayer( CBaseEntity *pEntity )
{
	return pEntity && pEntity->IsPlayer() ? static_cast< CCSPlayer * >( pEntity ) : NULL;
}

#endif // CS_PLAYER_H
