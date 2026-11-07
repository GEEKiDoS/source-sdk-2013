//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PLAYERANIMSTATE_H
#define TERROR_PLAYERANIMSTATE_H
#ifdef _WIN32
#pragma once
#endif

#include "Multiplayer/multiplayer_animstate.h"
#include "l4d2/terror_shareddefs.h"

#ifdef CLIENT_DLL
class C_TerrorPlayer;
#define CTerrorPlayer C_TerrorPlayer
#else
class CTerrorPlayer;
#endif

// Healthy survivor locomotion/aim uses Terror's update, not the generic
// multiplayer update. The base class owns the seven native gesture slots.
class CTerrorPlayerAnimState : public CMultiPlayerAnimState
{
public:
#if defined( CLIENT_DLL ) && defined( L4D2 )
	bool IsGrenadePresentationSuppressed();
#endif

	DECLARE_CLASS( CTerrorPlayerAnimState, CMultiPlayerAnimState );

	explicit CTerrorPlayerAnimState( CTerrorPlayer *pPlayer );
	virtual void ClearAnimationState() OVERRIDE;
	virtual void Update( float eyeYaw, float eyePitch ) OVERRIDE;
	virtual void RestartMainSequence() OVERRIDE;
	Activity GetTerrorActivity() const { return m_eCurrentMainSequenceActivity; }
	double GetEyeYaw() const { return m_flEyeYaw; } // Actual inherited eye-yaw228, not feet yaw or actor EyeAngles.
	virtual void GetAnimationMovement( Vector *pMovement ) const;
	virtual void ResetAnimationMovement();
	bool IsLedgeClimbAnimation();
	void DoTerrorAnimationEvent( TerrorPlayerAnimEvent event, int nData = 0 );
	bool IsGrenadeProjectilePresentationActive();
	virtual void FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options );
#ifdef CLIENT_DLL
	void OnReceivedGesture( int iGestureSlot, Activity activity );
	bool IsAnimationBusy() const;
#else
	void RestartMainSequenceIfUnlocked();
	int GetSequenceOverride();
	void ClearSequenceOverride();
	void ForceSequenceOverride( int sequence );
	bool IsUseGestureActive() const { return m_bUseGestureActive; }
	bool IsAnimationBusy() const { return m_nStaggerDirection != -1; }
	bool IsChargerImpactAnimation() const { return m_bChargerImpact; }
	bool IsUseActionAnimationBlocked() const { return m_bChargerPounded; } // S33C0F0, native349.
#endif

protected:
	virtual Activity TranslateActivity( Activity activity ) OVERRIDE;
	virtual bool HandleJumping( Activity &idealActivity ) OVERRIDE;
	virtual bool HandleDucking( Activity &idealActivity ) OVERRIDE;
	virtual bool HandleMoving( Activity &idealActivity ) OVERRIDE;
	virtual bool HandleSwimming( Activity &idealActivity ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual Activity CalcMainActivity() OVERRIDE;
	virtual void OnGestureStarted( int iGestureSlot, Activity activity, int iSequence ) OVERRIDE;
	virtual void ComputeMainSequence() OVERRIDE;
	virtual void OnGestureStopped( int iGestureSlot ) OVERRIDE;
#else
	virtual int SelectGestureSequence( int iGestureSlot, Activity activity, CBasePlayer *pPlayer ) OVERRIDE;
#endif
	virtual Activity UntranslateActivity( Activity activity ) OVERRIDE { return UntranslateTerrorActivity( activity ); }
	virtual void ComputeSequences( CStudioHdr *pStudioHdr ) OVERRIDE;
	virtual void ComputePoseParam_MoveYaw( CStudioHdr *pStudioHdr ) OVERRIDE;
	virtual void ComputePoseParam_AimPitch( CStudioHdr *pStudioHdr ) OVERRIDE;
	virtual void ComputePoseParam_AimYaw( CStudioHdr *pStudioHdr ) OVERRIDE;
	virtual double GetCurrentMaxGroundSpeed() OVERRIDE;

private:
	CTerrorPlayer *GetTerrorPlayer();
#ifndef CLIENT_DLL
	TerrorOriginalActivity GetOriginalMainActivity() const { return LookupTerrorOriginalActivity( m_eCurrentMainSequenceActivity ); }
	void ResetUpperBodyPoseParameters();
	void UpdateChainsawGestures();
	void UpdateIdleGesture();
	Activity CalcTerrorDyingActivity();
	Activity CalcReturnToLifeActivity();
	Activity CalcSpitActivity();
	Activity CalcChargerPummelActivity();
	Activity CalcChargerPummelVictimActivity();
	Activity CalcChargerImpactActivity();
	Activity CalcChargerSlammedWallActivity();
	Activity CalcChargerSlammedGroundActivity();
	Activity CalcCarriedActivity();
	Activity CalcTankPunchActivity();
	Activity CalcTongueVictimActivity();
	Activity CalcPounceVictimActivity();
	Activity CalcJockeyVictimActivity();
	Activity CalcSpecialMoveActivity();
	Activity CalcHunterPounceActivity();
	Activity CalcJockeyRideActivity();
	Activity CalcSmokerTongueActivity();
	Activity CalcIncapacitatedActivity();
	Activity CalcLadderActivity();
	Activity CalcPushActivity();
	Activity CalcStaggerActivity();
#endif
#ifdef CLIENT_DLL
	void UpdateReceivedMainSequence();
#endif
	bool SetupSurvivorPoseParameters( CStudioHdr *pStudioHdr );
	void UpdateAnimationMovement( CStudioHdr *pStudioHdr );
	void EstimateFeetYaw();
	bool ShouldFaceAim();
	double GetFeetYawRateScale();
	bool ShouldLockEyeAngles();
	void UpdateEyeYawFromAction();
	double GetLadderDismountPose();
	void UpdatePlaybackAndSpecialPoses( CStudioHdr *pStudioHdr );
	double ReadDebugPoseParameter( int nPoseParameter, bool bPhysical );
	void ShowTerrorDebugState( int nPlayerIndex, CStudioHdr *pStudioHdr, bool bDump );
	static void DumpAllTerrorAnimationStates();

	int m_nHeadPitchPoseParameter;
	int m_nHeadYawPoseParameter;
	int m_nLeanXPoseParameter;
	int m_nLeanYPoseParameter;
	int m_nDismountPoseParameter;
	int m_nTongueAnglePoseParameter;
	int m_nVerticalVelocityPoseParameter;
	int m_nHorizontalVelocityPoseParameter;
	int m_nJockeyXPoseParameter;
	int m_nJockeyYPoseParameter;
	float m_flAimUpdateMultiplier;
	float m_flPreviousMovementCycle;
	Vector m_vecAnimationMovement;
	float m_flJockeyMoveX; // Native S300/C348; retained between ride updates.
	float m_flJockeyMoveY; // Native S304/C352; cleared only with animation state.
	bool m_bWasAirborne; // Native S332/C380, separate from event7's base latch.
	float m_flAirborneStartTime; // Native S336/C384.
#ifndef CLIENT_DLL
	bool m_bSequenceOverride; // Native 340; distinct from the low-level restart.
	bool m_bTerrorDying; // Native 341, independent of the base dying latch.
	bool m_bUseGestureActive; // Native 342, independent of main-sequence lock.
	bool m_bReturnToLifePending; // Native 343.
	bool m_bChargerImpact; // Native 344.
	bool m_bChargerPummeled;
	bool m_bChargerSlammedWall;
	bool m_bChargerSlammedGround;
	bool m_bChargerPummelVictimNotified;
	bool m_bChargerPounded;
	bool m_bChargerCarried; // Native clear/constructor deliberately omit 350.
	bool m_bHitByTank;
	bool m_bSpitting;
	bool m_bPounced;
	bool m_bTongueVictim;
	bool m_bChainsawStartup;
	bool m_bHunterPounce;
	bool m_bJockeyRide;
	bool m_bJockeyRidden;
	bool m_bTongueAttack;
	bool m_bLunging;
	bool m_bChargerPummel;
	bool m_bChargerPound;
	bool m_bTongueChoke;
	bool m_bTongueWallHit;
	int m_nThrowAnimation;
	int m_nClimbAnimation;
	int m_nTankSpecialAnimation;
	int m_nStaggerDirection;
	bool m_bFidgetDisabled;
	CountdownTimer m_fidgetTimer;
#endif
	CountdownTimer m_grenadePresentationTimer; // Native S400/C392, local QC event state.
};

#ifndef CLIENT_DLL
// Original event ordinals belong only to this wire boundary, not SDK enums.
void TE_TerrorPlayerAnimEvent( CTerrorPlayer *pPlayer, int nOriginalEvent, int nData );
#endif

#endif // TERROR_PLAYERANIMSTATE_H
