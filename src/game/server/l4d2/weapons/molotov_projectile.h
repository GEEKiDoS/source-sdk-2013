//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_MOLOTOV_PROJECTILE_H
#define L4D2_MOLOTOV_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/basecsgrenade_projectile.h"
#include "util_shared.h"

class CMolotovProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CMolotovProjectile, CBaseCSGrenadeProjectile );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	static CMolotovProjectile *Create( const Vector &position, const QAngle &angles,
		const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower );
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void GrenadeTouch( CBaseEntity *pOther ) OVERRIDE;
	virtual void Detonate() OVERRIDE;
	virtual void BounceSound() OVERRIDE;

private:
	void StillThink();
	IntervalTimer m_stillTimer;
};

#endif // L4D2_MOLOTOV_PROJECTILE_H
