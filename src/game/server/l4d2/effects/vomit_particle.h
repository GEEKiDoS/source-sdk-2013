//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_VOMIT_PARTICLE_H
#define L4D2_VOMIT_PARTICLE_H
#ifdef _WIN32
#pragma once
#endif
#include "baseentity.h"
class CTerrorPlayer;

class CVomitParticle : public CBaseEntity
{
public:
	DECLARE_CLASS( CVomitParticle, CBaseEntity );
	DECLARE_DATADESC();
	CVomitParticle();
	virtual void Spawn() OVERRIDE;
	virtual unsigned int PhysicsSolidMaskForEntity() const OVERRIDE;
	virtual void ResolveFlyCollisionCustom( trace_t &trace, Vector &velocity ) OVERRIDE;
	static CVomitParticle *Create( const Vector &position, const QAngle &angles,
		CTerrorPlayer *owner, CTerrorPlayer *attacker );
	void ParticleThink();
	void OnHit( CBaseEntity *entity );
private:
	Vector m_sprayVelocity;
	Vector m_inheritedVelocity;
	float m_flLifetime;
	CHandle< CTerrorPlayer > m_attacker;
	Vector m_previousPosition;
};

void ApplyVomitHit( CTerrorPlayer *attacker, CBaseEntity *victim, CVomitParticle *particle,
	int team, const Vector &velocity, const Vector &position );
#endif // L4D2_VOMIT_PARTICLE_H
