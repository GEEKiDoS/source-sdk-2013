//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_basecsgrenade_projectile.h"
#include "l4d2/player/c_terrorplayer.h"
#include "input.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS( C_BaseCSGrenadeProjectile, DT_BaseCSGrenadeProjectile, CBaseCSGrenadeProjectile )
BEGIN_RECV_TABLE( C_BaseCSGrenadeProjectile, DT_BaseCSGrenadeProjectile )
	RecvPropVector( RECVINFO( m_vInitialVelocity ) ),
END_RECV_TABLE()

// C:0x21FCE0.
void C_BaseCSGrenadeProjectile::Spawn()
{
	m_flSpawnTime = gpGlobals->curtime;
	BaseClass::Spawn();
}

// C:0x21FDA0. These are real interpolation samples, not client projectile physics.
void C_BaseCSGrenadeProjectile::PostDataUpdate( DataUpdateType_t type )
{
	BaseClass::PostDataUpdate( type );
	if ( type == DATA_UPDATE_CREATED )
	{
		CInterpolatedVar< Vector > &interpolator = GetOriginInterpolator();
		interpolator.ClearHistory();
		const float flChangeTime = GetLastChangeTime( LATCH_SIMULATION_VAR );
		Vector origin = GetLocalOrigin() - m_vInitialVelocity;
		interpolator.AddToHead( flChangeTime - 1.0f, &origin, false );
		origin = GetLocalOrigin();
		interpolator.AddToHead( flChangeTime, &origin, false );
	}
}

// C:0x21FBC0. Core owns the actual incap/pin/weapon/animation-timer predicate.
int C_BaseCSGrenadeProjectile::DrawModel( int flags )
{
	C_BaseCombatCharacter *pThrower = GetThrower();
	if ( pThrower && ( pThrower != C_BasePlayer::GetLocalPlayer() || input->CAM_IsThirdPerson() ) &&
		( pThrower->GetTeamNumber() == TEAM_SURVIVOR || pThrower->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		gpGlobals->curtime - m_flSpawnTime < 1.0f )
	{
		C_TerrorPlayer *pPlayer = ToTerrorPlayer( pThrower );
		if ( pPlayer && pPlayer->ShouldHideGrenadeProjectile() )
			return 0;
	}
	return BaseClass::DrawModel( flags );
}
