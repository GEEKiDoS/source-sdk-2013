//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_rifle.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CRifle_Desert C_Rifle_Desert
#endif

class CRifle_Desert : public CBaseRifle
{
public:
	DECLARE_CLASS( CRifle_Desert, CBaseRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_RIFLE_DESERT; }
	virtual bool Deploy() OVERRIDE;
	virtual void ItemPostFrame() OVERRIDE;
	virtual void PrimaryAttack() OVERRIDE;
	virtual void SecondaryAttack() OVERRIDE;

private:
	float m_flBurstShot1;
	float m_flBurstShot2;
	float m_flBurstResetTime;
};

IMPLEMENT_NETWORKCLASS_ALIASED( Rifle_Desert, DT_Rifle_Desert )
BEGIN_NETWORK_TABLE( CRifle_Desert, DT_Rifle_Desert )
END_NETWORK_TABLE()
#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CRifle_Desert )
	// C:0x6F2834: SAVE, not INSENDTABLE and not FIELD_TIME.
	DEFINE_PRED_FIELD( m_flBurstShot1, FIELD_FLOAT, FTYPEDESC_SAVE ),
	DEFINE_PRED_FIELD( m_flBurstShot2, FIELD_FLOAT, FTYPEDESC_SAVE ),
	DEFINE_PRED_FIELD( m_flBurstResetTime, FIELD_FLOAT, FTYPEDESC_SAVE ),
END_PREDICTION_DATA()
#endif
LINK_ENTITY_TO_CLASS( weapon_rifle_desert, CRifle_Desert );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_rifle_desert );
#endif

bool CRifle_Desert::Deploy()
{
	// S:0x3DFF90. Holster does not have a second cancellation path.
	m_flBurstShot1 = 0.0f;
	m_flBurstShot2 = 0.0f;
	m_flBurstResetTime = 0.0f;
	return BaseClass::Deploy();
}

#pragma float_control( precise, on, push )
void CRifle_Desert::PrimaryAttack()
{
	// S:0x3E01D0. The interaction consumer precedes the generic bullet;
	// queued shots below intentionally bypass it and the visibility gate.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || !pPlayer->CanAttack() || !IsWeaponVisible() || m_flBurstResetTime > gpGlobals->curtime || HandlePrimaryAttackUse() )
		return;
	FireBullet();
	// S3E0242..273/C306BBA..BEA: retain the wide cycle until each store.
	const double flCycle = GetFireRate();
	m_flBurstShot1 = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + flCycle );
	m_flBurstShot2 = static_cast< float >( ( flCycle + flCycle ) + static_cast< double >( gpGlobals->curtime ) );
	m_flBurstResetTime = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.35 );
	m_flNextPrimaryAttack = m_flBurstShot1;
}

void CRifle_Desert::ItemPostFrame()
{
	// S:0x3DFFE0: strict >, before CTerrorGun updates spread/input. A burst
	// can finish after releasing attack; neither an empty clip nor a reload
	// adds an invented cancellation rule here.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer && pPlayer->CanAttack() )
	{
		if ( m_flBurstShot1 > 0.0f && gpGlobals->curtime > m_flBurstShot1 )
		{
			FireBullet();
			m_flBurstShot1 = 0.0f;
		}
		if ( m_flBurstShot2 > 0.0f && gpGlobals->curtime > m_flBurstShot2 )
		{
			FireBullet();
			m_flBurstShot2 = 0.0f;
		}
	}
	else
	{
		m_flBurstShot1 = 0.0f;
		m_flBurstShot2 = 0.0f;
		m_flBurstResetTime = 0.0f;
	}
	BaseClass::ItemPostFrame();
}
#pragma float_control( pop )

void CRifle_Desert::SecondaryAttack()
{
	// S:0x3DFFB0: perform the actual shove before clearing the burst.
	BaseClass::SecondaryAttack();
	m_flBurstShot1 = 0.0f;
	m_flBurstShot2 = 0.0f;
	m_flBurstResetTime = 0.0f;
}

// Native server 0x3dff50; 0x5c4fd8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_rifle_desertPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_rifle_desert", 0x5c4fd8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_rifle_desert" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_desert", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CRifle_Desert )
END_DATADESC()
#endif
