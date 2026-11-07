//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_MOLOTOV_PROJECTILE_H
#define L4D2_C_MOLOTOV_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_basecsgrenade_projectile.h"
#include "particles_new.h"

class C_MolotovProjectile : public C_BaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( C_MolotovProjectile, C_BaseCSGrenadeProjectile );
	DECLARE_CLIENTCLASS();
	virtual bool Simulate() OVERRIDE;
	virtual void OnNewParticleEffect( const char *pName, CNewParticleEffect *pEffect ) OVERRIDE;
private:
	HPARTICLEFFECT m_hWickEffect;
};

#endif // L4D2_C_MOLOTOV_PROJECTILE_H
