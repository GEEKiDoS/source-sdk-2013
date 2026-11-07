//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_TERRORPLAYER_H
#define C_TERRORPLAYER_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/player/c_cs_player.h"
#include "l4d2/terror_shareddefs.h"
#include "util_shared.h"
// Existing actual Music definition for plain clientmember
#include "l4d2/sound/terror_music.h"
#include "panelmetaclassmgr.h"
#include "tier1/utlmap.h"
#include "particles_new.h"
#include "l4d2/effects/terror_glow_player.h"

class CTerrorPlayerAnimState;
class C_BaseAbility;
class C_TerrorWeapon;
class C_BaseMountedWeapon;
class C_SceneEntity;
class CSoundPatch;
class IMatRenderContext;
struct dlight_t;
namespace vgui { class Panel; }

class C_TerrorPlayer : public C_CSPlayer
{
public:
	DECLARE_CLASS( C_TerrorPlayer, C_CSPlayer );
	friend class CTerrorGameMovement;
	friend class CTerrorPlayerAnimState;
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	C_TerrorPlayer();
	virtual bool CreateMove( float inputSampleTime, CUserCmd *cmd ) OVERRIDE;
	virtual void PlayerUse( C_BaseEntity *preferred = NULL ) OVERRIDE;
	void AvoidPlayers( CUserCmd *cmd );
	int GetNextTeammateFocusCharacter( bool requireThreat );
	virtual void UpdateClientSideAnimation() OVERRIDE;
	virtual const QAngle &GetRenderAngles() OVERRIDE;
	bool IsAnimationBusy() const;
	Activity GetTerrorActivity() const;
	CTerrorPlayerAnimState *GetTerrorAnimState() const;
	void DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData = 0 );
	bool ShouldHideGrenadeProjectile() const;
	virtual void FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options ) OVERRIDE;
	static bool IsAttackBlockingActivity( Activity activity );
	static bool IsClimbingActivity( Activity activity );
	float GetHealthBuffer() const;
	bool IsGoingToDie() const;
	C_BaseAbility *GetCustomAbility() const;
	bool IsLunging() const;
	void SetShoveForce( const Vector &force );
	bool IsIncapacitated() const;
	virtual float GetFriction() const OVERRIDE;
	virtual bool IsBusy() const OVERRIDE;
	virtual bool IsProgressBarActive() const OVERRIDE;
	double GetProgressBarProgress() const;
	virtual float GetIncapCrawlSpeed() const;
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
	virtual bool IsGhost() const;
	bool CanBeRevived() const;
	C_TerrorPlayer *GetPounceAttacker() const;
	C_TerrorPlayer *GetTongueOwner() const;
	C_TerrorPlayer *GetTongueVictim() const { return static_cast< C_TerrorPlayer * >( m_tongueVictim.Get() ); }
	C_TerrorPlayer *GetPummelVictim() const { return static_cast< C_TerrorPlayer * >( m_pummelVictim.Get() ); }
	C_TerrorPlayer *GetPummelAttacker() const { return static_cast< C_TerrorPlayer * >( m_pummelAttacker.Get() ); }
	C_TerrorPlayer *GetCarryVictim() const { return static_cast< C_TerrorPlayer * >( m_carryVictim.Get() ); }
	bool IsHangingFromTongue() const { return m_isHangingFromTongue; }
	bool IsAbleToDualWield() const;
	C_BaseEntity *FindUseEntity();
	virtual C_BaseEntity *FindUseEntity( float flRange, float flDirectRange, float flMinimumDot,
		bool *pOutInRange = NULL, bool bPrioritizePlayers = false );
	virtual bool IsUseableEntity( C_BaseEntity *pEntity, unsigned int requiredCaps ) OVERRIDE;
	virtual int ObjectCaps() OVERRIDE;
	virtual int GetUsePriority( C_BaseEntity *pUser ) OVERRIDE;
	bool IsPerformingUseAction( int nAction = 0 ) const;
	bool IsUseActionTarget( int nAction = 0 ) const;
	bool IsPerformingSelfUseAction( int nAction = 0 ) const;
	bool IsPerformingOtherUseAction( int nAction = 0 ) const;
	C_BaseMountedWeapon *GetMountedWeapon();
	virtual void OnMountedWeaponFired(); // Native C254670; sole two-timer owner for gun and mounted fire.
	Vector GetTonguePullPosition( bool absolute = false ) const;
	virtual unsigned int PlayerSolidMask( bool brushOnly = false ) const OVERRIDE;
	virtual bool ShouldCollide( int collisionGroup, int contentsMask ) const OVERRIDE;
	bool IsDominated() const;
	bool IsUseActionAnimationBlocked() const;
	virtual bool CanAttack() const OVERRIDE;
	virtual bool IsZoomed() OVERRIDE; // Native2697B0, one shared paired body.
	virtual bool IsShoveAvailable() const; // Native26A0B0, virtual356.
	virtual void ExtendNextShoveTime( float deadline ); // Native26CFC0, virtual357.
	bool CanPerformBackpackAction() const;
	const Vector *IsStaggering() const;
	bool IsKnockedDown() const;
	void ResetForSpawn();
	virtual void OnPreDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual void Spawn() OVERRIDE;
	void ReloadEffect( int originalActivity ); // C226CE0; inherited sole delayed-sound list.
	virtual void ProcessMuzzleFlashEvent() OVERRIDE;
	virtual float GetPlayerMaxSpeed();
	virtual void RecalculateMaxSpeed() OVERRIDE;
	virtual int GetZombieClass() const { return m_zombieClass; }
	virtual int GetMaxHealth() const { return m_iMaxHealth; }
	virtual float GetWalkTopSpeed() const;
	virtual float GetCrouchTopSpeed() const;
	virtual bool CanJump() const;
	bool IsStaggeringOrPending() const;
	bool IsAutoCrouchEnabled() const;
	bool IsAutoCrouching() const;
	void StartAutoCrouch();
	bool IsAirMovementRestricted() const { return m_airMovementRestricted; }
	void SetAirMovementRestricted( bool bRestricted ) { m_airMovementRestricted = bRestricted; }
	SurvivorCharacterType GetSurvivorCharacter() const { return (SurvivorCharacterType)m_survivorCharacter; }
	float GetITBlend() const;
	float GetITTimerDuration() const { return m_itTimer.m_duration; }
	float GetITTimerTimestamp() const { return m_itTimer.m_timestamp; }
	virtual ~C_TerrorPlayer() OVERRIDE;
	virtual void BuildTransformations( CStudioHdr *, Vector *, Quaternion[], const matrix3x4_t &, int, CBoneBitList & ) OVERRIDE;
	virtual void AccumulateLayers( IBoneSetup &, Vector[], Quaternion[], float ) OVERRIDE;
	virtual void OnModelPoseParametersChanged() OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual void MaintainSequenceTransitions( IBoneSetup &, float, Vector[], Quaternion[] ) OVERRIDE;
	virtual bool StartSceneEvent( CSceneEventInfo *, CChoreoScene *, CChoreoEvent *, CChoreoActor *, C_BaseEntity * ) OVERRIDE;
	virtual bool IsSceneActive( CChoreoScene *scene ) OVERRIDE;
	virtual int GetRealHealth() const OVERRIDE;
	virtual C_BaseCombatWeapon *Weapon_GetSlot( int slot ) const OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void OnNewParticleEffect( const char *, CNewParticleEffect * ) OVERRIDE;
	virtual void OnParticleEffectDeleted( CNewParticleEffect * ) OVERRIDE;
	virtual void UpdateCollisionBounds() OVERRIDE;
	virtual void OnClientFootstepEvent() OVERRIDE;
	virtual void GetFootstepSoundPosition( bool, bool, bool, Vector *, bool *, bool * ) OVERRIDE;
	virtual bool ShouldPlayTerrorFootstepSound() const OVERRIDE;
	virtual bool IsTerrorFootstepSurfaceValid( float, bool ) const OVERRIDE;
	virtual const char *GetTerrorFootstepSoundName( const char *, bool, float, bool ) OVERRIDE;
	C_TerrorWeapon *GetWeaponOfType( int weaponID ) const;
	void UpdateSurvivorExpression();
	void UpdateInfectedExpression();
	void UpdateDeadExpression();
	void GetExpressionIntensity( float *, float * );
	void SetClientExpressionScene( const char * );
	void ClearClientExpressionScene();
	bool CanDoSceneGesture();
	bool StartGestureSceneEvent( CSceneEventInfo *, CChoreoScene *, CChoreoEvent *, CChoreoActor *, C_BaseEntity * );
	void ClearTerrorLaserSightEffects( int user );
	int RemapLowViolenceSequence( int );
	static void RecvProxy_TerrorSequence( const CRecvProxyData *, void *, void * );
	virtual int GetDefaultFOV() OVERRIDE;
	void UpdateGlow( int slot, const Vector &, const Vector & );
	virtual void GetGlowColor( float *, float *, float *, float * ) OVERRIDE;
	virtual bool GetStencilState( ShaderStencilState_t &state ) OVERRIDE;
	float GetGlowNoiseAlpha() const;
	bool IsGlowHighlightedByAbility( int slot ) const { return m_GlowView[slot].m_bAbilityHighlight; }
	int GetGlowUseEntityIndex() const { return m_nUseEntity; }
	int GetSelectedGlowEntityIndex() const { return m_nGlowEntity; }
	C_BaseEntity *GetGlowUseTarget() const { return m_hUseGlowTarget.Get(); }
	bool WasDrawnForGlow( int slot ) const { return m_nGlowDrawFrame[slot] == gpGlobals->framecount; }
	void MarkGlowDrawFrame( int slot ) { m_nGlowDrawFrame[slot] = gpGlobals->framecount; }
	bool IsSurvivorGlowEnabled() const { return m_bSurvivorGlowEnabled; }
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual const Vector &GetRenderOrigin() OVERRIDE;
	virtual bool ShouldDrawSpeechVoice();
	void UpdateSpeechVoicePresentation();
	virtual bool Simulate() OVERRIDE;
	virtual void UpdateClientSideParticleEffects() OVERRIDE;
	virtual bool IsThirdPersonCameraActive() const OVERRIDE;
	virtual void CalcThirdPersonView( Vector &, QAngle & ); // Native INTRODUCER1456, not override.
	virtual void CalcView( Vector &, QAngle &, float &, float &, float & ) OVERRIDE;
	virtual void CalcDeathCamView( Vector &, QAngle &, float & ) OVERRIDE; // Native1312/mode4.
	virtual void CalcChaseCamView( Vector &, QAngle &, float & ) OVERRIDE;
	virtual void CalcRoamingView( Vector &, QAngle &, float & ) OVERRIDE;
	virtual void CalcInEyeCamView( Vector &, QAngle &, float & ) OVERRIDE;
	virtual Vector GetChaseCamViewOffset( C_BaseEntity *target ) OVERRIDE;
	virtual void OverrideMouseInput( float *, float * ) OVERRIDE;
	virtual void OverrideJoystickInput( float *, float * ) OVERRIDE;
	bool IsBeingDraggedByTongue() const;
	double GetDesiredThirdPersonCameraDistance() const;
	virtual bool ShouldDraw() OVERRIDE;
	virtual bool ShouldPredict() OVERRIDE;
	virtual bool ShouldDrawEntityFlameLight() const OVERRIDE;
	C_BaseEntity *GetRescueModelIfDead() const;
	bool IsPouncing() const;
	bool IsJockeyRiding() const;
	bool IsAttemptingToPounce() const { return m_isAttemptingToPounce; }
	C_TerrorPlayer *GetPounceVictim() const;
	C_TerrorPlayer *GetJockeyVictim() const;
	C_TerrorPlayer *GetJockeyAttacker() const;
	virtual const char *GetFlashlightTextureName() OVERRIDE;
	virtual float GetFlashlightFOV() OVERRIDE;
	virtual float GetFlashlightFarZ() OVERRIDE;
	virtual float GetFlashlightLinearAttenuation() OVERRIDE;
	virtual bool CastsFlashlightShadows() OVERRIDE;
	virtual void ComputeFlashlightOffset( const Vector &, const Vector &, const Vector &, Vector & ) OVERRIDE;
	const QAngle &GetAimEyeAnglesWithPunch();
	void UpdateLineOfScrimmagePresentation();
	void DrawLineOfScrimmagePresentation();
	int GetYardLineNumber( vgui::Panel * );
	int GetYardLineAlpha( vgui::Panel * );
	double GetBecomeGhostAt() const;
	double GetScrimmageSphereRadius() const;
	const Vector &GetScrimmageSphereCenter() const;
	virtual bool CanEnableFlashlight(); // NativeINTRODUCER1452.
	bool EnableFlashlight( bool playSound );
	void ProcessFlashlightImpulse();
	virtual void ItemPostFrame() OVERRIDE;
	void UpdateVomitParticleEffects(); // Existing SOLE body in c_vomitjar_projectile.cpp, NOT copied.
	bool HasVomitScreenSpaceParticles() const;
	void RenderVomitScreenSpaceParticles( IMatRenderContext * );

	// One ordered nontrivial union after C_CSPlayer. PE32 offsets below are
	// provenance only: no padding, offset constants or target layout assertions.
	CHandle< C_BaseEntity > m_hBuildableButtonUseEnt; // 6848.
	CountdownTimer m_clientPresentationTimer; // 7048.
	CountdownTimer m_tankAttachmentImpactTimer; // 7060, Move198A.
	CHandle< C_BaseEntity > m_useActionTarget; // 7072.
	CHandle< C_BaseEntity > m_useActionOwner; // 7076.
	CUtlMap< int, int, unsigned short > m_lowViolenceSequenceMap; // 7104.
	// Producerless vectors7160/7180 and their allocations/frees are D586-elided.
	CHandle< C_BaseEntity > m_vocalizationSubject; // 7200.
	CNetworkVarEmbedded( CountdownTimer, m_vocalizationSubjectTimer ); // 7204.
	CountdownTimer m_noAvoidanceTimer; // 7216, plain.
	CNetworkVarEmbedded( CountdownTimer, m_staggerTimer ); // 7228.
	CNetworkVarEmbedded( CountdownTimer, m_tugTimer ); // 7268.
	CUtlVector< Vector > m_lineOfScrimmagePolyline; // 7400.
	CUtlVector< Vector > m_rearLineOfScrimmagePolyline; // 7444.
	CUtlVector< Vector > m_yardLinePolylines[5]; // 7584..7683.
	CPanelWrapper m_yardLinePanels[5]; // 7684..7703, actual SDK panel owner.
	int m_yardLinePanelWidth[5]; //7704..23; OMIT ctor/Spawn/reset.
	int m_yardLinePanelHeight[5]; //7724..43; OMIT ctor/Spawn/reset.
	int m_yardLineAlpha[5]; //7744..63; real draw writer ONLY.
	CNetworkVarEmbedded( IntervalTimer, m_autoCrouchTimer ); // 7884.
	CHandle< C_BaseAbility > m_customAbility; // 7892.
	CountdownTimer m_stunTimer; // 7896.
	IntervalTimer m_terrorFireInterval; // 7908, existing fire owner.
	IntervalTimer m_stationaryTimer; // 7916.
	IntervalTimer m_viewAccelerationRestTimer; // 7924.
	float m_flStatsLifeStartTime; // Native8004, actual allocation-zero field: no ctor/Spawn/reset/recv writer.
	CUtlVector< EHANDLE > m_Candidates; // 8020, sole M7 selection vector.
	CHandle< C_BaseEntity > m_tongueVictim; // 8040.
	CHandle< C_BaseEntity > m_tongueOwner; // 8044.
	IntervalTimer m_tongueVictimTimer; // 8048, plain.
	Vector m_initialTonguePullDir; //8056, genuine received Vector; OMIT ctor.
	CHandle< C_BaseEntity > m_reviveOwner; // 8072.
	CHandle< C_BaseEntity > m_reviveTarget; // 8076.
	CHandle< C_BaseEntity > m_hClientReviveEntity; // 8080.
	CHandle< C_BaseEntity > m_dragTarget; // 8084.
	CHandle< C_BaseEntity > m_positionEntity; // 8088.
	CHandle< C_BaseEntity > m_pushEntity; // 8096.
	CountdownTimer m_itTimer; // 8140, plain.
	IntervalTimer m_weaponChangeInterval; // 9700.
	CNetworkVarEmbedded( IntervalTimer, m_hangTimer ); // 9712.
	EHANDLE m_hLastObserverCameraTarget; // 9784, independent of received target.
	Music m_music; // 9852, one actual default construction.
	CHandle< C_BaseEntity > m_carryAttacker; // 10004.
	CHandle< C_BaseEntity > m_carryVictim; // 10008.
	CHandle< C_BaseEntity > m_pummelVictim; // 10012.
	CHandle< C_BaseEntity > m_pummelAttacker; // 10016.
	CHandle< C_BaseEntity > m_pummelTarget; // 10020, queuedPummelVictim.
	CHandle< C_BaseEntity > m_pounceVictim; // 10024.
	CHandle< C_BaseEntity > m_pounceAttacker; // 10028.
	CHandle< C_TerrorPlayer > m_hHunterPounceSoundSource; // 10052.
	CHandle< C_BaseEntity > m_jockeyVictim; // 10056.
	CHandle< C_BaseEntity > m_jockeyAttacker; // 10060.
	EHANDLE m_hUseGlowTarget; // 10068, sole M7 target.
	CNetworkVarEmbedded( IntervalTimer, m_knockdownTimer ); // 10092.
	CHandle< C_SceneEntity > m_hClientExpressionScene; // 10104.
	CountdownTimer m_presserTimer; // 10152.
	CountdownTimer m_blinkTimer; // 10168.
	CountdownTimer m_modelFlexTimer; // 10180.
	CountdownTimer m_combatExpressionTimer; // 10204.
	IntervalTimer m_continuousFireInterval; // 10216, existing fire owner.
	TerrorGlowPlayerState_t m_GlowView[2]; // 10256/10280, actual interval pairs.
	CountdownTimer m_clientMuzzleFlashTimer; // 10304.
	HPARTICLEFFECT m_hCigaretteSmoke; // 10316.
	HPARTICLEFFECT m_hMolotovGroundFire; // 10328.
	HPARTICLEFFECT m_hSmokerSporeTrail; // 10340.
	HPARTICLEFFECT m_hSpitterSlimeTrail; // 10352.
	HPARTICLEFFECT m_hITBodyEffect; // 10364, sole M4 owner.
	HPARTICLEFFECT m_hVomitScreenEffect; // 10376, sole M4 owner.
	HPARTICLEFFECT m_hSmokerSporeAttack; // 10388.
	HPARTICLEFFECT m_hSpeechVoice; // 10400, sole M7 owner.
	HPARTICLEFFECT m_hBurningCharacterScreen; // 10412.
	HPARTICLEFFECT m_hAdrenalineScreen; // 10424.
	bool m_bBandageEffectActive; // 10436, between actual reference groups.
	HPARTICLEFFECT m_hThirdPersonLaserSightParticles[2]; // 10440/10452.
	HPARTICLEFFECT m_hFirstPersonLaserSightParticles[2]; // 10464/10476.
	EHANDLE m_hSmartLookTarget; // 10508, sole M7 state.
	EHANDLE m_hLastSmartLookTarget; // 10512, sole M7 state.
	int m_iVersusTeam; // Native10524, received actual team index; no team-guessing fallback.
	CHandle< C_BaseEntity > m_lookatPlayer; // 10532, received target.
	CountdownTimer m_sceneLookAtTimer; // 10536.
	EHANDLE m_hSceneLookAtTarget; // 10548.

	// Genuine additional POD bindings. Placement is source-owned; descriptive
	// names do not establish an unrecovered public semantic API or byte layout.
	float m_flPerformanceStatsStartTime; // 6784.
	bool m_bTeammateFocusActive; // 7101.
	int m_nTeammateFocusCharacter; // 7132.
	Vector m_vecFlashlightOffset; //7136, whole Simulate writer; OMIT ctor.
	float m_flCurrentEyeHeight; //7148, whole Simulate writer; OMIT ctor.
	int m_nClientLookAtState; // 7152, exact signed32 private state.
	float m_flClientLookAtScale; // 7156.
	Vector m_previousLineOfScrimmagePos; // 7764..7775.
	Vector m_previousRearLineOfScrimmagePos; // 7776..7787.
	Vector m_previousYardLinePos[5]; // 7788..7847.
	Vector m_vecEyeAngularVelocity; // 7932..7943.
	Vector m_vecEyeAngularAcceleration; // 7944..7955.
	QAngle m_angPreviousMotionEyeAngles; // 7956..7967.
	float m_flLastEyeMotionUpdateTime; // 7968.
	QAngle m_angAimEyeAnglesWithPunch; //7972, real252A50 writes BEFORE return.
	float m_flClientMotionState; // 7996.
	float m_flClientInteractionState; // 8008.
	int m_nUseEntity; // 8012, native ctor omission.
	int m_nGlowEntity; // 8016, native ctor omission.
	float m_flGlowNoiseTimestamp; // 8128.
	int m_nGlowNoiseLatch; // 8132.
	int m_iLastSimulatedWeaponID; //9696, local real Simulate writer; OMIT ctor.
	float m_flCurrentThirdPersonCameraDistance; // 9776, sole Move198A owner.
	float m_flObserverEyeHeight; //9780, actual255AA0 writer; OMIT ctor.
	Vector m_vecRoamingObserverLookTarget; // 9788, native ctor omission.
	Vector m_vecThirdPersonCameraOffset; // 9800.
	float m_flUseActionViewYawOffset; // 9812.
	QAngle m_angLatchedUseActionViewAngles; // 9816.
	bool m_bUseActionViewAnglesLatched; // 9828.
	bool m_bRestoreUseActionViewYaw; // 9829.
	float m_flViewBobAmount; //9832, actual253990 writer; OMIT ctor.
	float m_flViewBobTime; // 9836.
	float m_flLastViewBobTime; //9840, actual253990 writer; OMIT ctor.
	int m_nViewBobFrame; // 9844.
	CSoundPatch *m_pHunterPounceSound; // 10048.
	bool m_jockeyFirmAttach; // 10084, not server firmly-attached state.
	const char *m_pszClientExpressionScene; // 10100, borrowed.
	float m_flNextExpressionUpdateTime; // 10108.
	LocalFlexController_t m_angryFlex[3]; // 10112..10123.
	LocalFlexController_t m_fearFlex[3]; // 10124..10135.
	LocalFlexController_t m_deadFlex; // 10136.
	LocalFlexController_t m_eyesUpDownFlex; // 10140.
	LocalFlexController_t m_eyesRightLeftFlex; // 10144.
	LocalFlexController_t m_presserFlex; // 10148.
	float m_flPresserTargetStrength; //10164, real expression writer; OMIT ctor.
	float m_flModelFlexState; // 10192.
	float m_flModelFlexBlend; // 10196.
	int m_nClientExpressionState; //10200, nativeINT32; OMIT ctor.
	float m_flPreviousVomitStart; // 10228, separate from received start.
	dlight_t *m_pSimulationDLight; // 10488, effects-pool ownership.
	float m_flNextGlowSelectionUpdate; // 10492.
	float m_flLastObserverMouseX; //10496, OMIT ctor/reset/schema.
	float m_flLastObserverMouseY; //10500, OMIT ctor/reset/schema.
	float m_flLastObserverInputTime; //10504, OMIT ctor/reset/schema.
	float m_flSmartLookStart; // 10516.
	bool m_bWasAliveLastClientThink; // 10520.
	bool m_bJockeyPredictionSuppressed; // 10528.
	bool m_bWasPresentAtSurvivalStart; // 10552, not Glow permission10553.
	bool m_bNativeTooFarFromSurvivors; // Native client7320; allocation-zero ONLY.
	int m_nDismountPoseParameter; // 10556, native ctor0 bug.
	float m_fLastServerAnimUpdateTime; // 6992, allocation0; no extra ctor reset.
	float m_fServerAnimStartTime; // 6996, distinct from received6788 below.
	int m_NetGestureSequence[GESTURE_SLOT_COUNT];
	int m_NetGestureActivity[GESTURE_SLOT_COUNT];
	float m_NetGestureStartTime[GESTURE_SLOT_COUNT];
	int m_PreviousNetGestureActivity[GESTURE_SLOT_COUNT];
	float m_PreviousNetGestureStartTime[GESTURE_SLOT_COUNT];

	// Actual missing received legacy records; all native ctor-omitted POD.
	float m_mainSequenceStartTime; // 6788; NEVER alias to6996.
	int m_fireLayerSequence; // 6796.
	int m_reloadLayerSequence; // 6800.
	int m_reloadAltLayerSequence; // 6804.
	int m_grenadeLayerSequence; // 6808.
	int m_flinchLayerSequence; // 6812.
	int m_fidgetLayerSequence; // 6816.
	int m_reloadLayerNumShells; // 6820, no extra snapshot.
	float m_fireLayerStartTime; // 6824.
	float m_reloadLayerStartTime; // 6828.
	float m_reloadAltLayerStartTime; // 6832.
	float m_grenadeLayerStartTime; // 6836.
	float m_flinchLayerStartTime; // 6840.
	float m_fidgetLayerStartTime; // 6844.
	int m_PreviousServerGestureSequence[6]; // 7000..7023.
	float m_PreviousServerGestureStartTime[6]; // 7024..7047.

	int m_survivorCharacter;
	int m_zombieClass;
	int m_zombieState;
	int m_iMaxHealth;
	float m_jumpSupressedUntil;
	bool m_airMovementRestricted;
	bool m_isAutoCrouchEnabled;
	float m_flNextShoveTime;
	int m_iShovePenalty;
	float m_flLastShoveTime;
	bool m_bSurvivorPositionHidingWeapons;
	float m_noiseLevel;
	int m_noiseLevelLatch;
	float m_healthBuffer;
	float m_healthBufferTime;
	bool m_isIncapacitated;
	bool m_isCalm;
	bool m_isGoingToDie;
	bool m_bIsOnThirdStrike;
	bool m_bIsFirstManOut;
	bool m_bAdrenalineActive;
	int m_currentReviveCount;
	bool m_isHangingFromLedge;
	bool m_isFallingFromLedge;
	Vector m_hangAirPos;
	Vector m_hangPos;
	Vector m_hangStandPos;
	Vector m_hangNormal;
	bool m_isHangingFromTongue;
	bool m_reachedTongueOwner;
	bool m_isProneTongueDrag;
	bool m_usingMountedGun;
	bool m_usingMountedWeapon;
	bool m_isGhost;
	int m_checkpointIncaps;
	int m_checkpointReviveOtherCount;
	int m_iCurrentUseAction;
	Vector m_staggerStart;
	Vector m_staggerDir;
	float m_staggerDist;
	Vector m_shoveForce;
	float m_bashedStart;
	bool m_isAttemptingToPounce;
	int m_knockdownReason;
	bool m_isCulling;
	bool m_isRelocating;
	float m_pushDistance;
	float m_TimeForceExternalView;
	float m_burnPercent;
	float m_bbqPercent;

	bool m_holdingObject;

	int m_nVariantType;

	int m_checkpointAwardCounts[149];

	int m_missionAwardCounts[149];

	int m_checkpointZombieKills[9];

	int m_missionZombieKills[9];

	int m_checkpointSurvivorDamage;

	int m_missionSurvivorDamage;

	int m_classSpawnCount[9];

	float m_maxDeadDuration;

	float m_totalDeadDuration;

	int m_checkpointMedkitsUsed;

	int m_checkpointPillsUsed;

	int m_missionMedkitsUsed;

	int m_missionPillsUsed;

	int m_checkpointMolotovsUsed;

	int m_missionMolotovsUsed;

	int m_checkpointPipebombsUsed;

	int m_missionPipebombsUsed;

	int m_checkpointBoomerBilesUsed;

	int m_missionBoomerBilesUsed;

	int m_checkpointAdrenalinesUsed;

	int m_missionAdrenalinesUsed;

	int m_checkpointDefibrillatorsUsed;

	int m_missionDefibrillatorsUsed;

	int m_checkpointDamageTaken;

	int m_missionDamageTaken;

	int m_missionReviveOtherCount;

	int m_checkpointFirstAidShared;

	int m_missionFirstAidShared;

	int m_missionIncaps;

	int m_checkpointDamageToTank;

	int m_checkpointDamageToWitch;

	int m_missionAccuracy;

	int m_checkpointHeadshots;

	int m_checkpointHeadshotAccuracy;

	int m_missionHeadshotAccuracy;

	int m_checkpointDeaths;

	int m_missionDeaths;

	int m_checkpointMeleeKills;

	int m_missionMeleeKills;

	int m_checkpointPZIncaps;

	int m_checkpointPZTankDamage;

	int m_checkpointPZHunterDamage;

	int m_checkpointPZSmokerDamage;

	int m_checkpointPZBoomerDamage;

	int m_checkpointPZJockeyDamage;

	int m_checkpointPZSpitterDamage;

	int m_checkpointPZChargerDamage;

	int m_checkpointPZKills;

	int m_checkpointPZPounces;

	int m_checkpointPZPushes;

	int m_checkpointPZTankPunches;

	int m_checkpointPZTankThrows;

	int m_checkpointPZHung;

	int m_checkpointPZPulled;

	int m_checkpointPZBombed;

	int m_checkpointPZVomited;

	int m_checkpointPZHighestDmgPounce;

	int m_checkpointPZLongestSmokerGrab;

	int m_checkpointPZLongestJockeyRide;

	int m_checkpointPZNumChargeVictims;

	int m_checkpointAccuracy; // Native9588 INTERNAL, no transport or invented constructor reset.

	int GetOuttroStatistic( int statistic, int scope ) const;


	Vector m_lineOfScrimmagePos;

	QAngle m_lineOfScrimmageAngle;

	Vector m_rearLineOfScrimmagePos;

	QAngle m_rearLineOfScrimmageAngle;

	Vector m_scrimmageSphereCenter;

	float m_scrimmageSphereInitialRadius;

	int m_scrimmageType;

	float m_scrimmageStartTime;

	float m_survivorsLineOfScrimmageDistance;

	Vector m_yardLinePos[5];

	QAngle m_yardLineAngles[5];

	int m_ghostSpawnState;

	int m_ghostSpawnClockMaxDelay;

	int m_ghostSpawnClockCurrentDelay;

	int m_iBloodyHandsLevel;




	float m_vomitStart;

	float m_vomitFadeStart;

	float GetBloodyHandsBlend() const;

	// EXACT PROVIDED member inline from E9D5; do not add a second out-of-line body. Constructors ONLYSetOwner(this), notm_music(this).
	Music *GetMusic() { return &m_music; }

	// EXACT PROVIDED member inline from E9D5; do not add a second out-of-line body. Constructors ONLYSetOwner(this), notm_music(this).
	const Music *GetMusic() const { return &m_music; }


	Vector m_tugStart;

	Vector m_tugDir;

	float m_tugDist;

	// EXACT supplied shared inline body; CLIENT class spelling follows existing actual paired class. No RTTI/type/life/reciprocity validation, no separate sharedCPP definition.
	C_TerrorPlayer *GetPummelTarget() const { return static_cast< C_TerrorPlayer * >( m_pummelTarget.Get() ); }


	int m_frustration;


	// Reuse exact existing owner body; no alternate cache or weapon policy.
	bool IsWeaponAllowedForTeam( int weaponID ) const;

	Vector m_pounceStartPosition;

	Vector m_jockeyDesiredMove;

	bool m_jockeyRotatedDesire;

	int m_clientIntensity;

	bool m_hasVisibleThreats;

	float m_salivaStart;

	bool m_isInMissionStartArea;

	bool m_bSurvivorGlowEnabled;
private:
	void UpdateGlowSelection();
	C_BaseEntity *FindTankGlowEntity();
	void SetSpeechVoiceParticleEnabled( bool );
	void StopSpeechVoiceParticle();
	void AddViewBob( Vector &, float & );
	void UpdateCigaretteParticle();
	void UpdateSmokerSporeTrail();
	void UpdateSpitterSlimeTrail();
	void UpdateSmokerSporeAttack();
	void UpdateSmokerScreenPulse();
	void UpdateBashedScreenPulse();
	void UpdateBurningCharacterScreen();
	void UpdateAdrenalineScreen();
	void UpdateClientMotionTimers();
	void UpdateHunterPounceSound();
	bool EmitTankAttachmentImpact( int );
	void UpdateTankAttachmentImpacts();
	void UpdateLowPerformanceReports();
	void UpdateRescueAnimation();
	void UpdateSimulationDLight();
	void UpdateLaserSightParticles();
	void UpdateFlashlightParticles();
	void UpdateJockeyAttachmentView();
	int m_nGlowDrawFrame[2]; // Native omitted ctor stores, NO added initialization.
	float m_jockeyAttachmentViewTime; // Native CLIENT10064; full Simulate owns the live writer.
	bool m_bGrenadePresentationSuppressed; // Native CLIENT7100, constructor-only.
	float m_roundStartTime[2];
};

C_TerrorPlayer *GetTerrorPlayerBySurvivorCharacter( int character );
void ResetClientTerrorPlayerPerformanceStats( int reason, int result );

inline C_TerrorPlayer *ToTerrorPlayer( C_BaseEntity *pEntity )
{
	return pEntity && pEntity->IsPlayer() ? static_cast< C_TerrorPlayer * >( pEntity ) : NULL;
}

#define CTerrorPlayer C_TerrorPlayer
#endif // C_TERRORPLAYER_H
