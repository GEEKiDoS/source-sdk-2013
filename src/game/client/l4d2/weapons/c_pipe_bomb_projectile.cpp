//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_pipe_bomb_projectile.h"
#include "l4d2/player/c_terrorplayer.h"
#include "particle_parse.h"
#include "particle_property.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_PipeBombProjectile, DT_PipeBombProjectile, CPipeBombProjectile )
END_RECV_TABLE()

bool C_PipeBombProjectile::Simulate()
{
	// C305150 retries creation only during the original first second, and
	// uses the same actual thrower animation-state gate as base DrawModel.
	if ( m_hFuseEffect )
	{
		m_hFuseEffect->SetSortOrigin( GetAbsOrigin() );
		m_hFuseEffect->SetNeedsBBoxUpdate( true );
	}
	else if ( gpGlobals->curtime - m_flSpawnTime < 1.0f )
	{
		C_TerrorPlayer *pThrower = ToTerrorPlayer( GetThrower() );
		if ( pThrower && !pThrower->ShouldHideGrenadeProjectile() )
		{
			ParticleProp()->Create( "weapon_pipebomb_fuse", PATTACH_POINT_FOLLOW, "fuse" );
			ParticleProp()->Create( "weapon_pipebomb_blinking_light", PATTACH_POINT_FOLLOW, "pipebomb_light" );
		}
	}
	BaseClass::Simulate();
	return true;
}

void C_PipeBombProjectile::OnNewParticleEffect( const char *pName, CNewParticleEffect *pEffect )
{
	if ( !Q_strcmp( pName, "weapon_pipebomb_fuse" ) )
		m_hFuseEffect = pEffect;
	if ( !Q_strcmp( pName, "weapon_pipebomb_blinking_light" ) )
		m_hBlinkEffect = pEffect;
	BaseClass::OnNewParticleEffect( pName, pEffect );
}
