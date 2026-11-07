//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERRORPLAYER_H
#define TERRORPLAYER_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/cs_player.h"
#include "l4d2/terror_shareddefs.h"
#include "util_shared.h"
#include "takedamageinfo.h"
// Existing actual Music definition for embedded member
#include "l4d2/sound/terror_music.h"
#include "steam/steam_api_common.h"
#include "steam/isteamgameserver.h"

class CTerrorWeapon;
class CTerrorWeaponInfo;
class CTerrorPlayerAnimState;
class CNavArea;
class CSurvivorDeathModel;
class KeyValues;
class CBaseAbility;
class CSurvivorPosition;
class CBaseMountedWeapon;
class CTerrorCheckpoint;

// ActualM4navtype; pointer/vector fields only
class TerrorNavArea;
// Existing typed ThrowWeapon/StoreWeaponReserveForDrop interface
class CWeaponCSBase;
// S29C910/+12020 has no own vtable or RTTI; this is a descriptive type name.
class CSurvivorIntensity
{
public:
	CSurvivorIntensity();
	void Reset();
	void Add( int severity );
	void Hold( float duration );
	void Update();
	float GetCurrent() const { return m_current; }
	float GetTimeAveraged() const { return m_timeAveraged; }

private:
	float m_current;
	float m_timeAveraged;
	IntervalTimer m_updateTimer;
	CountdownTimer m_holdTimer;
};

// Native2D1830's real stack census; the reached traversals contain only players.
// One actual producer replaces the old selector-only specialization and serves
// the Director/Survival diagnostics without a new persistent count/cache.
struct TerrorInfectedClassCounts
{
	explicit TerrorInfectedClassCounts( bool includeDeadPlayers );
	void Count( int team = -1 );
	int tanks;
	int witches;
	int commons;
	int special[6];
	int living[6];
	bool includeDead;
};

class Witch;
class CSurvivorSpeech;
const char *GetResponseCharacterName( int character );
const char *GetResponseCampaignName();
extern IResponseSystem *g_pTerrorPlayerResponseSystem; // Native1085BB78, borrowed from the response-system dictionary.
class CTerrorPlayer : public CCSPlayer
{
public:
	DECLARE_CLASS( CTerrorPlayer, CCSPlayer );
	friend class CTerrorGameMovement;
	friend class CTongue;
	friend class SurvivorReplacementState;
	friend class InfectedReplacementState;
	friend class CSurvivorRescue;
	friend class CSurvivorSpeech;
	friend class CDirector;
	friend class CTerrorGameRules;
	friend class CGnome;
	friend class CTerrorPlayerResource;
	friend class EnsureTeamSize;
	friend class SurvivorBot;
	friend bool SpawnZombie( int zombieClass, Vector position, QAngle angles, int wound, bool ragdoll, bool inArea );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	DECLARE_ENT_SCRIPTDESC();
private:
	struct CommonDamageSample_t { float damage; float time; };
	struct TrackedSound { const char *m_sound; float m_deadline; };
public:
	CNetworkHandle( CBaseEntity, m_carryVictim );
	CNetworkHandle( CBaseEntity, m_carryAttacker );
private:
	CCallback< CTerrorPlayer, GSClientAchievementStatus_t, true > m_GSClientAchievementStatusCallback;
	void OnGSClientAchievementStatus( GSClientAchievementStatus_t * ) {}
public:
	// Native S2FE490 reads these allocation-zero legacy fields. No game writer.
	CNetworkVar( float, m_mainSequenceStartTime );
	CNetworkVar( int, m_grenadeLayerSequence );
	CNetworkVar( int, m_fireLayerSequence );
	CNetworkVar( int, m_flinchLayerSequence );
	CNetworkVar( int, m_fidgetLayerSequence );
	CNetworkVar( int, m_reloadLayerSequence );
	CNetworkVar( int, m_reloadAltLayerSequence );
	CNetworkVar( int, m_reloadLayerNumShells );
	CNetworkVar( float, m_grenadeLayerStartTime );
	CNetworkVar( float, m_fireLayerStartTime );
	CNetworkVar( float, m_flinchLayerStartTime );
	CNetworkVar( float, m_fidgetLayerStartTime );
	CNetworkVar( float, m_reloadLayerStartTime );
	CNetworkVar( float, m_reloadAltLayerStartTime );
	CNetworkHandle( CBaseEntity, m_hBuildableButtonUseEnt );
	CNetworkHandle( CBaseEntity, m_useActionTarget );
	CNetworkHandle( CBaseEntity, m_useActionOwner );
private:
	CUtlVector< TrackedSound > m_trackedSounds;
public:
	CNetworkHandle( CBaseEntity, m_vocalizationSubject );
	CNetworkVarEmbedded( CountdownTimer, m_vocalizationSubjectTimer );
private:
	CountdownTimer m_nameChangeTimer;
	EHANDLE m_hLastPickupObject;
public:
	CountdownTimer m_tankBumpTimer;
private:
	CountdownTimer m_scrimmageTimer;
	CountdownTimer m_ghostSpawnClockTimer;
	CountdownTimer m_ghostSpawnBotTimer;
	EHANDLE m_hTugSource;
public:
	CNetworkHandle( CBaseEntity, m_dragTarget );
	CHandle< CTerrorPlayer > m_dragOwner;
	CUtlLinkedList< Vector, unsigned short > m_dragPath;
private:
	CountdownTimer m_incapacitatedTimer;
	EHANDLE m_incapacitatedByAttacker;
	IntervalTimer m_incapacitatedTimestamp;
	CountdownTimer m_incapacitationRecoveryTimer;
	CTakeDamageInfo m_deathInfo;
public:
	CNetworkHandle( CBaseEntity, m_reviveOwner );
	CNetworkHandle( CBaseEntity, m_reviveTarget );
	CountdownTimer m_reviveTimer;
private:
	CountdownTimer m_invulnerabilityTimer;
public:
	CNetworkVarEmbedded( CountdownTimer, m_noAvoidanceTimer );
private:
	CountdownTimer m_calmWeaponTimer;
	CountdownTimer m_zombieStateUpdateTimer;
	IntervalTimer m_zombieStateTimer;
	CountdownTimer m_survivorDistressResponseTimer;
	CountdownTimer m_musicMobScanTimer;
	CountdownTimer m_enemySightCheckTimer;
	IntervalTimer m_commonRunningTimer;
	CountdownTimer m_runExertionTimer;
public:
	CSurvivorIntensity m_survivorIntensity;
	CNetworkVarEmbedded( IntervalTimer, m_autoCrouchTimer );
private:
	CountdownTimer m_friendSightCheckTimer;
	IntervalTimer m_friendSeenTimer[8];
	IntervalTimer m_friendNearbyInterval;
	CountdownTimer m_coughTimer;
public:
	CountdownTimer m_infectedAmbientVocalizeTimer;
private:
	CountdownTimer m_vocalizeTimer;
	CountdownTimer m_infectedPainTimer;
public:
	CNetworkHandle( CBaseAbility, m_customAbility );
private:
	IntervalTimer m_occupiedNavAreaInterval;
	CUtlVector<TerrorNavArea *> m_potentiallyVisibleAreas;
	IntervalTimer m_navAreaChangedInterval;
	IntervalTimer m_seenByEnemyInterval;
	IntervalTimer m_enemySeenInterval;
	IntervalTimer m_visibleEnemyInterval;
	CUtlVector<CHandle<CBaseCombatCharacter> > m_visibleEnemies;
	IntervalTimer m_terrorFireInterval;
public:
	IntervalTimer m_terrorShoveInterval;
private:
	IntervalTimer m_damageTimer;
	IntervalTimer m_burnDamageTimer;
public:
	IntervalTimer m_terrorInjuryInterval;
private:
	IntervalTimer m_positionStillTimer;
	IntervalTimer m_viewStillTimer;
	Vector m_infectedAlertPosition; // Native12764, real EyePosition snapshot; no constructor/reset store.
	CountdownTimer m_staggerSourceTimer;
	EHANDLE m_hStaggerSource;
	CountdownTimer m_damageStaggerTimer;
public:
	CNetworkVarEmbedded( CountdownTimer, m_staggerTimer );
	CNetworkVarEmbedded( CountdownTimer, m_tugTimer );
private:
	CountdownTimer m_vomitDamageSourceTimer;
	EHANDLE m_hVomitDamageSource;
public:
	CNetworkVarEmbedded( CountdownTimer, m_itTimer );
private:
	EHANDLE m_hITSource;
	CountdownTimer m_adrenalineTimer;
	CountdownTimer m_coughSourceTimer;
	EHANDLE m_coughSource;
	CountdownTimer m_tongueReleaseTimer;
	EHANDLE m_hTongueReleaseOwner;
	CountdownTimer m_launchSourceTimer;
	EHANDLE m_hLaunchSource;
	EHANDLE m_burnAttacker;
	CountdownTimer m_cullingTimer;
public:
	CNetworkVarEmbedded( CountdownTimer, m_stunTimer );
private:
	CountdownTimer m_bashImmunityTimer;
	CountdownTimer m_delayedDSPTimer;
	CountdownTimer m_reloadChatterTimer;
	CountdownTimer m_friendlyFireResponseTimer;
	CountdownTimer m_painTimer;
	CountdownTimer m_tongueVoiceTimer; // Native13144; distinct from pull/choke timers.
	CUtlVector< float > m_staggerHistory;
public:
	CNetworkHandle( CBaseEntity, m_positionEntity );
	CNetworkHandle( CBaseEntity, m_pushEntity );
	IntervalTimer m_useEntityChangedTimer;
	CNetworkHandle( CBaseEntity, m_tongueVictim );
	CNetworkHandle( CBaseEntity, m_tongueOwner );
	CNetworkVarEmbedded( IntervalTimer, m_tongueVictimTimer );
	CNetworkVector( m_initialTonguePullDir );
private:
	CountdownTimer m_tonguePullTimer;
	IntervalTimer m_tongueChokeStartTimer;
	CountdownTimer m_tongueChokeTimer;
public:
	CNetworkVarEmbedded( IntervalTimer, m_hangTimer );
private:
	EHANDLE m_hangSurface;
	CountdownTimer m_ledgeGroundCheckTimer;
	CountdownTimer m_frustrationGraceTimer;
	IntervalTimer m_commonDamageTimer;
	CUtlVector< CommonDamageSample_t > m_commonDamageHistory;
public:
	// Music owns dynamic state and is deliberately noncopyable. Keep the
	// ordinary embedded change forwarding without generating CopyFrom.
	class NetworkVar_m_music;
	friend class NetworkVar_m_music;
	static inline int GetOffset_m_music() { return MyOffsetOf( ThisClass, m_music ); }
	typedef ThisClass ThisClass_m_music;
	class NetworkVar_m_music : public Music
	{
	public:
		virtual void NetworkStateChanged()
		{
			DispatchNetworkStateChanged( ( ThisClass_m_music * )( ( ( char * )this ) - GetOffset_m_music() ) );
		}
		virtual void NetworkStateChanged( void *pVar )
		{
			DispatchNetworkStateChanged( ( ThisClass_m_music * )( ( ( char * )this ) - GetOffset_m_music() ), pVar );
		}
	};
	NetworkVar_m_music m_music;
private:
	CNetworkVarEmbedded( CountdownTimer, m_downAndBeatenTimer );
public:
	CNetworkHandle( CBaseEntity, m_pummelVictim );
	CNetworkHandle( CBaseEntity, m_pummelAttacker );
private:
	EHANDLE m_pummelTarget;
	float m_pummelTargetTime; // Native15972; SetPummelTarget writes -1 for stagger completion.
	EHANDLE m_pummelObject;
	CountdownTimer m_defibrillatorReturnToLifeTimer;
public:
	CNetworkHandle( CBaseEntity, m_pounceVictim );
	CNetworkHandle( CBaseEntity, m_pounceAttacker );
private:
	CountdownTimer m_pounceDamageTimer;
	bool m_pouncePainAlternation; // native16032, allocation-zero omission; no pounce/Spawn reset
	CountdownTimer m_pounceGrowlTimer;
	IntervalTimer m_pounceStartTimer;
	IntervalTimer m_lungeTimer;
	IntervalTimer m_pounceSoundTimer;
	CountdownTimer m_jockeyDamageTimer;
	EHANDLE m_pendingPounceAttacker;
public:
	CNetworkHandle( CBaseEntity, m_jockeyVictim );
	CNetworkHandle( CBaseEntity, m_jockeyAttacker );
private:
	EHANDLE m_hInstancedScene;
	CountdownTimer m_jockeyMountTimer;
public:
	CNetworkVarEmbedded( IntervalTimer, m_knockdownTimer );
	CHandle< CTerrorPlayer > m_MVPNominee;
private:
	CountdownTimer m_physicsDamageImmunityTimer;
	CountdownTimer m_commonHearingTimer;
	CountdownTimer m_tankFootstepShakeTimer;
	CountdownTimer m_helpTimer;
	CountdownTimer m_spawnedAsTankTimer;
	CHandle<CBaseCombatWeapon> m_hHiddenWeapon;
	CountdownTimer m_idleTimer;
public:
	CNetworkHandle( CBaseEntity, m_lookatPlayer );
	CHandle< Witch > m_witch;

	CTerrorPlayer();
	virtual ~CTerrorPlayer();
	int GetSenseFlags() const;
	void SetSenseFlags( int flags );
	// Server native script callbacks (S330690), with gameplay owners reused.
	bool IsOnASurvivorTeam() const;
	void ScriptGivePrimaryAmmo( int count );
	void ScriptGiveNamedItem( const char *name );
	void ScriptGiveNamedItemWithSkin( const char *name, int skin );
	void ScriptAddUpgrade( int upgrade );
	void ScriptRemoveUpgrade( int upgrade );
	int ScriptGetButtons() const;
	int GetSurvivorSlot() const;
	int ScriptGetPlayerUserId() const;
	HSCRIPT ScriptGetActiveWeapon() const;
	void ScriptReviveByDefibrillator();
	int ScriptGetZombieType() const;
	bool IsDying() const;
	void ScriptHitWithVomit();
	// Declaration only: existing implementation is terrorplayer_vomit.cpp.
	void OnVomitedUpon( CTerrorPlayer *attacker, bool exploded );
	Vector TryGetPathableLocationWithin( float radius );
	void ScriptStaggerPlayer( Vector sourcePosition );
	void ScriptDropItem( const char *name );
	bool ScriptSwitchToItem( const char *name );
	bool SuppressFallingDamage() const;
	bool ScriptIsInCombat() const;
	HSCRIPT ScriptGetSpecialInfectedDominatingMe() const;
	bool IsFiringWeapon() const;
	const Vector &GetInfectedAlertPosition(); // Native419700.
	bool IsCommentaryStatue() const { return m_isCommentaryStatue; }
	void InputSetCommentaryStatueMode( inputdata_t &inputdata );
	virtual void AvoidPlayers( CUserCmd *command );
	static CTerrorPlayer *CreatePlayer( const char *pClassName, edict_t *pEdict );
	virtual void Precache();
	virtual void InitialSpawn();
	virtual void Spawn();
	virtual void ItemPostFrame() OVERRIDE;
	void OnShove(); // S32AFA0, ordinary shove callback.
	void OnLeftStartArea(); // Existing S303970 body.
	virtual void RemoveAmmo( int count, int ammoIndex ) OVERRIDE;
	using BaseClass::RemoveAmmo;
	virtual void PostThink();
	virtual void UpdateTonemapController() OVERRIDE;
	virtual void Extinguish() OVERRIDE;
	virtual void OnRevived();
	void PrepareLedgeGrab( const Vector &hangPosition, const Vector &standPosition, const Vector &fallPosition,
		const Vector &normal, const Vector &climbEndPosition, CBaseEntity *surface, CNavArea *standArea );
	void GrabLedge( const Vector &airPosition );
	virtual bool ClientCommand( const CCommand &args );
	const char *GetSmartLookConcept( CBaseEntity *target, char *modifiers, int size );
	const char *GetHurrahConcept();
	virtual void ProcessUsercmds( CUserCmd *pCommands, int nNumCommands, int nTotalCommands, int nDroppedPackets, bool bPaused ) OVERRIDE;
	virtual void PlayerRunCommand( CUserCmd *pCommand, IMoveHelper *pMoveHelper );
	virtual void CreateViewModel( int iViewModel = 0 );
	virtual CBaseEntity *EntSelectSpawnPoint();
	virtual bool HandleCommand_JoinTeam( int iTeam, int iCharacter, bool bForce = false );
	virtual bool ShouldAutoSelectTeam();
	virtual void OnTeamChanged( int iOldTeam );
	virtual bool CanRoam() OVERRIDE;
	virtual float GetPlayerMaxSpeed();
	virtual float GetFriction() const OVERRIDE;
	virtual int GetZombieClass() const OVERRIDE { return m_zombieClass; }
	virtual bool IsZombieClass( int zombieClass ) const // Native3254F0, introducing virtual337.
	{
		return zombieClass > 0 && ( zombieClass <= 6 || zombieClass == 8 );
	}
	virtual CBaseEntity *GiveNamedItem( const char *pszName, int nSubType = 0 ) OVERRIDE;
	void FireBullet( const Vector &, const QAngle &, const CTerrorWeaponInfo &, int nSeed );
	virtual void OnTerrorReloadStarted( bool shotgun, int oldClip, bool partial ) OVERRIDE; // Native307DF0, actual CCS virtual530.
	void OnTerrorReloadFinished();
	bool TraceTerrorShove( CTerrorWeapon *, const Vector &direction );
	TerrorPlayerAnimEvent SelectTerrorShoveAnimation( CTerrorWeapon *pWeapon );
	void NoteTerrorWeaponFired();
	double GetTimeSinceWeaponFired() const;
	virtual void SetNoiseLevel( float flRequested );
	float GetNoiseLevel() const;
	void DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData = 0 );
	TerrorPlayerAnimEvent GetDamageAnimationEvent( const CTakeDamageInfo &info ) const;
	virtual void OnTankPhysicsHitVictim( CTerrorPlayer *victim, bool newlyIncapacitated );
	virtual void OnTerrorFootstep( const Vector &origin, bool left, bool force ) OVERRIDE;
	virtual const char *GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force ) OVERRIDE;
	virtual bool ShouldPlayTerrorFootstepSound() const OVERRIDE;
	virtual bool IsTerrorFootstepSurfaceValid( float speed, bool force ) const OVERRIDE;
	bool IsRunning() const { return m_commonRunningTimer.HasStarted(); }
	float GetTimeSinceStartedRunning() const { return m_commonRunningTimer.GetElapsedTime(); }
	static bool IsAttackBlockingActivity( Activity activity );
	static bool IsClimbingActivity( Activity activity );
	virtual float GetHealthBuffer() const;
	bool IsGoingToDie() const;
	virtual bool IsIncapacitated() const;
	virtual bool IsBusy() const OVERRIDE;
	virtual bool IsProgressBarActive() const OVERRIDE;
	double GetProgressBarProgress() const;
	const CountdownTimer &GetIncapacitationRecoveryTimer() const { return m_incapacitationRecoveryTimer; }
	float GetIncapCrawlSpeed() const;
	bool IsCrawling() const;
	float GetPushDistance() const;
	Vector GetMovementViewOffset( bool duck ) const;
	Vector TraceMovementViewOffset( const Vector &origin, const QAngle &angles, float height, bool pounced ) const;
	Vector ComputeMovementViewOffset( const Vector &origin, const QAngle &angles );
	bool IsHangingFromLedge() const;
	bool IsFallingFromLedge() const;
	bool IsOnThirdStrike() const;
	bool IsAdrenalineActive() const;
	bool IsBeingPounced() const;
	bool IsTongueVictim() const;
	bool IsPummelVictim() const;
	bool IsJockeyVictim() const;
	bool IsBeingCarried() const;
	virtual bool IsGhost() const OVERRIDE;
	virtual bool CanBreatheUnderwater() const OVERRIDE { return IsGhost(); } // S3254C0.
	bool CanBeRevived() const;
	const Vector *IsStaggering() const;
	bool IsStaggeringOrPending() const;
	void ClearStagger();
	void UpdateStagger();
	bool IsNearShoveLimit() const;
	void MeleeRepel( CTerrorPlayer *source, const Vector &force );
	bool IsAnimationBusy() const;
	void OnShovedBy( CTerrorPlayer *source, bool unused );
	void StumblePounceBystanders( CTerrorPlayer *ignored );
	CBaseAbility *GetCustomAbility() const;
	bool IsLunging() const;
	void SetShoveForce( const Vector &force );
	CTerrorPlayerAnimState *GetTerrorAnimState() const;
	virtual bool FlashlightTurnOn( bool playSound = true ) OVERRIDE;
	virtual void FlashlightTurnOff( bool playSound = true ) OVERRIDE;
	virtual bool CanUseFlashlight() const OVERRIDE;
	CBaseEntity *GetDisplacingEntity() const;
	CBaseEntity *GetLedgeCauser() const;
	CBaseEntity *GetTongueReleaseSource() const;
	void RecordDamageToSurvivor( int damage, bool assist, int character );
	void RecordSurvivorLungeShove( int survivorCharacter );
	virtual void RecordDamageTaken( const char *name, int damage ) OVERRIDE;
	virtual void SelectDeathPose( const CTakeDamageInfo &info ) OVERRIDE;
	void OnSurvivorDamageResponse( const CTakeDamageInfo &info );
	void OnInfectedDamageResponse( const CTakeDamageInfo &info );
	void OnSurvivorPhysicsDamage( const CTakeDamageInfo &info );
	void OnSurvivorPain( const CTakeDamageInfo &info );
	void OnInfectedPain( const CTakeDamageInfo &info );
	int TakeGhostTriggerDamage( const CTakeDamageInfo &info );
	bool IsInvulnerable() const;
	void OnEnteredSpit();
	void OnLand( float damage );
	virtual void OnGroundChanged( CBaseEntity *oldGround, CBaseEntity *newGround ) OVERRIDE;
	void CheckpointRespawn( CTerrorCheckpoint *checkpoint );
	virtual int GetTeamSwitchRule() const;
	virtual void SpawnTraceBlood( float damage, const CTakeDamageInfo &info, const Vector &direction, trace_t *trace );
	void SetDelayedDSP( float distance, float delay, float sensitivity );
	void UpdateDelayedDSP();
	void LogTerrorDamage( const CTakeDamageInfo &info, int reason );
	void TrackSound( const char *sound );
	Activity GetTerrorActivity() const;
	bool CanPerformBackpackAction() const;
	bool IsDominated() const;
	CBaseEntity *GetDominatingAttacker() const;
	double GetPredictedFallDamage();
	CTerrorPlayer *GetPounceAttacker() const;
	CTerrorPlayer *GetTongueOwner() const;
	CTerrorPlayer *GetTongueVictim() const { return static_cast< CTerrorPlayer * >( m_tongueVictim.Get() ); }
	CTerrorPlayer *GetPummelVictim() const { return static_cast< CTerrorPlayer * >( m_pummelVictim.Get() ); }
	CTerrorPlayer *GetPummelAttacker() const { return static_cast< CTerrorPlayer * >( m_pummelAttacker.Get() ); }
	CTerrorPlayer *GetCarryVictim() const { return static_cast< CTerrorPlayer * >( m_carryVictim.Get() ); }
	bool IsHangingFromTongue() const { return m_isHangingFromTongue; }
	bool HasReachedTongueOwner() const { return m_reachedTongueOwner; }
	void SetReachedTongueOwner( bool reached ) { m_reachedTongueOwner = reached; }
	float GetTongueVictimTimerStartTime() const { return m_tongueVictimTimer.GetStartTime(); }
	bool IsAbleToDualWield() const;
	void SetLookatPlayer( CTerrorPlayer *player ) { m_lookatPlayer = player; }
	float m_lastAliveTime;

	float GetWalkTopSpeed() const;
	float GetCrouchTopSpeed() const;
	virtual bool CanJump() const;
	bool IsAutoCrouchEnabled() const;
	bool IsAutoCrouching() const;
	void StartAutoCrouch();
	bool IsAirMovementRestricted() const { return m_airMovementRestricted; }
	void SetAirMovementRestricted( bool bRestricted ) { m_airMovementRestricted = bRestricted; }
	SurvivorCharacterType GetSurvivorCharacter() const { return (SurvivorCharacterType)m_survivorCharacter.Get(); }
	void SetSurvivorCharacter( SurvivorCharacterType character );
	void SetDesiredSurvivorCharacter( int character );
	CBaseEntity *GetSurvivorPosition() const;
	void SetSurvivorPosition( CSurvivorPosition *position );
	Vector GetTonguePullPosition( bool absolute = false ) const;
	CBaseEntity *GetGroundElevator() const;
	bool IsOnElevatorThatIsMoving() const;
	CBaseMountedWeapon *GetMountedWeapon();
	bool IsPerformingOtherUseAction( int nAction = 0 ) const;
	virtual unsigned int PlayerSolidMask( bool brushOnly = false ) const OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	virtual int ShouldTransmit( const CCheckTransmitInfo *info ) OVERRIDE;
	void ResetFullConnectNotification() { m_bFullConnectNotified = false; }
	double GetTimeSinceSeenByEnemy() const;
	bool IsInCheckpoint() const;
	void UpdateCullingEligibility();
	void BeginCulling();
	void EndCulling();
	void UpdateCulling();

	CNetworkVar( int, m_survivorCharacter );
	CNetworkVar( int, m_zombieClass );
	CNetworkVar( int, m_zombieState );
	CNetworkVar( float, m_jumpSupressedUntil );
	CNetworkVar( bool, m_airMovementRestricted );
	CNetworkVar( bool, m_isAutoCrouchEnabled );
	CNetworkVar( float, m_flNextShoveTime );
	CNetworkVar( int, m_iShovePenalty );
	CNetworkVar( float, m_burnPercent );
	CNetworkVar( float, m_bbqPercent );
	CNetworkVar( float, m_noiseLevel );
	CNetworkVar( int, m_noiseLevelLatch );
	CNetworkVar( bool, m_bSurvivorPositionHidingWeapons );
	CNetworkVar( float, m_healthBuffer );
	CNetworkVar( float, m_healthBufferTime );
	CNetworkVar( bool, m_isIncapacitated );
	CNetworkVar( bool, m_isCalm );
	CNetworkVar( bool, m_isGoingToDie );
	CNetworkVar( bool, m_bIsOnThirdStrike );
	CNetworkVar( bool, m_bIsFirstManOut );
	CNetworkVar( bool, m_bAdrenalineActive );
	CNetworkVar( int, m_currentReviveCount );
	CNetworkVar( bool, m_isHangingFromLedge );
	CNetworkVar( bool, m_isFallingFromLedge );
	CNetworkVector( m_hangAirPos );
	CNetworkVector( m_hangPos );
	CNetworkVector( m_hangStandPos );
	CNetworkVector( m_hangNormal );
	CNetworkVar( bool, m_isHangingFromTongue );
	CNetworkVar( bool, m_reachedTongueOwner );
	CNetworkVar( bool, m_isProneTongueDrag );
	CNetworkVar( bool, m_usingMountedGun );
	CNetworkVar( bool, m_usingMountedWeapon );
	CNetworkVar( bool, m_isGhost );
	CNetworkVar( int, m_checkpointIncaps );
	CNetworkVar( int, m_checkpointReviveOtherCount );
	CNetworkVar( int, m_iCurrentUseAction );
	CNetworkVector( m_staggerStart );
	CNetworkVector( m_staggerDir );
	CNetworkVar( float, m_staggerDist );
	CNetworkVector( m_shoveForce );
	CNetworkVar( float, m_bashedStart );
	CNetworkVar( bool, m_isAttemptingToPounce );
	CNetworkVar( int, m_knockdownReason );
	CNetworkVar( bool, m_isCulling );
	CNetworkVar( bool, m_isRelocating );
	CNetworkVar( float, m_pushDistance );
	CNetworkVar( float, m_TimeForceExternalView );
	IMPLEMENT_NETWORK_VAR_FOR_DERIVED( m_iMaxHealth );

	virtual int TakeHealth( float health, int damageType ) OVERRIDE;

	void SetHealthBuffer( float health );

	void GiveHealthBuffer( float health, float maximumHealth );

	float HealSurvivor( float maximumHealth, float healFraction );

	void GiveHealth();

	void UseAdrenaline( float duration );

	void SetReviveCount( int count );

	void ResetSurvivorHealth();

	void OnIncapacitated( const CTakeDamageInfo &info );

	bool OnLedgeBleedOut( const CTakeDamageInfo &info );

	void ReleaseLedge();

	Vector FindLedgeRevivePosition() const;

	void RemoveHangLighting();

	CSurvivorDeathModel *GetDeathModel() const;

	void RemoveDeathModel();

	void InputEnableLedgeHang( inputdata_t &input );

	void InputDisableLedgeHang( inputdata_t &input );
	void InputIgnoreFallDamage( inputdata_t &inputdata );
	void InputIgnoreFallDamageWithoutReset( inputdata_t &inputdata );
	void InputTeleportToSurvivorPosition( inputdata_t &inputdata );
	void InputReleaseFromSurvivorPosition( inputdata_t &inputdata );
	void InputSpeakResponseConcept( inputdata_t &inputdata );
	void InputRemoveWeaponUpgrades( inputdata_t &inputdata );
	bool GiveUpgrade( int upgrade ); // S308EE0, sole upgrade_item.cpp implementation.
	void OnScavengeGasCanDestroyed(); // S306DE0, sole actual gas-owner producer.
	void InputSetGlowEnabled( inputdata_t &inputdata );
	virtual float PlayScene( const char *scene, float delay = 0.0f, AI_Response *response = NULL, IRecipientFilter *filter = NULL ) OVERRIDE;
	void InputCancelCurrentScene( inputdata_t &inputdata );

	void ResetSurvivorHealthBuffer();

	void ApplyTempHealthOnlyMutation();

	bool BeginDeathAnimation( const CTakeDamageInfo &info );

	bool IsPlayingDeathAnimation() const;

	void ReleaseJockey( CTerrorPlayer *rescuer );

	void RestoreAfterDeath( CTerrorPlayer *rescuer, CSurvivorDeathModel *deathModel );

	void StaggerPlayer( CBaseEntity *source, const Vector *sourcePosition = NULL );

	bool IsKnockedDown() const;

	void Launch( const Vector &velocity, TerrorPlayerAnimEvent event, CBaseEntity *source, float externalViewDuration = 0.0f );

	void SetPushEntity( CBaseEntity *entity );

	void SetLaunchSource( CBaseEntity *entity );

	void Knockdown( int reason );

	float GetFriendlyFireDamageMultiplier( const CTakeDamageInfo &info );

	bool CheckDamageThrottle( const CTakeDamageInfo &info, bool record );

	void UpdateCommonDamageDPS();

	bool IsArmoredHitgroup( int hitgroup ) const;

	virtual void TraceAttack( const CTakeDamageInfo &info, const Vector &direction, trace_t *trace, CDmgAccumulator *accumulator = NULL ) OVERRIDE;

	void OnTankIncapacitated( const CTakeDamageInfo &info );

	void Bash( float duration );

	void OnAbilityOwnerDamaged( const CTakeDamageInfo &info );

	virtual void OnNavAreaChanged(CNavArea *newArea,CNavArea *oldArea) OVERRIDE;

	float GetIntensity() const { return m_survivorIntensity.GetCurrent(); }
	float GetTimeAveragedIntensity() const { return m_survivorIntensity.GetTimeAveraged(); }

	void DelayCalm(float duration);

	bool ShouldBeCalm();

	bool IsCalm() const { return m_isCalm.Get(); }

	bool IsSneaking() const { return m_isSneaking; }

	bool IsInCombat(bool recent=false) const;

	double GetTimeSinceCombat() const;

	float GetTimeSinceGroupInCombat(bool everyone=false) const;

	// Main/parent final returnDOUBLE supersedes older M4 float; actual same m_enemySeenInterval. Never use12584seenByEnemy or12688injury.
	double GetTimeSinceEnemySeen() const;

	double GetTimeSinceVisibleEnemy() const;

	bool HasSeenEnemy() const { return m_enemySeenInterval.HasStarted(); }

	int CountNearbyFriends(float range,bool visibleOnly=false);

	void CollectResponseZombiePresence(bool present[9]) const;

	const CUtlVector<CHandle<CBaseCombatCharacter> > &GetVisibleEnemies() const { return m_visibleEnemies; }

	float GetVisibleEnemyDot() const { return m_bestVisibleEnemyDot; }

	float GetVisibleEnemyDistance() const { return m_lastVisibleEnemyDistance; }

	bool HasVisibleThreats() const { return m_hasVisibleThreats.Get(); }

	void UpdateSneaking();

	void UpdateStillness();

	bool IsStill(bool positionOnly=false) const;

	float GetStillnessDuration(bool positionOnly=false) const;

	float GetTimeSinceFriendSeen(CTerrorPlayer *other) const;

	float GetTimeSinceFriendNearby() const { return m_friendNearbyInterval.GetElapsedTime(); }

	void UpdateFriendSeenTimers();

	void CheckEnemySightings();

	void OnCough(CBaseEntity *source);

	void UpdateCoughAndExertion();

	void UpdatePotentiallyVisibleAreas(CNavArea *newArea,CNavArea *oldArea);

	void OnNavAreaDestroyed( TerrorNavArea *area ); // native30D890, PVS-only mesh callback

	const CUtlVector<TerrorNavArea *> &GetPotentiallyVisibleAreas() const { return m_potentiallyVisibleAreas; }

	float GetNewlyVisibleArea() const { return m_newlyVisibleArea; }

	float GetTimeSinceNavAreaChanged() const { return m_navAreaChangedInterval.GetElapsedTime(); }

	virtual bool Weapon_CanUse(CBaseCombatWeapon *pWeapon) OVERRIDE;

	virtual void Weapon_Equip(CBaseCombatWeapon *pWeapon) OVERRIDE;

	virtual void Weapon_Drop(CBaseCombatWeapon *pWeapon,const Vector *pTarget=NULL,const Vector *pVelocity=NULL) OVERRIDE;

	virtual bool Weapon_CanSwitchTo(CBaseCombatWeapon *pWeapon) OVERRIDE;

	virtual bool Weapon_Switch(CBaseCombatWeapon *pWeapon,int nViewModelIndex=0) OVERRIDE;

	virtual bool BumpWeapon(CBaseCombatWeapon *pWeapon) OVERRIDE;

	CBaseEntity *GiveNamedItemInternal(const char *pszName,int nSubType,bool bForcePickup,CBaseEntity *pAppearanceSource);

	CTerrorWeapon *GetWeaponOfType(int nWeaponID) const;

	bool HasWeaponOfType(int nWeaponType) const;
	virtual bool IsUsingWeaponType( int weaponType ) const; // S2FCBC0, active only.

	bool IsWeaponAllowedForTeam(int nWeaponID) const;

	bool DropWeapon(CBaseCombatWeapon *pWeapon,bool bThrow,const Vector *pPosition=NULL);

	bool DropWeaponInSlot(int nSlot);

	bool ThrowWeapon(CWeaponCSBase *pWeapon,CTerrorPlayer *pTarget,float flSpeed,const char *pszPlayerBone=NULL,const char *pszWeaponBone=NULL);

	void GiveBeltItem(CTerrorPlayer *pTarget);

	void OnPickedUpWeapon(CBaseCombatWeapon *pWeapon);

	void OnAmmoPickup();

	void OnGivenWeapon(CBaseCombatWeapon *pWeapon);

	virtual void RecalculateMaxSpeed() OVERRIDE;

	void EquipIncapacitatedWeapon();

	void RestoreIncapacitatedWeapon();

	void RemoveHiddenWeapon();

	CBaseCombatWeapon *GetHiddenWeapon() const { return m_hHiddenWeapon.Get(); }

	virtual void PickupObject(CBaseEntity *pObject,bool bLimitMassAndSize=true) OVERRIDE;

	virtual void ForceDropOfCarriedPhysObjects(CBaseEntity *pOnlyIfHoldingThis=NULL) OVERRIDE;

	virtual float GetHeldObjectMass(IPhysicsObject *pHeldObject) OVERRIDE;

	bool IsHoldingEntity(CBaseEntity *pEntity=NULL);
	CBaseEntity *GetHeldEntity();

	void UpdateHoldingObjectState();

	bool IsGivingNamedItem() const { return m_bGivingNamedItem; }

	void SetRestoringFromTransition(bool bRestoring) { m_bRestoringFromTransition=bRestoring; }

	bool IsPerformingUseAction(int nAction=0) const;

	bool IsUseActionTarget(int nAction=0) const;

	bool IsPerformingSelfUseAction(int nAction=0) const;

	void SetUseAction(int nAction,CBaseEntity *pTarget,float flDuration,bool bShowProgress);

	void StopUseAction();

	void UpdateUseActionProgress(float flElapsed,float flDuration);

	void OnUseEntityChanged();

	void BeginReviving(CTerrorPlayer *pTarget);

	void EndReviving(bool bInterrupted=true);

	void StopBeingRevived(bool bInterrupted=true);

	void UpdateReviving();

	float GetReviveRemainingTime() const;

	float GetReviveDuration() const;

	bool ShouldUseWideUseTolerance() const;

	CBaseEntity *FindUseEntity() OVERRIDE;

	virtual CBaseEntity *FindUseEntity(float flRange,float flDirectRange,float flMinimumDot,bool *pOutInRange=NULL,bool bPrioritizePlayers=false);

	void PlayerUse() OVERRIDE;

	void PlayerUse(CBaseEntity *pPreferred);

	void DispatchPlayerUse(CBaseEntity *pSelected);

	int ObjectCaps() OVERRIDE;

	int GetUsePriority(CBaseEntity *pUser) OVERRIDE;

	bool IsUseableEntity(CBaseEntity *pEntity,unsigned int requiredCaps) OVERRIDE;

	void OnPlayerUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float flValue );

	void OnPlayerUsed(CBaseEntity *pTarget,USE_TYPE useType);

	virtual bool CanAttack() const; // Native2C8EC0, virtual527.
	virtual bool IsZoomed(); // Native2C7930, actual integer FOV/default query.

	virtual bool IsShoveAvailable() const; // Native2C87C0, virtual565.
	virtual void ExtendNextShoveTime( float deadline ); // Native2CB6E0, virtual566.
	bool IsRecentlyAttacking() const;

	bool IsRecentlyShoving() const;

	void DispatchUseActionAnimation(int nAction,CBaseEntity *pTarget);

	void ResetUseActionProgress();

	void StopDragging();

	void StopBeingDragged();

	void BeginDragging(CTerrorPlayer *pTarget);

	void UpdateBuildableButtonUseEntity();

	void StartBeingDragged(CTerrorPlayer *pOwner);

	void UpdateDragging();

	void UpdateBeingDragged();

	void SpeakReviveInterrupted(CTerrorPlayer *pReviver,bool bLedgeHang,float flWhen);

	CNetworkVar( bool, m_holdingObject );

	CNetworkVar( int, m_checkpointFirstAidShared );


	Vector m_reviveStartDirection;

	Vector m_reviveStartPosition;

	int m_reviveStartButtons;




	float ModifyDamageForHitgroup( const CTakeDamageInfo &info, float rawDamage, float damage, bool headshot );

	bool ShouldApplySurvivorDamage( const CTakeDamageInfo &info );

	int OnTakeDamage_Alive( const CTakeDamageInfo &input );

	float GetTimeSinceEnemyInjury() const;

	float GetTimeSinceBurnDamage() const;

	virtual int OnTakeDamage( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void DeathSound( const CTakeDamageInfo &info ) OVERRIDE;

	virtual CAI_Expresser *CreateExpresser() OVERRIDE;
	void PrecacheResponseSystem();
	virtual IResponseSystem *GetResponseSystem() OVERRIDE;

	virtual void ModifyOrAppendCriteria( AI_CriteriaSet &criteria ) OVERRIDE;

	virtual void OnSpokeConcept( AIConcept_t concept, AI_Response *response ) OVERRIDE;

	void AppendSubjectCriteria( AI_CriteriaSet &criteria );

	void AppendSubjectDistanceCriteria( AI_CriteriaSet &criteria );
	void UpdateSurvivorSpeech();
	void ResetSurvivorSpeech();
	int GetZombieKillCount( int zombieClass ) const;
	int GetIncapacitatedZombieKillCount() const;
	int GetStatsPlayerID();
	CTerrorPlayer *GetMVPNominee() const;
	bool HasUsedFlashlightOutsideSafeArea() const;
	void UpdateHighestDamagePounce( int damage );
	void RemoveAward( int award );
	void RecordPhysicsHit( int survivorCharacter );
	void RecordSmokerDrag( int survivorCharacter );
	void RecordSmokerChoke( int survivorCharacter );
	void RecordZombieKilled( int zombieClass );
	void RecordVomitHit( int survivorCharacter );
	void RecordVomitExplosion( int survivorCharacter );
	virtual bool OnAwardEarned( int award, CBaseEntity *subject );
	void UpdateMissionStats();
	float GetPounceElapsedTimeForStats() const;
	float GetTongueElapsedTimeForStats() const;
	float GetBileRemainingTimeForStats() const;
	bool HasLeftCheckpoint() const { return m_hasLeftCheckpoint; }
	void OnLeftCheckpoint();

	virtual void OnSpoke( CBaseCombatCharacter *speaker, const char *sound, float duration ) OVERRIDE;

	void EmitInfectedSound( const char *sound, float minimumInterval = 0.0f, float noiseDuration = 0.0f );

	void OnFriendlyFire( CTerrorPlayer *victim, IGameEvent *event );

	void UpdateFriendlyFireResponse();

	bool TryUseSpeech();

	bool SpeakIfAllowed( AIConcept_t concept, int priority = 1, const char *modifiers = NULL, char *chosen = NULL, size_t size = 0, IRecipientFilter *filter = NULL );

	const char *GetResponseCharacterName() const;

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void BeginLunge();

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void OnPounceContact(CTerrorPlayer *victim);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void OnLungedBy(CTerrorPlayer *attacker,bool allowStagger);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void TryPounceVictim(CTerrorPlayer *victim);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void OnPounced(CTerrorPlayer *attacker);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void TryJockeyRide(CTerrorPlayer *victim);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void UpdatePounce();

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void UpdateJockeyRide();

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	bool IsPouncing()const;

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	bool IsJockeyRiding()const;

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	bool IsAttemptingToPounce() const { return m_isAttemptingToPounce; }

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	CTerrorPlayer *GetPounceVictim()const;

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	CTerrorPlayer *GetJockeyVictim()const;
	CTerrorPlayer *GetJockeyAttacker() const;
	virtual bool IsLungePushable() const;

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void SetJockeyVictim(CTerrorPlayer*);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void SetJockeyAttacker(CTerrorPlayer*);

	// Reuse the exact frozen body at its existing owner; do not implement a second handle resolver.
	void ApplyJockeyControl(float *forwardMove,float *sideMove);

	virtual void Touch( CBaseEntity *other ) OVERRIDE;

	CNetworkVector( m_jockeyDesiredMove );

	CNetworkVar( bool, m_jockeyRotatedDesire );

	float m_jockeyRawYaw;

	CNetworkVar( int, m_nVariantType );

	virtual bool CanBeShoved( CBaseEntity *pusher ) const;

	void SetVariantType( int variant );

	virtual bool ShouldShowMotD() OVERRIDE; // S301BD0, genuine inherited slot522.

	void ShowHostFile();

	virtual void OnEnterWaitingForRescue() OVERRIDE;

	virtual void OnPreThinkWaitingForRescue() OVERRIDE;

	virtual void OnEnterIntroCamera() OVERRIDE;

	virtual void OnPreThinkIntroCamera() OVERRIDE;

	virtual void OnLeaveIntroCamera() OVERRIDE;

	virtual void OnPreThinkObserverMode() OVERRIDE;

	virtual bool GetIntoGame() OVERRIDE;

	virtual void RoundRespawn() OVERRIDE;

	// BothGENUINEmodelvirtuals retained. Not a compatibilityalias: distinct nativeentries/callbackgates. No bodycollapse.
	virtual void ApplySpawnModel() OVERRIDE;

	virtual int GetRagdollType() const;

	virtual void CreateRagdollEntity( const CTakeDamageInfo &info );

	void ReleaseTongueOnDeath( bool reportRelease );

	// Main explicitly authorizes pure CCS interface; never add empty base body or a compatibility overload.
	virtual void DropWeapons( bool dead ) OVERRIDE;

	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;

	bool IsCustomAbilityActive() const;

	void ExplodeOnDeath( const CTakeDamageInfo &info );

	bool DispatchSpecialDeathEffects( const CTakeDamageInfo &info, bool wasAbilityActive );

	void DropHolidayGift();

	void CleanupPlayerState();

	virtual void OnLeaveActive() OVERRIDE;

	virtual void OnLeaveDeathAnim() OVERRIDE;

	virtual void OnLeaveDeathWaitForKey() OVERRIDE;

	virtual void SetZombieClass( int zombieClass );

	int SelectNextZombieClass( unsigned char enforceClassLimits );

	void HandleInfectedUse();

	// Existing Main death/state or Scenario ghost body; never synthesize a fallback.
	virtual void OnEnterGhost() OVERRIDE;

	// Existing Main death/state or Scenario ghost body; never synthesize a fallback.
	virtual void OnPreThinkGhost() OVERRIDE;

	// Existing Main death/state or Scenario ghost body; never synthesize a fallback.
	virtual void OnLeaveGhost() OVERRIDE;

	CNetworkArray( int, m_checkpointAwardCounts, 149 );

	CNetworkArray( int, m_missionAwardCounts, 149 );

	CNetworkArray( int, m_checkpointZombieKills, 9 );

	CNetworkArray( int, m_missionZombieKills, 9 );

	CNetworkVar( int, m_checkpointSurvivorDamage );

	CNetworkVar( int, m_missionSurvivorDamage );

	CNetworkArray( int, m_classSpawnCount, 9 );

	CNetworkVar( float, m_maxDeadDuration );

	CNetworkVar( float, m_totalDeadDuration );

	CNetworkVar( int, m_checkpointMedkitsUsed );

	CNetworkVar( int, m_checkpointPillsUsed );

	CNetworkVar( int, m_missionMedkitsUsed );

	CNetworkVar( int, m_missionPillsUsed );

	CNetworkVar( int, m_checkpointMolotovsUsed );

	CNetworkVar( int, m_missionMolotovsUsed );

	CNetworkVar( int, m_checkpointPipebombsUsed );

	CNetworkVar( int, m_missionPipebombsUsed );

	CNetworkVar( int, m_checkpointBoomerBilesUsed );

	CNetworkVar( int, m_missionBoomerBilesUsed );

	CNetworkVar( int, m_checkpointAdrenalinesUsed );

	CNetworkVar( int, m_missionAdrenalinesUsed );

	CNetworkVar( int, m_checkpointDefibrillatorsUsed );

	CNetworkVar( int, m_missionDefibrillatorsUsed );

	CNetworkVar( int, m_checkpointDamageTaken );

	CNetworkVar( int, m_missionDamageTaken );

	CNetworkVar( int, m_missionReviveOtherCount );

	CNetworkVar( int, m_missionFirstAidShared );

	CNetworkVar( int, m_missionIncaps );

	CNetworkVar( int, m_checkpointDamageToTank );

	CNetworkVar( int, m_checkpointDamageToWitch );

	CNetworkVar( int, m_missionAccuracy );

	CNetworkVar( int, m_checkpointHeadshots );

	CNetworkVar( int, m_checkpointHeadshotAccuracy );

	CNetworkVar( int, m_missionHeadshotAccuracy );

	CNetworkVar( int, m_checkpointDeaths );

	CNetworkVar( int, m_missionDeaths );

	CNetworkVar( int, m_checkpointMeleeKills );

	CNetworkVar( int, m_missionMeleeKills );

	CNetworkVar( int, m_checkpointPZIncaps );

	CNetworkVar( int, m_checkpointPZTankDamage );

	CNetworkVar( int, m_checkpointPZHunterDamage );

	CNetworkVar( int, m_checkpointPZSmokerDamage );

	CNetworkVar( int, m_checkpointPZBoomerDamage );

	CNetworkVar( int, m_checkpointPZJockeyDamage );

	CNetworkVar( int, m_checkpointPZSpitterDamage );

	CNetworkVar( int, m_checkpointPZChargerDamage );

	CNetworkVar( int, m_checkpointPZKills );

	CNetworkVar( int, m_checkpointPZPounces );

	CNetworkVar( int, m_checkpointPZPushes );

	CNetworkVar( int, m_checkpointPZTankPunches );

	CNetworkVar( int, m_checkpointPZTankThrows );

	CNetworkVar( int, m_checkpointPZHung );

	CNetworkVar( int, m_checkpointPZPulled );

	CNetworkVar( int, m_checkpointPZBombed );

	CNetworkVar( int, m_checkpointPZVomited );

	CNetworkVar( int, m_checkpointPZHighestDmgPounce );

	CNetworkVar( int, m_checkpointPZLongestSmokerGrab );

	CNetworkVar( int, m_checkpointPZLongestJockeyRide );

	CNetworkVar( int, m_checkpointPZNumChargeVictims );

	// Existing M6 statistics-body owner; no new reset design.
	void ResetCheckpointStatistics();
	int m_survivorKillScore;
	int m_score;
	float m_zombieClassDeathTimestamp[9];
	float m_minDeadDuration;
	int m_deadCount;
	int m_checkpointAccuracy;
	int m_checkpointShotsFired;
	int m_checkpointShotsHit;
	int m_survivorPounces[4];
	int m_totalSurvivorPounces;
	int m_survivorLungeShoves[4];
	int m_totalSurvivorLungeShoves;
	int m_survivorSmokerDrags[4];
	int m_totalSurvivorSmokerDrags;
	int m_survivorSmokerChokes[4];
	int m_totalSurvivorSmokerChokes;
	int m_survivorVomitHits[4];
	int m_totalSurvivorVomitHits;
	int m_survivorVomitExplosions[4];
	int m_totalSurvivorVomitExplosions;
	int m_survivorClawHits[4];
	int m_totalSurvivorClawHits;
	int m_survivorPhysicsHits[4];
	int m_totalSurvivorPhysicsHits;
	int m_survivorDirectDamage[4];
	int m_survivorAssistDamage[4];
	int m_survivorKills[4];
	int m_directSurvivorDamage;
	int m_assistSurvivorDamage;
	int m_totalSurvivorKills;
	CNetworkVar( bool, m_bWasPresentAtSurvivalStart );
	int m_teamChangeCount;
	int m_nextCreditsLineIndex;
	int m_statsPlayerID;
	float m_flBestSurvivalTime;
	int m_nSurvivalMedal;
	int m_nSpecialSpawnSlot;
	float m_flGnomePickupTime;

	double GetFlowDistance( int direction = 0 ) const;
	virtual void OnNavAreaRemoved( CNavArea *removedArea ) OVERRIDE;

	double GetLineOfScrimmageRange() const;

	double GetLineOfScrimmageFlow() const;

	double GetScrimmageSphereRadius() const;

	void UpdateLineOfScrimmage( float frontFlow = -1.0f, float rearFlow = -1.0f );

	void UpdateScrimmageSphere();

	void OnTankKilled( CTerrorPlayer *tank );

	bool ComputeLineOfScrimmage( bool front, float requested, float *flow, Vector *position, QAngle *angles ) const;

	void ClearLineOfScrimmage();

	void UpdateScrimmageYardLines( float maximum );

	void WarpGhostToInitialPosition( bool randomSurvivor );

	void MaterializeFromGhost();

	bool CanBecomeGhost( bool force );

	void AttemptBecomeGhost( bool force );

	void SetNextGhostSpawnTime( float timestamp );

	void CapGhostSpawnClockForFinale();

	void ResetInfectedLifeStatistics();

	void PlayTrackedSound( const char *sound, float duration );

	void StopTrackedSound( const char *sound );

	void RecordSurvivorIncap( int character );

	void RecordTankRockHit( int character );

	virtual void SaveForTransition( const char *targetMap );

	KeyValues *SaveTransitionData( const char *recordName );

	void RestoreTransitionData( KeyValues *data );

	KeyValues *RestorePendingTransition();

	bool OnEndChangeLevel();

	void RestoreTransitionModel( KeyValues *data );

	bool TakeOverBot( bool force = false );

	CTerrorPlayer *ReplaceWithBot( bool force = false );

	CTerrorPlayer *GetSpectatedBot() const;

	void UpdateReplacementCollisionBounds();

	bool IsAwaitingTransitionRestore() const { return m_bAwaitingTransitionRestore; }

	void SetAwaitingTransitionRestore( bool awaiting ) { m_bAwaitingTransitionRestore = awaiting; }

	virtual void GiveDefaultItems();

	void OnTankSpawned( CTerrorPlayer *tank );
	// S3254A0 is the genuine RET4 base callback; SurvivorBot overrides S38B830.
	virtual void OnPlayerDisconnected( CTerrorPlayer * ) {}

	void OnClawHitSurvivorCharacter( int character );

	float GetTankHitDamage() const;

	void ResetFrustration();
	void SetAttackingWitch( Witch *witch );
	Witch *GetAttackingWitch() const;
	bool IsNearAttackingWitch() const;

	void UpdateTankFrustration();

	void KillSilently();

	void TakeOverTankBot( CTerrorPlayer *botTank );

	CNetworkVar( int, m_iBloodyHandsLevel );




	void AddTerrorSplatter( int amount, float distance, int kind );

	CNetworkVar( float, m_vomitStart );

	CNetworkVar( float, m_vomitFadeStart );

	virtual void OnHitByVomitJar( CBaseEntity *pAttacker ) OVERRIDE;

	virtual bool IsIT() const OVERRIDE;

	void UpdateITEvents();

	virtual bool CanChangeName() OVERRIDE;

	virtual void ChangeName( const char *name ) OVERRIDE;

	void UpdateClientSettings();


	bool HasSkippedOuttro() const;

	int GetRematchVote() const;

	virtual bool SetObserverMode( int nMode ) OVERRIDE;

	virtual bool SetObserverTarget( CBaseEntity *pTarget ) OVERRIDE;

	virtual CBaseEntity *FindNextObserverTarget( bool bReverse ) OVERRIDE;

	virtual int GetNextObserverSearchStartPoint( bool bReverse ) OVERRIDE;

	virtual bool IsValidObserverTarget( CBaseEntity *pTarget ) OVERRIDE;

	virtual void CheckObserverSettings() OVERRIDE;

	bool PassesObserverFilter( CBaseEntity *pTarget );

	void CollectObserverTargets( CUtlVector< CBaseEntity * > &targets );

	void BeginObservingSurvivorBot( CTerrorPlayer *pBot );

	bool IsIdle() const;

	void GoAwayFromKeyboard();

	static bool IsGenuineSurvivorBot( CTerrorPlayer *pPlayer );

	static CTerrorPlayer *FindSurvivorBotForCharacter( SurvivorCharacterType character );

	void ClearDelayedDSP();

	void AddBloodToHands();

	void MakeHandsBloody();

	// EXACT PROVIDED member inline from E9D5; do not add a second out-of-line body. Constructors ONLYSetOwner(this), notm_music(this).
	Music *GetMusic() { return &m_music; }

	// EXACT PROVIDED member inline from E9D5; do not add a second out-of-line body. Constructors ONLYSetOwner(this), notm_music(this).
	const Music *GetMusic() const { return &m_music; }

	CNetworkVar( int, m_clientIntensity );

	CNetworkVar( bool, m_hasVisibleThreats );



	Vector m_dragOwnerLastPosition;

	float m_lastUseActionProgressReset;


	CNetworkVector( m_tugStart );

	CNetworkVector( m_tugDir );

	CNetworkVar( float, m_tugDist );

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void SetPummelTarget( CTerrorPlayer *victim, float delay );
	void BecomeCarried( CTerrorPlayer *charger );
	void CarryVictim( CTerrorPlayer *victim );
	void BecomePummeled( CTerrorPlayer *charger );
	void StartPummel( CTerrorPlayer *victim );
	void ImpactChargerVictim( CTerrorPlayer *victim, bool hitWall, bool longFall );
	void UpdateCarry();
	void UpdatePummel();
	void OnChargerPound();
	void SetTongueVictim( CTerrorPlayer *victim );
	void BecomeTongueVictim( CTerrorPlayer *smoker );
	void GrabVictimWithTongue( CTerrorPlayer *victim );
	void BeginTongueChoke( int reasons );
	void SetProneTongueDrag( bool prone );
	void UpdateTongueVictim();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleaseCarry( bool releaseOther, bool cancelPummel, bool preserveVictimParent );

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	bool IsRespondingToSurvivorDistress() const;

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	CTerrorPlayer *RequestSurvivorBotAssistance();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void UnstickAfterPin();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleasePounce();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleasePummel( bool releaseOther, CTerrorPlayer *rescuer );

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void EndTongueChoke( int reasons );

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleaseTongueVictim();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleaseTongueOwner();

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ReleaseTongue( bool force );

	// Exact current existing-owner signature; NO default arguments were present in these definition signatures. Parent migrates obsolete prose callsites/default assumptions, without adding an overload.
	void ClearTug();

	CNetworkVar( int, m_frustration );

	// EXACT supplied shared inline body; CLIENT class spelling follows existing actual paired class. No RTTI/type/life/reciprocity validation, no separate sharedCPP definition.
	CTerrorPlayer *GetPummelTarget() const { return static_cast< CTerrorPlayer * >( m_pummelTarget.Get() ); }


	// Use sole existing frozen Core body; no new code or alternate caller.
	void ShowRoundStats();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void PrepareForFinaleLoss();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void OnMissionLost();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void OnScavengeRoundHalftime();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void OnScavengeRoundEnd();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void OnScavengeMatchEnd();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void PrepareForRoundRestart();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void SaveIdleStateForTransition();

	// Use sole existing frozen Core body; no new code or alternate caller.
	virtual void ChangeTeam( int team ) OVERRIDE;

	// Use sole existing frozen Core body; no new code or alternate caller.
	bool TryRemarkableSpeech();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void UpdateIdleSpeech();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void OnSurvivalRoundStarted();

	// Use sole existing frozen Core body; no new code or alternate caller.
	void UpdateSurvivalRecord( float time );

	// Use sole existing frozen Core body; no new code or alternate caller.
	void ResetVersusDamageStatistics();

	float m_flNextIdleVocalizeTime;

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void DrawSurvivorNavDebug();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	bool UpdateBoomerFalling();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void UpdateIncapacitatedTank();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void UpdateTankBump();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void DrawInfectedDebug();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void UpdateHealingGnome();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void UpdateAutoJoinTeam();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	bool UpdateAirborneState();


	Vector m_lastPostThinkVelocity;

	bool m_wasAirborne;

	bool m_bBoomerFalling;

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateSpitterBurnMusic();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateInfectedVocalizations();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void DecayLineOfScrimmage();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateWeaponVisibility();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void RestartLedgeGroundCheckTimer();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateStaggerSource();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateFallingFromLedge();

	// Use sole frozen phaseA1DC9 body; no repeated detached updater wrappers.
	void UpdateNoise();

	// Canonical CLOSED: earlierA GetCharacterDisplayName ownsONEbody; B GetPlayerTypeName removed, no alias or extra virtual.
	virtual const char *GetCharacterDisplayName() const;


	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CNetworkVar( float, m_salivaStart );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	CNetworkVar( int, m_totalSurvivorDamage );
	int GetTotalSurvivorDamage() const { return m_totalSurvivorDamage; }

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	virtual void RestorePreviousWeapons();

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	virtual void PreThink() OVERRIDE;

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	virtual void HandleAnimEvent( animevent_t *event ) OVERRIDE;

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void UpdateZombieState();

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void SetZombieState( int state );

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void ResetClassSpawnStatistics();

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void RefreshSpectatorIdleTime();

	// EXACT PROVIDED INLINE BODY, not newly authored; retain identical existing owner.
	float GetSpectatorIdleTime() const { return m_flSpectatorIdleTime; }

	// EXACT PROVIDED INLINE BODY, not newly authored; retain identical existing owner.
	CNavArea *GetSpawnArea() const { return m_spawnArea; }

	CNetworkVar( bool, m_isInMissionStartArea );

	CNetworkVar( bool, m_bSurvivorGlowEnabled );

	float m_lastSpitTime;

	int m_burnDamageType;

	// Defaults are EXACTLY supplied in the frozen declaration, not inferred. No baseIgnite call / no client mirror.
	virtual void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false ) OVERRIDE;

	int m_missionIDSpawnedOn;

	int m_teamWhenMissionStarted;

	int m_incapacitatedZombieKills;

	// Reuse EXACT supplied body at1816:41-47; no airtime/ground/enemy-injury surrogate, no new fields.
	float GetTimeSinceLastDamage() const;

	// Server body exists ONLY in parent-owned use5BA8:46-63; client resets of punch/viewroll/lastshove MUST NOT be copied server-side.
	virtual void ResetForSpawn();

	// Use frozen EB4C whole existing-owner payload; no duplicate animation update.
	virtual void UpdateAddonBits() OVERRIDE;
private:
	SurvivorCharacterType SelectSurvivorCharacter( int requested ) const;
	// TWO genuine nativevirtuals: keep this AND ApplySpawnModel554. Main latest nativeproof supersedes earlier single-interface interpretation. No collapse/removal.
	virtual void SetModelFromClass() OVERRIDE;
	void NotifyCommonHearing();
	bool IsCommonPostThinkDue();
	void SelfReleaseLedge();
	void UpdateBurnEffects();
	float m_previousTerrorFireTimestamp;
	float m_lastNoiseTime;
	float m_lastCommonPostThink;
	bool m_commonPostThinkDue;
	CNetworkVar( int, m_iVersusTeam ); // Native16328; actual ChangeTeam335BD0 is the existing writer.
	bool m_bReloadVoicePlayed;
	int m_iDesiredSurvivorCharacter;
	bool m_bFullConnectNotified;
	int m_senseFlags;
	mutable int m_lastHealthBuffer;
	int m_savedHealth;
	int m_savedHealthBuffer;
	Vector m_hangFallPos;
	CNavArea *m_hangStandArea;
	Vector m_hangClimbEndPos;
	Vector m_hangSurfaceOrigin;
	QAngle m_hangSurfaceAngles;
	bool m_isReleasingLedge;
	int m_pendingStaggerEventData;
	int m_staggerThreshold;
	bool m_isCommentaryStatue;
	float m_fallDamageImmunityUntil;
	bool m_clearFallDamageImmunityOnDamage;
	bool m_canGrabLedge;
	bool m_ledgeHangingEnabled;
	int m_totalReviveCount;
	bool m_isBleedingOut;
	bool m_incapBloodPoolPlaced;
	KeyValues *m_deathEvent;


	void UpdateMusicMobSightings();

	void RememberVisibleEnemies(const CUtlVector<CBaseCombatCharacter *> &enemies,CBaseCombatCharacter *best,float dot);

	bool DropWeaponInternal(CBaseCombatWeapon *pWeapon,CBaseCombatWeapon *pUnused,bool bThrow,const Vector *pPosition);

	void StoreWeaponReserveForDrop(CWeaponCSBase *pWeapon);





	bool m_isSneaking;

public:
private:



	bool m_hasCommentedOnDeadFriend[4];


	bool m_hasPendingCough;



	CNavArea *m_pendingAASArea;

	CNavArea *m_previousAASArea;


	float m_newlyVisibleArea;




	float m_bestVisibleEnemyDot;

	float m_lastVisibleEnemyDistance;




	Vector m_vecSpeechEyeVelocity;

	Vector m_vecSpeechEyeAcceleration;

	QAngle m_angLastSpeechEye;

	float m_flLastSpeechEyeSample;



	bool m_hasUsedFlashlightOutsideSafeArea;

	int m_nSpeechPriority;

	int m_nNextSpeechPriority;


	int m_friendlyFireSubject;


	int m_friendlyFireDamageType;

	char m_friendlyFireWeapon[32];


	bool m_bRestoringFromTransition;



	float m_roundStartTime[2];


	bool m_pounceLanded;

	int m_pounceDamageRemaining;

	CNetworkVector( m_pounceStartPosition );



	int m_pounceReleaseFrame;



	float m_jockeyStaggerPhaseX;

	float m_jockeyStaggerPhaseY;

	float m_jockeyControlPhase;


	float m_jockeyRideStartTime;


	float m_pendingPounceTime;

	float m_jockeyRawForwardMove;

	float m_jockeyRawSideMove;


	float m_jockeyMountDuration;

	bool m_isFirmlyAttached;

	float m_flLineOfScrimmageFlow;

	float m_flLineOfScrimmageCurrentFlow;

	CNetworkVector( m_lineOfScrimmagePos );

	CNetworkQAngle( m_lineOfScrimmageAngle );

	float m_flRearLineOfScrimmageFlow;

	float m_flRearLineOfScrimmageCurrentFlow;

	CNetworkVector( m_rearLineOfScrimmagePos );

	CNetworkQAngle( m_rearLineOfScrimmageAngle );

	float m_flNextGhostSpawnTime;

	CNetworkVector( m_scrimmageSphereCenter );

	CNetworkVar( float, m_scrimmageSphereInitialRadius );

	CNetworkVar( int, m_scrimmageType );

	CNetworkVar( float, m_scrimmageStartTime );

	CNetworkVar( float, m_survivorsLineOfScrimmageDistance );


	CNetworkArray( Vector, m_yardLinePos, 5 );

	CNetworkArray( QAngle, m_yardLineAngles, 5 );

	bool m_bNativeGhostStateFlag11448;

	CNetworkVar( int, m_ghostSpawnState );

	CNetworkVar( int, m_ghostSpawnClockMaxDelay );

	CNetworkVar( int, m_ghostSpawnClockCurrentDelay );



	bool m_bNativeGhostStateFlag11489;

	bool m_bNativeGhostFinaleGate11490;

	int m_iGhostWarpCharacter;


	int m_survivorIncaps[4];

	int m_totalSurvivorIncaps;

	int m_survivorTankRockHits[4];

	int m_totalSurvivorTankRockHits;

	int m_nTerrorSplatter[4];



	float m_commonDamageDPS;



	int m_tongueChokeReasons;


	bool m_tongueGrabPending;



	void FlushTerrorSplatter();


	bool m_bITFromBoomer;

	bool m_bIsITUntilNextAttack;

	bool m_bWasIT;

public:
	bool m_bReusingInfectedSpawnState;

	int m_reusedInfectedSpawnHealth;

	float m_reusedInfectedRecharge;
private:

	float m_reusedInfectedRechargeDuration;


	KeyValues *m_pClientSettings;

	float m_lastSmartLookTime; // Native16312, allocation0; no invented constructor/Spawn reset or descriptor.

	bool m_bHasSkippedOuttro;

	int m_nRematchVote;



	float m_previousDamageTime;



	int m_delayedDSP;


	bool m_bAwaitingTransitionRestore;

	bool m_hasLeftCheckpoint;

	bool m_isIdle;

	bool m_wasIdle;

	bool m_bCarryingHealingGnome;

	// Use sole existing frozen Core body; no new code or alternate caller.
	void FreezeForScavengeRound();

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	void GetNavDebugColor( SurvivorCharacterType requestedCharacter, int *red, int *green, int *blue ) const;

	// Whole scheduled338740 positions only. Exact provided body9599; native2C8D60 must have ONE character-display wrapper owner across phaseA/B.
	bool IsTankBumpAttachmentObstructed( int attachment );

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_bNativeGhostClockFlag;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_deathTimestamp;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_deathFlowDistance;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	Vector m_deathPosition;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_flGhostSpawnDeadline;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	float m_flPendingGhostSpawnDeadline;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_hasPlayedSpawnMusic;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_lastHadElites;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_lastPrimaryWeaponAmmo;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_lastPrimaryWeaponId;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_lastSecondaryWeaponId;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_nativeZombieVariantChoice;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_suppressDeathEffects;
	bool m_deathEventAborted; // Native13194; actual Join/Spawn/death writers, no ctor store.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	bool m_wantsToPlay;

	// Exact Main field; no alias/extra reset/transport. Z is an omission, not a ctor assignment.
	int m_zombieClassSerial;

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void OnZombieAlert();

	// Exact provided native interface; no OVERRIDE for Terror-only introduction.
	void OnZombieEngaged();




	bool m_hasSpawnedOnTeam[2];

	CNavArea *m_spawnArea;

	Vector m_spawnAreaSamplePosition;




	float m_flSpectatorIdleTime;

	float m_delayedFlashlightUpdateTime;

	bool m_hasReportedStuck;

	bool m_bRespawningFromDefibrillator;


	// Exact parent/native access contract; one private actorfield/GoIdle, no publicaccessor/alias.
	friend class INextBot;

	// Exact parent/native access contract; one private actorfield/GoIdle, no publicaccessor/alias.
	void GoIdle();
	void OnFirstSpawnInMission(); // S316F90; sole existing statistics body, direct Spawn callback.


	void ExpireAdrenaline();
	void UpdateDefibrillatorReturnToLife();

	void UpdateTeamDesired(); // S307770, raw desired-team/avatar transition.
};

extern int g_iMinigunAmmoIndex;

CTerrorPlayer *CreateTerrorInfectedPlayerBot( int zombieClass );
void CollectTerrorPlayers( CUtlVector< CTerrorPlayer * > &out, int team, int stateMask, int controlMask, int zombieClass );
int CountLivingSurvivors( unsigned char flags );
void CollectMobileSurvivors( CUtlVector< CTerrorPlayer * > &out );
void SpeakConceptToSurvivors( CBaseEntity *actor, AIConcept_t concept, const char *modifiers ); // native304110/2D1B80
void DebugAbilityVictim( CTerrorPlayer *owner, CTerrorPlayer *victim, const char *format, ... ); // S360C80.
void LogIncapEvent( const char *format, CBaseEntity *first, CBaseEntity *second );
void IncrementTerrorBulletSerial();
int GetTerrorBulletSerial();
void BeginTerrorWeaponMessageScope();
void EndTerrorWeaponMessageScope();
void SendPZDamageMessage( int type, CTerrorPlayer *victim, CTerrorPlayer *attacker, CTerrorPlayer *assist = NULL, bool allTeams = false, int amount = 0 );

inline CTerrorPlayer *ToTerrorPlayer( CBaseEntity *pEntity )
{
	return pEntity && pEntity->IsPlayer() ? static_cast< CTerrorPlayer * >( pEntity ) : NULL;
}

#endif // TERRORPLAYER_H
