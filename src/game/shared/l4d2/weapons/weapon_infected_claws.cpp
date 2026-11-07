//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_claw.h"
#include "l4d2/gamerules/terror_gamerules.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#define CChargerClaw C_ChargerClaw
#define CBoomerClaw C_BoomerClaw
#define CSmokerClaw C_SmokerClaw
#define CSpitterClaw C_SpitterClaw
#define CJockeyClaw C_JockeyClaw
#else
#include "l4d2/player/terrorplayer.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

class CChargerClaw : public CClaw
{
public:
	DECLARE_CLASS( CChargerClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_CHARGER_CLAW; }
#ifndef CLIENT_DLL
protected:
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
};
class CBoomerClaw : public CClaw
{
public:
	DECLARE_CLASS( CBoomerClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_BOOMER_CLAW; }
#ifndef CLIENT_DLL
protected:
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
};
class CSmokerClaw : public CClaw
{
public:
	DECLARE_CLASS( CSmokerClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SMOKER_CLAW; }
#ifndef CLIENT_DLL
protected:
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
};
class CSpitterClaw : public CClaw
{
public:
	DECLARE_CLASS( CSpitterClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SPITTER_CLAW; }
#ifndef CLIENT_DLL
protected:
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
};
class CJockeyClaw : public CClaw
{
public:
	DECLARE_CLASS( CJockeyClaw, CClaw );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_JOCKEY_CLAW; }
protected:
	virtual void Swing( float interval, float duration, float range ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual float GetClawDamage( CTerrorPlayer *victim ) const OVERRIDE;
#endif
};

LINK_ENTITY_TO_CLASS( weapon_hunter_claw, CHunterClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_hunter_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( HunterClaw, DT_WeaponHunterClaw )
BEGIN_NETWORK_TABLE( CHunterClaw, DT_WeaponHunterClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CHunterClaw )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_charger_claw, CChargerClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_charger_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( ChargerClaw, DT_WeaponChargerClaw )
BEGIN_NETWORK_TABLE( CChargerClaw, DT_WeaponChargerClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CChargerClaw )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_boomer_claw, CBoomerClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_boomer_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( BoomerClaw, DT_WeaponBoomerClaw )
BEGIN_NETWORK_TABLE( CBoomerClaw, DT_WeaponBoomerClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CBoomerClaw )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_smoker_claw, CSmokerClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_smoker_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( SmokerClaw, DT_WeaponSmokerClaw )
BEGIN_NETWORK_TABLE( CSmokerClaw, DT_WeaponSmokerClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSmokerClaw )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_spitter_claw, CSpitterClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_spitter_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( SpitterClaw, DT_WeaponSpitterClaw )
BEGIN_NETWORK_TABLE( CSpitterClaw, DT_WeaponSpitterClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CSpitterClaw )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_jockey_claw, CJockeyClaw );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_jockey_claw );
#endif
IMPLEMENT_NETWORKCLASS_ALIASED( JockeyClaw, DT_WeaponJockeyClaw )
BEGIN_NETWORK_TABLE( CJockeyClaw, DT_WeaponJockeyClaw )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CJockeyClaw )
END_PREDICTION_DATA()

void CHunterClaw::Spawn()
{
	Precache();
	m_pounceHitTimer.Invalidate();
	m_iPrimaryAmmoType = -1;
	BaseClass::Spawn();
}


void CHunterClaw::Swing( float interval, float duration, float range )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner && !owner->m_pounceVictim.Get() )
		BaseClass::Swing( interval, duration, range );
}

void CJockeyClaw::Swing( float interval, float duration, float range )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( owner && !owner->m_jockeyVictim.Get() )
		BaseClass::Swing( interval, duration, range );
}

// S3D1820/C2FD290 are genuine empty overrides of the stun callback.
void CHunterClaw::OnOwnerStunned( float duration ) {}

#ifndef CLIENT_DLL
#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S3D1A40. Preserve the native inverted-looking admission arm.
void CHunterClaw::OnPounceHit()
{
	if ( !GetTerrorPlayerOwner() ||
		( !GetTerrorPlayerOwner()->m_isAttemptingToPounce && !GetTerrorPlayerOwner()->m_pounceVictim.Get() ) )
	{
		if ( static_cast< double >( gpGlobals->curtime ) >= m_pounceHitTimer.m_timestamp.Get() )
		{
			const double expiry = static_cast< double >( gpGlobals->curtime ) + 1.0;
			if ( m_pounceHitTimer.m_timestamp.Get() != expiry )
				m_pounceHitTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
			m_pounceHitTimer.m_duration = 1.0f;
			SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_PUSH ) );
		}
	}
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
#endif

#ifndef CLIENT_DLL
ConVar hunter_pz_claw_dmg( "hunter_pz_claw_dmg", "6", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ hunter's regular melee attack" );
ConVar charger_pz_claw_dmg( "charger_pz_claw_dmg", "10", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ charger's regular melee attack" );
ConVar boomer_pz_claw_dmg( "boomer_pz_claw_dmg", "4", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ boomer's regular melee attack" );
ConVar smoker_pz_claw_dmg( "smoker_pz_claw_dmg", "4", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ smoker's regular melee attack" );
ConVar spitter_pz_claw_dmg( "spitter_pz_claw_dmg", "4", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ spitter's regular melee attack" );
ConVar jockey_pz_claw_dmg( "jockey_pz_claw_dmg", "4", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
, "Amount of damage done by a PZ jockey's regular melee attack" );

float CHunterClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	if ( !GetTerrorPlayerOwner() )
		return TerrorGameRules()->GetCommonAttackDamage( victim );
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
		return hunter_pz_claw_dmg.GetFloat();
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0:
	case 1: return 10.0f;
	case 2: return 20.0f;
	case 3: return 40.0f;
	default: return 1.0f;
	}
}

float CChargerClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
		return charger_pz_claw_dmg.GetFloat();
	switch ( TerrorGameRules()->GetDifficultyLevel() )
	{
	case 0: return 5.0f;
	case 1: return 10.0f;
	case 2: return 15.0f;
	case 3: return 20.0f;
	default: return 100.0f;
	}
}

float CBoomerClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	return GetTerrorPlayerOwner() && TerrorGameRules()->IsPlayerControlledZombies() ?
		boomer_pz_claw_dmg.GetFloat() : TerrorGameRules()->GetCommonAttackDamage( victim );
}
float CSmokerClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	return GetTerrorPlayerOwner() && TerrorGameRules()->IsPlayerControlledZombies() ?
		smoker_pz_claw_dmg.GetFloat() : TerrorGameRules()->GetCommonAttackDamage( victim );
}
float CSpitterClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	return GetTerrorPlayerOwner() && TerrorGameRules()->IsPlayerControlledZombies() ?
		spitter_pz_claw_dmg.GetFloat() : TerrorGameRules()->GetCommonAttackDamage( victim );
}
float CJockeyClaw::GetClawDamage( CTerrorPlayer *victim ) const
{
	return GetTerrorPlayerOwner() && TerrorGameRules()->IsPlayerControlledZombies() ?
		jockey_pz_claw_dmg.GetFloat() : TerrorGameRules()->GetCommonAttackDamage( victim );
}
#endif

// Native server 0x3c4320; 0x5c4b44 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_boomer_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_boomer_claw", 0x5c4b44 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_boomer_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3cb4b0; 0x5c4be4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_charger_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_charger_claw", 0x5c4be4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_charger_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3d17e0; 0x5c4d38 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_hunter_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_hunter_claw", 0x5c4d38 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_hunter_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3d3f40; 0x5c4e08 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_jockey_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_jockey_claw", 0x5c4e08 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_jockey_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3e15a0; 0x5c507c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_smoker_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_smoker_claw", 0x5c507c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_smoker_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3e21c0; 0x5c50f8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_spitter_clawPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_spitter_claw", 0x5c50f8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_spitter_claw", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBoomerClaw )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CChargerClaw )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CJockeyClaw )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSmokerClaw )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CSpitterClaw )
END_DATADESC()
#endif
