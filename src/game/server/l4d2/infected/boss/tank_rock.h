//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_TANK_ROCK_H
#define L4D2_TANK_ROCK_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/weapons/basecsgrenade_projectile.h"

// S:367F80 allocates 6808 bytes: BaseCSGrenadeProjectile plus the members below.
// There is deliberately no DECLARE_SERVERCLASS: S:106A610C inherits the base DT.
class CTankRock : public CBaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( CTankRock, CBaseCSGrenadeProjectile );
	DECLARE_DATADESC();
	CTankRock();
	virtual void Precache() OVERRIDE;
	virtual void Spawn() OVERRIDE;
	virtual void Detonate() OVERRIDE;
	virtual void Event_Killed( const CTakeDamageInfo &info ) OVERRIDE;
	virtual void ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity ) OVERRIDE;
	virtual void GrenadeTouch( CBaseEntity *other ) OVERRIDE;
	static CTankRock *Create( const Vector &origin, const QAngle &angles, const Vector &velocity, CBaseCombatCharacter *thrower );
	void Launch( const Vector &origin, const QAngle &angles, const Vector &velocity, const AngularImpulse &angularVelocity );
	void BounceTouch( CBaseEntity *other );
	void ProximityThink();
private:
	friend class CEnvRockLauncher;
	bool CheckProximity( CBasePlayer *player );
	int m_surfaceMaterial; // S:+6776, selected from the thrower's ground surface.
	bool m_isLaunched; // S:+6780; Create writes this, the constructor does not.
	Vector m_previousPosition; // S:+6784; Create writes this, the constructor does not.
	IntervalTimer m_collisionExpansionTimer; // S:+6796, initially invalid.
	int m_rockDamageOverride; // S:+6804, constructor zero; env_rock_launcher supplies an override.
};
#endif
