//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BASECSGRENADE_PROJECTILE_H
#define L4D2_BASECSGRENADE_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"

class CBaseCSGrenadeProjectile : public CBaseGrenade
{
public:
	DECLARE_CLASS( CBaseCSGrenadeProjectile, CBaseGrenade );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
	virtual void ResolveFlyCollisionCustom( trace_t &trace, Vector &vecVelocity ) OVERRIDE;
	virtual void Splash() OVERRIDE;
	virtual float GetShakeAmplitude() OVERRIDE { return 0.0f; }
	virtual void GrenadeTouch( CBaseEntity *pOther );

	void DangerSoundThink();
	void SetTimer( float flDuration );
	void SetInitialVelocity( const Vector &velocity );

	CNetworkVector( m_vInitialVelocity );

private:
	// This deadline belongs to the CS projectile, not CBaseGrenade's tumble think.
	float m_flDetonateTime;
};

#endif // L4D2_BASECSGRENADE_PROJECTILE_H
