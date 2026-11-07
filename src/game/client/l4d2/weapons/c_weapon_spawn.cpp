//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/c_weapon_spawn.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "ammodef.h"

#include "tier0/memdbgon.h"

// Original client registration C:50F290, flags zero (not a replicated cvar).
static ConVar weapon_spawn_fade_distance( "weapon_spawn_fade_distance", "1000", 0x0000000A, "Maximum visible distance of weapon spawns" );

IMPLEMENT_CLIENTCLASS_DT( C_WeaponSpawn, DT_WeaponSpawn, CWeaponSpawn )
	RecvPropInt( RECVINFO( m_weaponID ) ),
	RecvPropFloat( RECVINFO( m_flGlowRange ) ),
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_WeaponAmmoSpawn, DT_WeaponAmmoSpawn, CWeaponAmmoSpawn )
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_WeaponScavengeItemSpawn, DT_ScavengeItemSpawn, CWeaponScavengeItemSpawn )
END_RECV_TABLE()

int C_WeaponSpawn::ObjectCaps()
{
	// C:249430. Do not use the server CItem's capabilities on this side.
	int caps = BaseClass::ObjectCaps();
	if ( !IsEffectActive( EF_NODRAW ) && !IsDormant() )
		caps |= FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
	return caps;
}

int C_WeaponAmmoSpawn::ObjectCaps()
{
	// C:2494E0 deliberately returns these bits even when hidden/dormant.
	return C_BaseAnimating::ObjectCaps() | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}

bool C_WeaponSpawn::IsUsable( C_BaseEntity *pUser )
{
	// C:249400 has no user/team check; those belong to GetUseType/Use.
	return !IsEffectActive( EF_NODRAW ) && !IsDormant();
}

int C_WeaponSpawn::GetUseType( C_BaseEntity *pUser )
{
	// C:2492C0. Range uses origins and a strict >, not eye distance or LOS.
	if ( IsEffectActive( EF_NODRAW ) || IsDormant() )
		return 0;
	C_TerrorPlayer *pPlayer = ToTerrorPlayer( pUser );
	if ( !pPlayer )
		return 0;
	if ( m_flGlowRange > 0.0f && ( pPlayer->GetAbsOrigin() - GetAbsOrigin() ).LengthSqr() > m_flGlowRange * m_flGlowRange )
		return 4;
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( m_weaponID );
	const int nMaxCarry = pInfo ? GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ) : -1;
	return GetTerrorWeaponGlowType( pPlayer, m_weaponID, -1, nMaxCarry, NULL, false );
}

CStudioHdr *C_WeaponSpawn::OnNewModel()
{
	// C:249270 sets fade distances after the actual animating model change.
	CStudioHdr *pHdr = BaseClass::OnNewModel();
	m_fadeMinDist = weapon_spawn_fade_distance.GetFloat() * 0.75f;
	m_fadeMaxDist = weapon_spawn_fade_distance.GetFloat();
	return pHdr;
}
