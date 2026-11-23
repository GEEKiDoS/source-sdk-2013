//========= Copyright Valve Corporation, All rights reserved. ============//
//
// First-person body: client-only copies of the local survivor's third-person model. The legs copy draws in the
// local main view with its upper body collapsed; the shadow copy (with a copy of the world weapon) draws only into
// native sun/local-light shadowmaps. Both sit the same distance behind the camera, so the shadow starts at the
// visible feet. The real player stays hidden and casts nothing in first person.
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

class C_TerrorFirstPersonBody : public C_BaseAnimatingOverlay
{
public:
	DECLARE_CLASS( C_TerrorFirstPersonBody, C_BaseAnimatingOverlay );

	enum Role { ROLE_LEGS, ROLE_SHADOW };
	static C_TerrorFirstPersonBody *Create( C_TerrorPlayer *player, Role role );

	// The player's lower body matches the first-person camera (standing, crouching, walking): legs show, and both
	// copies back off cl_fp_legs_offset. Otherwise (crawling, hanging, pinned, climbing) the shadow copy stays in place.
	static bool LegsPose( C_TerrorPlayer *player );
	// The shadow copy and its weapon cast (cl_fp_self_shadow, native shadowmaps running, first-person local body).
	static bool CastsSelfShadow( C_TerrorPlayer *player );

	virtual bool ShouldDraw() OVERRIDE;
	virtual bool ShouldCastShadowWhileHidden() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual bool SetupBones( matrix3x4_t *bones, int maximum, int mask, float time ) OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; }
	virtual CStudioHdr *OnNewModel() OVERRIDE;
	virtual void BuildTransformations( CStudioHdr *studio, Vector *pos, Quaternion q[], const matrix3x4_t &cameraTransform,
		int boneMask, CBoneBitList &computed ) OVERRIDE;

private:
	C_TerrorFirstPersonBody( C_TerrorPlayer *player, Role role );
	// Copies the player's model, animation state and body transform; once per frame.
	void SyncFromPlayer();
	void FindHiddenBones();

	CHandle< C_TerrorPlayer > m_hPlayer;
	Role m_Role;
	int m_nSyncFrame;
	// Legs only: for each bone, the hidden subtree root (head, arms) it collapses to, or -1; and the subtree leaned
	// back from the lean bone before the collapse.
	short m_CollapseRoot[MAXSTUDIOBONES];
	int m_nFirstHiddenBone, m_nLeanBone;
	int m_nBoneNamesGeneration; // cl_fp_legs_hide_bones / cl_fp_legs_lean_bone changes rebuild the bone sets
	CBitVec< MAXSTUDIOBONES > m_LeanBones;
	Vector m_vecStretchTarget; // where hidden bones collapse with cl_fp_legs_stretch (behind the camera)
};

// The active weapon's world model bone-merged onto the shadow copy; shadow passes only.
class C_TerrorFirstPersonShadowWeapon : public C_BaseAnimating
{
public:
	DECLARE_CLASS( C_TerrorFirstPersonShadowWeapon, C_BaseAnimating );

	// Creates, replaces or releases `weapon` so it shows the player's active world weapon on `body`.
	static void Maintain( CHandle< C_TerrorFirstPersonShadowWeapon > &weapon, C_TerrorPlayer *player, C_TerrorFirstPersonBody *body );

	virtual bool ShouldDraw() OVERRIDE { return false; }
	virtual bool ShouldCastShadowWhileHidden() OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; }

private:
	explicit C_TerrorFirstPersonShadowWeapon( C_TerrorPlayer *player ) : m_hPlayer( player ), m_nWorldModelIndex( -1 ) {}

	CHandle< C_TerrorPlayer > m_hPlayer;
	int m_nWorldModelIndex; // world model index it was created for
};

#endif // C_TERROR_FP_LEGS_H
