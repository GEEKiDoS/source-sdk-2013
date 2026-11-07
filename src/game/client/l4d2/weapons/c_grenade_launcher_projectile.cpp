//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_grenade_launcher_projectile.h"
#include "c_te_effect_dispatch.h"
#include "effect_dispatch_data.h"
#include "ragdollexplosionenumerator.h"
#include "ispatialpartition.h"
#include "clientleafsystem.h"
#include "tier0/memdbgon.h"

// A C++ type spelling (not a macro) preserves both native registry strings.
typedef C_GrenadeLauncher_Projectile CGrenadeLauncher_Projectile;
LINK_ENTITY_TO_CLASS( grenade_launcher_projectile, CGrenadeLauncher_Projectile );
IMPLEMENT_CLIENTCLASS_DT( C_GrenadeLauncher_Projectile, DT_GrenadeLauncher_Projectile, CGrenadeLauncher_Projectile )
END_RECV_TABLE()

C_GrenadeLauncher_Projectile::C_GrenadeLauncher_Projectile()
{
	m_hTrail = NULL;
}

#pragma float_control( precise, on, push )
void C_GrenadeLauncher_Projectile::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );
	if ( updateType == DATA_UPDATE_CREATED )
	{
		// C2FC0B0/C2FC080: the canonical game-side leaf rendering flag.
		SetNextClientThink( static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.15 ) );
		ClientLeafSystem_EnableRendering( GetRenderHandle(), false );
	}
}
#pragma float_control( pop )

void C_GrenadeLauncher_Projectile::ClientThink()
{
	ClientLeafSystem_EnableRendering( GetRenderHandle(), true );
}


void C_GrenadeLauncher_Projectile::OnNewParticleEffect( const char *pszParticleName, CNewParticleEffect *pNewParticleEffect )
{
	// C:0x2FC330/0x2FC3B0. A native reference becomes NULL when the
	// effect is deleted; never retain an untracked CNewParticleEffect*.
	if ( !Q_strcmp( pszParticleName, "weapon_grenadelauncher_trail" ) )
		m_hTrail = pNewParticleEffect;
	BaseClass::OnNewParticleEffect( pszParticleName, pNewParticleEffect );
}

bool C_GrenadeLauncher_Projectile::Simulate()
{
	// C:0x2FC150 updates the particle sort origin before aligning the
	// model to the current absolute velocity. This is server-authored
	// motion, not a locally predicted projectile or a bullet tracer.
	if ( m_hTrail )
	{
		m_hTrail->SetSortOrigin( GetAbsOrigin() );
		m_hTrail->SetNeedsBBoxUpdate( true );
	}
	else
	{
		ParticleProp()->Create( "weapon_grenadelauncher_trail", PATTACH_ABSORIGIN_FOLLOW );
	}
	QAngle angles;
	VectorAngles( GetAbsVelocity(), angles );
	SetAbsAngles( angles );
	BaseClass::Simulate();
	return true;
}

// C:0x2FC110 ->0x20A140. This effect is force on existing client
// ragdolls, not another visual explosion or authoritative radius damage.
static void GrenadeLauncherExplosionForce( const CEffectData &data )
{
	CRagdollExplosionEnumerator ragdolls( data.m_vOrigin, data.m_flRadius, data.m_flMagnitude );
	partition->EnumerateElementsInSphere( PARTITION_CLIENT_RESPONSIVE_EDICTS,
		data.m_vOrigin, data.m_flRadius, false, &ragdolls );
}
DECLARE_CLIENT_EFFECT( "ExplosionForce", GrenadeLauncherExplosionForce );
