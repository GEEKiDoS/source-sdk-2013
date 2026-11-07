//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_BASECSGRENADE_PROJECTILE_H
#define L4D2_C_BASECSGRENADE_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"

class C_BaseCSGrenadeProjectile : public C_BaseGrenade
{
public:
	DECLARE_CLASS( C_BaseCSGrenadeProjectile, C_BaseGrenade );
	DECLARE_CLIENTCLASS();

	virtual void Spawn() OVERRIDE;
	virtual void PostDataUpdate( DataUpdateType_t type ) OVERRIDE;
	virtual int DrawModel( int flags ) OVERRIDE;

protected:
	Vector m_vInitialVelocity;
	float m_flSpawnTime;
};

#endif // L4D2_C_BASECSGRENADE_PROJECTILE_H
