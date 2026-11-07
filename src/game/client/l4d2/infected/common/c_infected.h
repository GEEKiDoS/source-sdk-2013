//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_INFECTED_H
#define C_INFECTED_H
#ifdef _WIN32
#pragma once
#endif

#include "NextBot/C_NextBot.h"
#include "l4d2/effects/terror_material_proxy_state.h"
#include "mathlib/vector4d.h"
#include "util_shared.h"

class C_Infected;

// The server sends the start of an overlay, not its per-frame cycle or weight.
class C_InfectedAnimationLayer
{
public:
	DECLARE_CLASS_NOBASE( C_InfectedAnimationLayer );

	C_InfectedAnimationLayer();
	void ReceiveSequence( int nSequence );
	void ReceiveStartTime( float flStartTime );
	void ReceiveOrder( int nOrder );
	void Update( CStudioHdr *pStudioHdr );
	int DrawDebug( C_AnimationLayer *layer, int line ) const;

	int m_nSequence;
	float m_flStartTime;
	int m_nOrder;
	bool m_bLooping;

	C_Infected *m_pOwner;
	int m_nIndex;

private:
	friend class C_Infected;
	int m_nActivity;
	void StartFade( float flDuration );
	void ResetOverlay();
	float m_flNetworkStartTime;
	bool m_bDirty;
};

struct InfectedWoundRenderData_t
{
	InfectedWoundRenderData_t();

	bool m_bEnabled;
	Vector m_vecPosition;
	Vector m_vecAxis[3];
	Vector4D m_vecUVMapping;
	Vector2D m_vecUVScale;
};

// This state belongs to an actor/corpse, never to its shared IMaterial.
struct InfectedRenderState_t
{
	InfectedRenderState_t();

	int m_nSkinTint;
	int m_nClothingTint;
	int m_nBloodMask;
	float m_flBurnStartTime;
	bool m_bIsBurning;
	int m_iRequestedWound[2];
	int m_iWound[2];
	int m_nOriginalBody;
	int m_nModelType;
	InfectedWoundRenderData_t m_Wound[2];
};

void GetInfectedBloodAndBurn( const InfectedRenderState_t &state, int &bloodSheet, float &burnStrength );

class C_Infected : public C_NextBotCombatCharacter
{
public:
	DECLARE_CLASS( C_Infected, C_NextBotCombatCharacter );
	DECLARE_CLIENTCLASS();

	C_Infected();
	virtual ~C_Infected();
	virtual CStudioHdr *OnNewModel();
	virtual void OnSequenceChanged() OVERRIDE; // C313B60, inherited by Witch.
	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual void PostDataUpdate( DataUpdateType_t updateType ) OVERRIDE;
	virtual void NotifyShouldTransmit( ShouldTransmitState_t state );
	virtual void UpdateOnRemove() OVERRIDE;
	virtual void UpdateClientSideAnimation();
	virtual C_Infected *MyInfectedPointer() OVERRIDE;
	virtual int GetZombieClass() const OVERRIDE { return 0; } // C315990.
	virtual bool Simulate() OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE;
	virtual C_BaseAnimating *BecomeRagdollOnClient( int category );
	virtual void FireEvent( const Vector &origin, const QAngle &angles, int event, const char *options ) OVERRIDE;
	virtual const char *GetTerrorFootstepSoundName( const char *surfaceSound, bool rightFoot, float speed, bool force ) OVERRIDE;
	virtual const char *DamageDecal( int bitsDamageType, int gameMaterial ) OVERRIDE;
	virtual void AddDecal( const Vector &rayStart, const Vector &rayEnd, const Vector &decalCenter,
		int hitbox, int decalIndex, bool doTrace, trace_t &tr, int maxLODToDecal = ADDDECAL_TO_ALL_LODS ) OVERRIDE;

	const InfectedRenderState_t &GetInfectedRenderState();
	float GetLastDormantTime() const { return m_flLastDormantTime; }
	void ReceiveSequenceStartTime( float flStartTime );
	void ResizeClientAnimatedOverlays( int nCount );

private:
	friend class C_InfectedAnimationLayer;
	void UpdateBaseAnimation( CStudioHdr *studio );
	void UpdateForwardViewOffset();
	void PrepareGibBudgetForSimulate();
	void UpdateInfectedAnimation( bool latch, bool inView );
	void DrawInfectedAnimationDebug( CStudioHdr *studio );
	void UpdateMovementPose( const Vector &vecLocalVelocity, bool bIterate );
	void UpdateWounds();
	void EmitGibs( C_BaseAnimating *pSource, bool bEmitParticles );
	void ReleaseWoundModels();
	void TransferWoundModels( C_BaseAnimating *pRagdoll );
	void UpdateUncommonModels();
	void ReleaseUncommonItemModels();
	void ReleaseUncommonModels();
	int GetBodyActivityFlags();
	CUtlVector<C_InfectedAnimationLayer> m_clientAnimatedOverlay;
	CountdownTimer m_forwardViewTimer;
	int m_iForwardAttachment;
	int m_iMoveX;
	int m_iMoveY;
	int m_iBodyYaw;
	int m_iBodyPitch;
	int m_iLeanYaw;
	int m_iLeanPitch;
	float m_flNominalGroundSpeed;
	Vector m_gibbedLimbForce;
	int m_originalBody;
	bool m_mobRush;
	bool m_bIsBurning;
	// Existing renderer projection owns the single native burn-interval timestamp.
	InfectedRenderState_t m_RenderState;
	int m_nFallenFlags;
	CHandle<C_BaseAnimating> m_hWoundModel[2];
	int m_iRequestedWound1;
	int m_iRequestedWound2;
	bool m_bWoundDecalsDirty;
	Activity m_currentActivity;
	float m_sequenceStartTime;
	bool m_bSequenceStartTimeChanged;
	float m_flLastDormantTime;
	CUtlVector<int> m_GibQueue;
	int m_nNextGib;
	bool m_bRagdollTransferred;
	C_BaseAnimating *m_pFaceplate;
	C_BaseAnimating *m_pUncommonItem[4];
	C_BaseAnimating *m_pUncommonPocket[4];
	CHandle<C_BaseEntity> m_clientLookatTarget;
	float m_clientLeanYaw;
	CInterpolatedVar<float> m_iv_clientLeanYaw;
	int m_nLastSequence;
	int m_nActivityFlags;
protected:
	virtual void UpdatePoseParameters(); // Native extra virtual247, C313C60.
	void SetAllowWounds( bool allow ) { m_allowWounds = allow; }
	Activity GetCurrentInfectedActivity() const { return m_currentActivity; }
	bool HasTransferredRagdoll() const { return m_bRagdollTransferred; }
	bool m_allowWounds;
};

// Native client ragdoll simulation, fading and save ownership are retained.
class C_TerrorClientRagdoll : public C_ClientRagdoll
{
public:
	DECLARE_CLASS( C_TerrorClientRagdoll, C_ClientRagdoll );
	DECLARE_DATADESC();

	C_TerrorClientRagdoll( bool bRestoring = true );
	virtual void Release() OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void SUB_Remove() OVERRIDE;
	void CopySourceState( const InfectedRenderState_t &state );
	void AcceptWoundModel( int nWound, C_BaseAnimating *pModel );
	void AcceptFaceplate( C_BaseAnimating *model );
	bool HasInfectedState() const { return m_bHasInfectedState; }
	const InfectedRenderState_t &GetInfectedRenderState() const { return m_RenderState; }

private:
	bool m_bHasInfectedState;
	float m_flWoundParticleTime;
	InfectedRenderState_t m_RenderState;
	CHandle<C_BaseAnimating> m_hWoundModel[2];
	// The global client list can retire this transferred child before its corpse.
	CHandle<C_BaseAnimating> m_hFaceplate;
};

C_ClientRagdoll *CreateTerrorClientRagdoll( C_BaseAnimating *pSource );
const InfectedRenderState_t *GetInfectedRenderState( C_BaseEntity *pEntity );
void EmitInfectedWoundParticle( C_BaseAnimating *pEntity, int nModelType, int nWound, const Vector &vecForce );

#endif // C_INFECTED_H
