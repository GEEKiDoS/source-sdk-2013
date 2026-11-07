//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_C_VOMITJAR_PROJECTILE_H
#define L4D2_C_VOMITJAR_PROJECTILE_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/weapons/c_basecsgrenade_projectile.h"

class C_VomitJarProjectile : public C_BaseCSGrenadeProjectile
{
public:
	DECLARE_CLASS( C_VomitJarProjectile, C_BaseCSGrenadeProjectile );
	DECLARE_CLIENTCLASS();
	virtual void OnDataChanged( DataUpdateType_t type ) OVERRIDE;
	virtual void ClientThink() OVERRIDE;
};

class IMatRenderContext;
class IMaterial;
// The existing player/view owners call these; they are not a second effect
// system. The actual particles and timers remain owned by C_TerrorPlayer.
bool Terror_HasVomitScreenSpaceParticles();
void Terror_RenderVomitScreenSpaceParticles( IMatRenderContext *pRenderContext );
void Terror_BindVomitScreenSpaceMaterial( IMaterial *pMaterial );

#endif // L4D2_C_VOMITJAR_PROJECTILE_H
