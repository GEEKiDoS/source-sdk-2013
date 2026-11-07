//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_VOMITJAR_PROJECTILE_H
#define L4D2_VOMITJAR_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/basecsgrenade_projectile.h"
#include "util_shared.h"

class ConVar;
extern ConVar z_vomit_hit_pitch_min;
extern ConVar z_vomit_hit_pitch_max;
extern ConVar z_vomit_hit_yaw_min;
extern ConVar z_vomit_hit_yaw_max;
extern ConVar z_vomit_fade_start;

class CVomitJarProjectile : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CVomitJarProjectile, CBaseCSGrenadeProjectile );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	static CVomitJarProjectile *Create( const Vector &position, const QAngle &angles,
		const Vector &velocity, const AngularImpulse &angularVelocity, CBaseCombatCharacter *pThrower );
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void GrenadeTouch( CBaseEntity *pOther ) OVERRIDE;
	virtual void Detonate() OVERRIDE;
	virtual void BounceSound() OVERRIDE;

private:
	void StillThink();
	void SplashBile( const Vector &position, const Vector &normal );
	IntervalTimer m_stillTimer;
};

// Original world-impact producer. Infected navigation and Director population
// consumers are separate M5/M6 owners; this entity never rewrites actor targets.
class CInfoGoalInfectedChase : public CPointEntity
{
public:
	DECLARE_CLASS( CInfoGoalInfectedChase, CPointEntity );
	DECLARE_DATADESC();
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	void InputEnable( inputdata_t &inputdata );
	void InputDisable( inputdata_t &inputdata );
private:
	void AttractThink();
};

#endif // L4D2_VOMITJAR_PROJECTILE_H
