//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_PIPE_BOMB_PROJECTILE_H
#define L4D2_C_PIPE_BOMB_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_basecsgrenade_projectile.h"
#include "particles_new.h"

class C_PipeBombProjectile : public C_BaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( C_PipeBombProjectile, C_BaseCSGrenadeProjectile );
	DECLARE_CLIENTCLASS();
	virtual bool Simulate() OVERRIDE;
	virtual void OnNewParticleEffect( const char *pName, CNewParticleEffect *pEffect ) OVERRIDE;
private:
	HPARTICLEFFECT m_hFuseEffect;
	HPARTICLEFFECT m_hBlinkEffect;
};

#endif // L4D2_C_PIPE_BOMB_PROJECTILE_H
