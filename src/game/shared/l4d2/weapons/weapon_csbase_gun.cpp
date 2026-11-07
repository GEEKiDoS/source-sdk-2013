//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/weapon_csbase_gun.h"
#include "tier0/memdbgon.h"

extern ConVar tongue_victim_accuracy_penalty;

IMPLEMENT_NETWORKCLASS_ALIASED( WeaponCSBaseGun, DT_WeaponCSBaseGun )
BEGIN_NETWORK_TABLE( CWeaponCSBaseGun, DT_WeaponCSBaseGun )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CWeaponCSBaseGun )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_csbase_gun, CWeaponCSBaseGun );

void CWeaponCSBaseGun::SecondaryAttack()
{
}

void CWeaponCSBaseGun::Spawn()
{
	m_flAccuracy = 0.2f;
	m_bDelayFire = false;
	m_flZoomFullyActiveTime = -1.0f;
	BaseClass::Spawn();
}
bool CWeaponCSBaseGun::Deploy()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player ) return false;
	m_flAccuracy = 0.2f;
	player->m_iShotsFired = 0;
	m_bDelayFire = false;
	m_flZoomFullyActiveTime = -1.0f;
	return BaseClass::Deploy();
}

#pragma float_control( precise, on, push )
void CWeaponCSBaseGun::ItemPostFrame()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player ) return;
#ifndef CLIENT_DLL
	// S250E30: client C232AD0 has no corresponding zoom/FOV writes.
	if ( gpGlobals->curtime >= m_flNextPrimaryAttack && player->m_bResumeZoom )
	{
		player->SetFOV( player, player->m_iLastZoom, 0.05f, 0 );
		m_flZoomFullyActiveTime = gpGlobals->curtime + 0.05f;
		if ( player->GetFOV() == player->m_iLastZoom )
			player->m_bResumeZoom = false;
	}
#endif
	BaseClass::ItemPostFrame();
}

void CWeaponCSBaseGun::WeaponIdle()
{
	// S250F80/C232AF0: unordered idle times take the animation path.
	// The native clip test is nonzero, not positive.
	if ( !( m_flTimeWeaponIdle > gpGlobals->curtime ) && m_iClip1 != 0 )
	{
		SetWeaponIdleTime( GetCSWpnData().m_flIdleInterval + gpGlobals->curtime );
		SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE ) );
	}
}

void CWeaponCSBaseGun::UpdateAccuracy()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player ) return;
	const CCSWeaponInfo &info = GetCSWpnData();
	if ( info.m_flAccuracyDivisor != -1.0f )
	{
		// Native IMUL keeps its low signed32 bits before signed->float.
		const unsigned int shots = static_cast< unsigned int >( player->m_iShotsFired );
		unsigned int power = shots * shots;
		if ( !info.m_bAccuracyQuadratic ) power *= shots;
		const float accuracy = static_cast< float >( static_cast< int >( power ) ) / info.m_flAccuracyDivisor + info.m_flAccuracyOffset;
		m_flAccuracy = accuracy;
		if ( player->GetTongueOwner() )
			m_flAccuracy = tongue_victim_accuracy_penalty.GetFloat() + accuracy;
		if ( m_flAccuracy > info.m_flMaxInaccuracy )
			m_flAccuracy = info.m_flMaxInaccuracy;
	}
}
#pragma float_control( pop )

bool CWeaponCSBaseGun::Reload()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player || player->GetAmmoCount( GetPrimaryAmmoType() ) <= 0 ) return false;
	const int activity = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD );
#ifdef CLIENT_DLL
	// C232940 bypasses the server-only virtual wrapper, as the original does.
	if ( !CBaseCombatWeapon::DefaultReload( GetMaxClip1(), GetMaxClip2(), activity ) ) return false;
#else
	if ( !DefaultReload( GetMaxClip1(), GetMaxClip2(), activity ) ) return false;
#endif
	player->SetAnimation( PLAYER_RELOAD );
#ifndef CLIENT_DLL
	if ( player->GetFOV() != player->GetDefaultFOV() )
		player->SetFOV( player, player->GetDefaultFOV(), 0.0f, 0 );
#endif
	m_flAccuracy = 0.2f;
	player->m_iShotsFired = 0;
	m_bDelayFire = false;
	return true;
}

double CWeaponCSBaseGun::GetFireRate()
{
	return static_cast< double >( GetCSWpnData().m_flCycleTime );
}
void CWeaponCSBaseGun::DoFireEffects()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( player ) player->DoMuzzleFlash();
}
