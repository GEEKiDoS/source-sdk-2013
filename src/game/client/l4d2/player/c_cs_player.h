//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_CS_PLAYER_H
#define C_CS_PLAYER_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseplayer.h"
#include "l4d2/cs_shareddefs.h"
#include "Multiplayer/multiplayer_animstate.h"
#include "tier1/utllinkedlist.h"
#include "particles_new.h"
class C_AddonProp;

class C_CSPlayer : public C_BasePlayer
{
public:
	DECLARE_CLASS( C_CSPlayer, C_BasePlayer );
	DECLARE_CLIENTCLASS();
	DECLARE_PREDICTABLE();

	C_CSPlayer();
	virtual ~C_CSPlayer();
	virtual void PreThink() OVERRIDE;
	virtual void RecalculateMaxSpeed();
	virtual bool CreateMove( float inputSampleTime, CUserCmd *cmd ) OVERRIDE;
	virtual const QAngle &EyeAngles();
	virtual void GetRagdollInitBoneArrays( matrix3x4_t *pDeltaBones0, matrix3x4_t *pDeltaBones1,
		matrix3x4_t *pCurrentBones, float boneDt ) OVERRIDE;
	// C2223A0: player corpses are owned by survivor_death_model or cs_ragdoll.
	virtual C_BaseAnimating *BecomeRagdollOnClient( int category ) OVERRIDE { return NULL; }
	CSPlayerState State_Get() const;
	virtual bool IsBusy() const;
	virtual bool CanAttack() const; // C225410, genuine CS introducer; Terror26C880 overrides.
	virtual bool IsProgressBarActive() const;
	float GetStamina() const { return m_flStamina; }
	void SetStamina( float flStamina ) { m_flStamina = flStamina; }
	float GetVelocityModifier() const { return m_flVelocityModifier; }
	void SetVelocityModifier( float flModifier ) { m_flVelocityModifier = flModifier; }
	bool IsDuckUntilOnGround() const { return m_duckUntilOnGround; }
	void SetDuckUntilOnGround( bool bDuck ) { m_duckUntilOnGround = bDuck; }
	void RecordLastStandingInfo( const Vector &position, const Vector &velocity, const QAngle &angles );
	void RecordLadderInfo( const Vector &position, const Vector &normal, const Vector &velocity, const QAngle &angles );
	double GetTimeSinceLadderInteraction() const;
	bool CanRegrabLadder( const Vector &position, const Vector &normal ) const;
	const Vector &GetLastLadderNormal() const { return m_lastLadderNormal; }
	friend class CCSGameMovement;
	static C_CSPlayer *GetLocalCSPlayer();
	static C_CSPlayer *GetViewCSPlayer();
	virtual void UpdateClientSideAnimation() OVERRIDE;
	virtual void ProcessMuzzleFlashEvent() OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual void NotifyShouldTransmit( ShouldTransmitState_t state ) OVERRIDE;
	virtual void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void FireEvent( const Vector &, const QAngle &, int, const char * ) OVERRIDE;
	// C22DB20/C22DB30 are genuine CCS RET leaves. Animation/water events,
	// not the inherited movement/rung step emitter, own Terror footstep audio.
	virtual void UpdateStepSound( surfacedata_t *, const Vector &, const Vector & ) OVERRIDE {}
	virtual void PlayStepSound( Vector &, surfacedata_t *, float, bool ) OVERRIDE {}
	virtual float GetWalkTopSpeed() const;
	virtual void UpdateCollisionBounds();
	virtual void OnClientFootstepEvent();
	virtual void GetFootstepSoundPosition( bool, bool, bool, Vector *, bool *, bool * );
	virtual surfacedata_t *GetTerrorFootstepSurface( int &surfaceIndex ) OVERRIDE;
	virtual bool IsTerrorFootstepSurfaceValid( float, bool ) const OVERRIDE;
	void UpdateAddonModels();
	void UpdateShadowWeapon();
	void RemoveAddonModels();
	void CreateAddonModel( int slot );
	void ClearFlashlightParticles();
	int GetIDTarget() const;
	virtual void SetServerIntendedCycle( float ) OVERRIDE;
	virtual float GetServerIntendedCycle() OVERRIDE;
	static void RecvProxy_HasDefuser( const CRecvProxyData *, void *, void * );
	static void RecvProxy_NightVisionOn( const CRecvProxyData *, void *, void * );
	static void RecvProxy_FlashDuration( const CRecvProxyData *, void *, void * );
	static void RecvProxy_CycleLatch( const CRecvProxyData *, void *, void * );
	static void RecvProxy_LocalOriginXY( const CRecvProxyData *, void *, void * );
	static void RecvProxy_LocalOriginZ( const CRecvProxyData *, void *, void * );
	static void RecvProxy_NonLocalOriginXY( const CRecvProxyData *, void *, void * );
	static void RecvProxy_NonLocalOriginZ( const CRecvProxyData *, void *, void * );
	virtual bool Simulate() OVERRIDE;
	virtual bool AddEntity() OVERRIDE;
	virtual bool IsThirdPersonCameraActive() const OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE;
	virtual const Vector &GetRenderOrigin() OVERRIDE;
	virtual const QAngle &GetRenderAngles() OVERRIDE;
	virtual void GetShadowRenderBounds( Vector &mins, Vector &maxs, ShadowType_t shadowType ) OVERRIDE;
	virtual bool GetShadowCastDirection( Vector *direction, ShadowType_t shadowType ) const OVERRIDE;

	CUtlVector< C_BaseEntity * > m_SmokeGrenades; //6224, FIRST CCS member lifetime.
	CHandle< C_BaseEntity > m_hRagdoll; //6332, MOVED once after smoke.
	int m_iPlayerState;
	QAngle m_angEyeAngles;
	float m_flStamina;
	int m_iDirection;
	int m_iShotsFired;
	bool m_bResumeZoom; // C6252: paired native Deploy-only reset.
	int m_iLastZoom; // C6256.
	float m_flVelocityModifier;
	bool m_duckUntilOnGround;
	float m_flProgressBarDuration;
	float m_flProgressBarStartTime;
	int m_iDeathPose;
	int m_iDeathFrame;

	CNetworkVar( int, m_iAccount );
	CNetworkVar( bool, m_bInBombZone );
	CNetworkVar( bool, m_bInBuyZone );
	CNetworkVar( bool, m_bInHostageRescueZone );
	CNetworkVar( int, m_iThrowGrenadeCounter );
	CNetworkVar( int, m_iNightVisionScale );


	bool m_bHasDefuser;

	bool m_bNightVisionOn;

	bool m_bHasNightVision;

	bool m_bHasHelmet;

	bool m_bIsDefusing;

	int m_iClass;

	int m_cycleLatch;

	float m_flFlashDuration;

	float m_flFlashMaxAlpha;

	int m_ArmorValue;

	int m_iAddonBits;

	int m_AddonModelWeaponIDs[5];
	int m_nClientPresentationState; //6248, ctor1, no invented API.
	float m_flNightVisionAlpha; //6344, OMIT ctor.
	float m_flFlashOverlayAlpha; //6348, OMIT ctor.
	float m_flFlashEndTime; //6352, OMIT ctor.
	Vector m_vecLastReceivedPosition; //6400, real local proxy writes.
	float m_flNextClientPushawayTime; //6524, ctor0.
	Vector m_vecRagdollVelocity; //6528, real receive; OMIT ctor.
protected:
	CMultiPlayerAnimState *m_PlayerAnimState; // Native6244; Terror constructs, CCS releases.
	Vector m_lastStandingPosition;
	Vector m_lastStandingVelocity;
	QAngle m_lastStandingAngles;
	IntervalTimer m_lastStandingTimer;
	IntervalTimer m_ladderInteractionTimer;
	Vector m_lastLadderNormal;
	Vector m_lastLadderPosition;
	Vector m_lastLadderVelocity;
	QAngle m_lastLadderAngles;

	bool m_bUseSimpleShadow[2]; // Native6476/6477; ctorfalse, user0 consumer.
	CountdownTimer m_shadowDistanceTimer; //6480, ONE ctor RandomFloat(0,.5).
private:
	CInterpolatedVar< QAngle > m_iv_angEyeAngles; //6540, sole existing watcher MOVED.
protected:
	void ProcessDelayedSounds();
	void UpdateIDTarget();
	void UpdateShadowDistanceState();
	struct DelayedSound_t { const char *m_soundName; float m_flPlaybackTime; };
	struct AddonModel_t
	{
		CHandle< C_AddonProp > m_hModel;
		int m_slot;
		int m_attachment;
		int m_weaponID;
	};
	int m_iIDEntIndex; //6584, actual ctor0.
	CountdownTimer m_IDTargetDelayTimer; //6588.
	int m_iOldIDEntIndex; //6600, actual ctor0.
	CountdownTimer m_IDTargetHoldTimer; //6604.
	int m_nFlashlightCleanupGate; //6616, actual ctor0; NotifyShouldTransmit gate.
	HPARTICLEFFECT m_hThirdPersonFlashlightParticles[2]; //6620/6632, default ONCE.
	HPARTICLEFFECT m_hFirstPersonFlashlightParticles[2]; //6644/6656, default ONCE.
	CUtlLinkedList< DelayedSound_t, int > m_DelayedSounds; // Native C225450/C226CE0 +0x1A18 (6680).
	CUtlLinkedList< AddonModel_t, int > m_AddonModels; //6708.
	int m_nAddonModelState; //6760, OMIT ctor.
	float m_flServerIntendedCycle; //6768, actual ctor-1.
	CHandle< C_BaseAnimating > m_hShadowWeapon; //6772.
	bool m_bFootstepPending[2]; //6776/6777, actual ctorfalse.
};

inline C_CSPlayer *ToCSPlayer( C_BaseEntity *pEntity )
{
	return pEntity && pEntity->IsPlayer() ? static_cast< C_CSPlayer * >( pEntity ) : NULL;
}

#define CCSPlayer C_CSPlayer
#endif // C_CS_PLAYER_H
