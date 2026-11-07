//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_CS_RAGDOLL_H
#define C_CS_RAGDOLL_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimatingoverlay.h"

class C_CSRagdoll : public C_BaseAnimatingOverlay
{
public:
	DECLARE_CLASS( C_CSRagdoll, C_BaseAnimatingOverlay );
	DECLARE_CLIENTCLASS();

	C_CSRagdoll();
	virtual ~C_CSRagdoll();
	virtual void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual datamap_t *GetDataDescMap() OVERRIDE { return NULL; } // C221C60; primary17 is metadata, NOT ShouldDraw.
	virtual int DrawModel( int flags ) OVERRIDE { return 0; } // C224B90.
	virtual ShadowType_t ShadowCastType() OVERRIDE { return SHADOWS_NONE; } // C224B80.
	bool IsRagdollCreated() const { return m_bCreated; } // C223680 reads native3040.

private:
	void CreateCSRagdoll();
	void CreateLowViolenceRagdoll();
	C_BaseAnimating *CreateRagdollFromModel( int category, int modelIndex, const Vector &force );

	CHandle< C_BaseEntity > m_hPlayer; //2976; preserve serial-checked IsPlayer test.
	Vector m_vecRagdollVelocity; //2980.
	Vector m_vecRagdollOrigin; //2992.
	int m_iDeathPose; //3004.
	int m_iDeathFrame; //3008.
	bool m_bOnFire; //3012.
	bool m_bInterpOrigin; //3013; wire value, no native reader in this class.
	int m_ragdollType; //3016; signed four-bit wire value, never bool/clamped.
	int m_zombieClass; //3020.
	int m_survivorCharacter; //3024; wire value, no native reader here.
	int m_nActualModelIndex; //3032; low-violence write, no native reader.
	bool m_bCreated; //3040.
	CHandle< C_BaseAnimating > m_hCreatedRagdoll; //3044; weak handle, not an owning corpse.
};

#endif // C_CS_RAGDOLL_H
