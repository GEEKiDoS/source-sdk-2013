//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_PIPE_BOMB_PROJECTILE_H
#define L4D2_PIPE_BOMB_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/basecsgrenade_projectile.h"

class CPipeBombProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CPipeBombProjectile, CBaseCSGrenadeProjectile );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	static CPipeBombProjectile *Create( const Vector &position, const QAngle &angles,
		const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower );
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void GrenadeTouch( CBaseEntity *pOther ) OVERRIDE;
	virtual void ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity ) OVERRIDE;
	virtual void Detonate() OVERRIDE;
	virtual void BounceSound() OVERRIDE;
	virtual float GetShakeAmplitude() OVERRIDE;
	virtual float GetShakeRadius() OVERRIDE;

private:
	void BeepThink();
	float m_flNextBeepTime;
	float m_flBeepInterval;
};

#endif // L4D2_PIPE_BOMB_PROJECTILE_H
