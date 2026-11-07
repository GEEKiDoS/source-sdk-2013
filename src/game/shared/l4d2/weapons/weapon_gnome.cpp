//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#ifdef CLIENT_DLL
#define CGnome C_Gnome
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

class CGnome : public CCarriedProp
{
public:
	DECLARE_CLASS( CGnome, CCarriedProp );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_GNOME; }
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/props_junk/gnome.mdl"; }
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void Spawn() OVERRIDE;
	virtual void Equip( CBaseCombatCharacter *pOwner ) OVERRIDE;
	virtual bool DropCarriedItem( bool bThrow ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( Gnome, DT_Gnome )
BEGIN_NETWORK_TABLE( CGnome, DT_Gnome )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CGnome )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_gnome, CGnome );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_gnome );
#endif

#ifndef CLIENT_DLL
void CGnome::Spawn()
{
	BaseClass::Spawn();
	// S:0x3CEB00. These read the actual native mode options, not a
	// guessed classname or a permanently disabled mutation predicate.
	if ( TerrorGameRules()->IsVIPGnomeMutation() || TerrorGameRules()->IsHealingGnomeMutation() )
		SetCollisionGroup( COLLISION_GROUP_INTERACTIVE_DEBRIS );
}

void CGnome::Equip( CBaseCombatCharacter *pOwner )
{
	// S:0x3CE9D0: VIP wins the else-if when both options are enabled.
	if ( TerrorGameRules()->IsVIPGnomeMutation() )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( pOwner );
		if ( pPlayer )
		{
			pPlayer->m_bIsITUntilNextAttack = true;
			pPlayer->m_flGnomePickupTime = gpGlobals->curtime;
		}
	}
	else if ( TerrorGameRules()->IsHealingGnomeMutation() )
	{
		CTerrorPlayer *pPlayer = ToTerrorPlayer( pOwner );
		if ( pPlayer )
		{
			pPlayer->m_bCarryingHealingGnome = true;
			pPlayer->m_flGnomePickupTime = gpGlobals->curtime;
		}
	}
	BaseClass::Equip( pOwner );
}

bool CGnome::DropCarriedItem( bool bThrow )
{
	// S:0x3CEB70. Mutation state clears on the explicit conversion path;
	// do not move these writes into the unrelated native Drop callback.
	if ( TerrorGameRules()->IsVIPGnomeMutation() )
	{
		CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
		if ( pPlayer )
		{
			pPlayer->m_bIsITUntilNextAttack = false;
			pPlayer->m_flGnomePickupTime = -1.0f;
		}
	}
	else if ( TerrorGameRules()->IsHealingGnomeMutation() )
	{
		CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
		if ( pPlayer )
		{
			pPlayer->m_bCarryingHealingGnome = false;
			pPlayer->m_flGnomePickupTime = -1.0f;
		}
	}
	if ( TerrorGameRules()->IsVIPGnomeMutation() )
		bThrow = false;
	return BaseClass::DropCarriedItem( bThrow );
}
#endif

Activity CGnome::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? CarriedActivity( "ACT_MELEE_SWEEP_GNOME" ) : ACT_INVALID;
}

acttable_t *CGnome::ActivityList( int &iActivityCount )
{
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_GNOME" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_GNOME" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_GNOME" },
		{ "ACT_WALK", "ACT_WALK_GNOME" },
		{ "ACT_RUN", "ACT_RUN_GNOME" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_GNOME" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_GNOME" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_GNOME" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_GNOME" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_GNOME" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_GNOME" },
		{ "ACT_JUMP", "ACT_JUMP_GNOME" }
	};
	static acttable_t s_Activities[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Activities[i].baseAct = CarriedActivity( s_pNames[i][0] );
			s_Activities[i].weaponAct = CarriedActivity( s_pNames[i][1] );
			s_Activities[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Activities );
	return s_Activities;
}

// Native server 0x3ce8c0; 0x5c4cbc is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_gnomePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_gnome", 0x5c4cbc ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_gnome", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CGnome )
END_DATADESC()
#endif
