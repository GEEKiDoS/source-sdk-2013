//========= Copyright Valve Corporation, All rights reserved. ============//
//
// First-person legs: a client-only copy of the local survivor's third-person model, drawn in the
// local main view with its upper body collapsed, while the real (hidden) player only casts shadows.
//
//=============================================================================//
#ifndef C_TERROR_FP_LEGS_H
#define C_TERROR_FP_LEGS_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimatingoverlay.h"
#include "bitvec.h"

class C_TerrorPlayer;

class C_TerrorFirstPersonLegs : public C_BaseAnimatingOverlay
{
public:
	DECLARE_CLASS( C_TerrorFirstPersonLegs, C_BaseAnimatingOverlay );

	static C_TerrorFirstPersonLegs *Create( C_TerrorPlayer *player );

	virtual bool ShouldDraw() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual bool SetupBones( matrix3x4_t *bones, int maximum, int mask, float time ) OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; }
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void BuildTransformations( CStudioHdr *studio, Vector *pos, Quaternion q[], const matrix3x4_t &cameraTransform,
		int boneMask, CBoneBitList &computed ) OVERRIDE;

private:
	explicit C_TerrorFirstPersonLegs( C_TerrorPlayer *player );
	// Copies the player's model, animation state and body transform; once per frame.
	void SyncFromPlayer();
	void FindHiddenBones();

	CHandle< C_TerrorPlayer > m_hPlayer;
	int m_nSyncFrame;
	// Bones collapsed by BuildTransformations: the cut bone and every descendant (chest, arms, neck, head).
	int m_nCutBone;
	int m_nCutBoneGeneration; // cl_fp_legs_cut_bone changes rebuild m_HiddenBones
	CBitVec< MAXSTUDIOBONES > m_HiddenBones;
};

#endif // C_TERROR_FP_LEGS_H
