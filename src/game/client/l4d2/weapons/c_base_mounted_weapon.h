//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef C_BASE_MOUNTED_WEAPON_H
#define C_BASE_MOUNTED_WEAPON_H
#ifdef _WIN32
#pragma once
#endif

#include "c_props.h"

class C_TerrorPlayer;
class CNewParticleEffect;

// The mounted family is a dynamic prop, not an inventory weapon.
class C_BaseMountedWeapon : public C_DynamicProp
{
public:
	DECLARE_CLASS( C_BaseMountedWeapon, C_DynamicProp );
	DECLARE_CLIENTCLASS();

	C_BaseMountedWeapon();
	virtual ~C_BaseMountedWeapon();

	C_TerrorPlayer *GetUser() const;
	const QAngle &GetInitialAngles() const { return m_initialAngles; }
	void GetControlAngles( QAngle *pInitial, float *pMinYaw, float *pMaxYaw, float *pMinPitch, float *pMaxPitch ) const;
	bool IsFiring() const { return m_firing; }
	bool IsOverheated() const { return m_overheated; }
	float GetHeat() const { return m_heat; }

	virtual void GetUserPosition( Vector *pPosition );
	void ClampViewAngles( QAngle &angles ) const;

protected:
	CStudioHdr *CacheMountedModel();
	void UpdateMountedPose();
	void UpdateOverheatEffect();
	CNewParticleEffect *FindMountedEffect( const char *pName );

	QAngle m_initialAngles;
	CHandle< C_BaseEntity > m_owner;
	float m_maxYaw;
	float m_maxPitch;
	float m_minPitch;
	bool m_firing;
	bool m_overheated;
	float m_heat;

	int m_iHorizontalPose;
	int m_iVerticalPose;
	float m_flHorizontalPose;
	float m_flVerticalPose;
	int m_iBarrelBone;
	int m_iLinkAttachment;
	int m_iShellAttachment;
	int m_iMuzzleAttachment;

private:
	float ApproachMountedPose( int iPose, float current, float target, float speed );
	C_BaseMountedWeapon( const C_BaseMountedWeapon & );
};

#define CBaseMountedWeapon C_BaseMountedWeapon

#endif // C_BASE_MOUNTED_WEAPON_H
