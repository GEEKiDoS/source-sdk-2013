//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/c_spitter_projectile.h"
#include "particle_property.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_SpitterProjectile, DT_SpitterProjectile, CSpitterProjectile )
END_RECV_TABLE()

bool C_SpitterProjectile::Simulate()
{
	if ( m_hSpitEffect )
	{
		m_hSpitEffect->SetSortOrigin( GetAbsOrigin() );
		m_hSpitEffect->SetNeedsBBoxUpdate( true );
	}
	else
		ParticleProp()->Create( "spitter_projectile", PATTACH_ABSORIGIN_FOLLOW );
	BaseClass::Simulate();
	return true;
}

void C_SpitterProjectile::OnNewParticleEffect( const char *name, CNewParticleEffect *effect )
{
	if ( !Q_strcmp( name, "spitter_projectile" ) )
		m_hSpitEffect = effect;
	BaseClass::OnNewParticleEffect( name, effect );
}
