//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_SPITTER_PROJECTILE_H
#define L4D2_SPITTER_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/basecsgrenade_projectile.h"
#include "util_shared.h"

class CSpitterProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CSpitterProjectile, CBaseCSGrenadeProjectile );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();
	static CSpitterProjectile *Create( const Vector &position, const QAngle &angles,
		const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *thrower );
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
	virtual void GrenadeTouch( CBaseEntity *other ) OVERRIDE;
	virtual void Detonate() OVERRIDE;
	virtual void BounceSound() OVERRIDE;
	void ProjectileThink();
private:
	IntervalTimer m_stillTimer;
	float m_flSpawnTime;
};

#endif // L4D2_SPITTER_PROJECTILE_H
