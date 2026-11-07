//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_GRENADE_LAUNCHER_PROJECTILE_H
#define L4D2_GRENADE_LAUNCHER_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/basecsgrenade_projectile.h"

class CGrenadeLauncher_Projectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CGrenadeLauncher_Projectile, CBaseCSGrenadeProjectile );
	DECLARE_SERVERCLASS();

	CGrenadeLauncher_Projectile();
	virtual void Spawn() OVERRIDE;
	virtual void Precache() OVERRIDE;
	virtual int GetDamageType() const OVERRIDE;
	virtual void Explode( trace_t *pTrace, int bitsDamageType ) OVERRIDE;

	static CGrenadeLauncher_Projectile *Create( const Vector &position, const QAngle &angles,
		const Vector &velocity, const AngularImpulse &angVelocity,
		CBaseCombatCharacter *pThrower, int bitsDamageType );

private:
	void ArmThink();
	void GrenadeLauncherTouch( CBaseEntity *pOther );

	IntervalTimer m_spawnTimer;
	int m_bitsUpgradeDamageType;
	bool m_bArmed;
};

#endif // L4D2_GRENADE_LAUNCHER_PROJECTILE_H
