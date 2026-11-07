//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef PROP_MINIGUN_H
#define PROP_MINIGUN_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/base_mounted_weapon.h"

// The L4D1 rotating-barrel gun is not a CPropMountedGun (.50 caliber).
class CPropMinigun : public CBaseMountedWeapon
{
public:
	DECLARE_CLASS( CPropMinigun, CBaseMountedWeapon );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CPropMinigun();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual void Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value ) OVERRIDE;
	virtual void GetUserPosition( Vector *pPosition ) OVERRIDE;

private:
	void AngleThink();
	void FireThink();
	void UpdateSpin( float interval );
	void UpdateHeat( float interval );
	void FireBullet( CTerrorPlayer *pPlayer, const Vector &vecEyePosition, const QAngle &angles, float coneSpread, bool bLastBullet );

	QAngle m_lastFireAngles;
	float m_targetSpinSpeed;
	float m_barrelRotation;
	COutputEvent m_OnFireStart;
	COutputEvent m_OnFireStop;
};

#endif // PROP_MINIGUN_H
