//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_INSECT_SWARM_H
#define L4D2_INSECT_SWARM_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/inferno.h"

class CInsectSwarm : public CInferno
{
public:
	DECLARE_CLASS( CInsectSwarm, CInferno );
	DECLARE_SERVERCLASS();
	virtual void Spawn() OVERRIDE;
	static CInsectSwarm *Create( CBaseEntity *owner, const Vector &position,
		const Vector &normal, const Vector &velocity, int depth = 0 );
	using CInferno::SetMaxFlames;
protected:
	virtual float GetFlameLifetime() const OVERRIDE;
	virtual double GetDamagePerSecond() const OVERRIDE;
	virtual int GetDamageType() const OVERRIDE;
	virtual bool CanHarm( CBaseEntity *entity ) const OVERRIDE;
	virtual float GetSpreadInterval() const OVERRIDE;
	virtual const char *GetLoopParticleName() const OVERRIDE;
	virtual const char *GetStartParticleName() const OVERRIDE;
private:
	float m_flSpawnTime;
};

#endif // L4D2_INSECT_SWARM_H
