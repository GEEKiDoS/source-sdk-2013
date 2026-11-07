//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_GRENADE_LAUNCHER_PROJECTILE_H
#define L4D2_C_GRENADE_LAUNCHER_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_basecsgrenade_projectile.h"
#include "particles_new.h"

class C_GrenadeLauncher_Projectile : public C_BaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( C_GrenadeLauncher_Projectile, C_BaseCSGrenadeProjectile );
	DECLARE_CLIENTCLASS();

	C_GrenadeLauncher_Projectile();
	virtual void OnDataChanged( DataUpdateType_t updateType ) OVERRIDE;
	virtual void ClientThink() OVERRIDE;
	virtual bool Simulate() OVERRIDE;
	virtual void OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect ) OVERRIDE;

private:
	HPARTICLEFFECT m_hTrail;
};

#endif // L4D2_C_GRENADE_LAUNCHER_PROJECTILE_H
