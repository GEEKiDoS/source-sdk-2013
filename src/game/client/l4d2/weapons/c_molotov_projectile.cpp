//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_molotov_projectile.h"
#include "particle_parse.h"
#include "particle_property.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_MolotovProjectile, DT_MolotovProjectile, CMolotovProjectile )
END_RECV_TABLE()

bool C_MolotovProjectile::Simulate()
{
	// C302F50. HPARTICLEFFECT is the native lifetime-tracked reference;
	// deleted effects clear it without retaining a dangling particle pointer.
	if ( m_hWickEffect )
	{
		m_hWickEffect->SetSortOrigin( GetAbsOrigin() );
		m_hWickEffect->SetNeedsBBoxUpdate( true );
	}
	else
		ParticleProp()->Create( "weapon_molotov_thrown", PATTACH_POINT_FOLLOW, "Wick" );
	BaseClass::Simulate();
	return true;
}

void C_MolotovProjectile::OnNewParticleEffect( const char *pName, CNewParticleEffect *pEffect )
{
	if ( !Q_strcmp( pName, "weapon_molotov_thrown" ) )
		m_hWickEffect = pEffect;
	BaseClass::OnNewParticleEffect( pName, pEffect );
}
