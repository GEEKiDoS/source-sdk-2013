//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/upgrade_item.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "igameevents.h"
#include "recipientfilter.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

IMPLEMENT_SERVERCLASS_ST( CBaseUpgradeItem, DT_BaseUpgradeItem )
	SendPropInt( SENDINFO( m_iUsedBySurvivorsMask ), 4, SPROP_UNSIGNED ),
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( upgrade_ammo_incendiary, CUpgradeAmmoIncendiary );
LINK_ENTITY_TO_CLASS( upgrade_ammo_explosive, CUpgradeAmmoExplosive );
LINK_ENTITY_TO_CLASS( upgrade_laser_sight, CUpgradeLaserSight );
LINK_ENTITY_TO_CLASS( trigger_upgrade_laser_sight, CTriggerUpgradeLaserSight );

CBaseUpgradeItem::CBaseUpgradeItem()
{
	m_iUsedBySurvivorsMask = 0;
}

static bool UpgradeItemVisibilityPredicate( CBaseEntity *entity, CBasePlayer *observer )
{
	// S3036E0: only player identity, the character-use bit, and primary presence.
	CTerrorPlayer *player = observer && observer->IsPlayer() ? static_cast< CTerrorPlayer * >( observer ) : NULL;
	CBaseUpgradeItem *upgrade = dynamic_cast< CBaseUpgradeItem * >( entity );
	return player && upgrade && !upgrade->HasBeenUsedBySurvivor( (unsigned int)player->GetSurvivorCharacter() ) &&
		player->Weapon_GetSlot( 0 ) != NULL;
}

void CBaseUpgradeItem::Spawn()
{
	// S:0x3569C0. Four uses, not a player-count snapshot. Identity is the
	// survivor character, so death/reconnect/takeover do not reset a bit.
	m_itemCount = 4;
	RegisterVisibility( 360.0f, UpgradeItemVisibilityPredicate );
	BaseClass::Spawn();
	SetModel( GetWeaponModel() );
	m_iUsedBySurvivorsMask = 0;
}

void CBaseUpgradeItem::Precache()
{
	PrecacheModel( GetWeaponModel() );
}

int CBaseUpgradeItem::GetWeaponID() const
{
	return TERROR_WEAPON_UPGRADE_ITEM;
}

const char *CUpgradeAmmoIncendiary::GetWeaponModel() const
{
	return "models/props/terror/incendiary_ammo.mdl";
}

const char *CUpgradeAmmoExplosive::GetWeaponModel() const
{
	return "models/props/terror/exploding_ammo.mdl";
}

const char *CUpgradeLaserSight::GetWeaponModel() const
{
	return "models/w_models/Weapons/w_laser_sights.mdl";
}

bool CBaseUpgradeItem::HasBeenUsedBySurvivor( unsigned int character ) const
{
	// The unsigned bound is significant for the L4D1 character indices.
	return IsLimitedUse() && character <= 3 && ( m_iUsedBySurvivorsMask & ( 1 << character ) ) != 0;
}

void CBaseUpgradeItem::MarkUsedBySurvivor( unsigned int character )
{
	if ( IsLimitedUse() && character <= 3 )
		m_iUsedBySurvivorsMask |= 1 << character;
}

int CBaseUpgradeItem::GetUseType( CBaseEntity *user ) // S356590.
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( user );
	if ( pPlayer && HasBeenUsedBySurvivor( (unsigned int)pPlayer->GetSurvivorCharacter() ) )
		return 0;
	return BaseClass::GetUseType( user );
}

void CBaseUpgradeItem::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// S:0x356B00 admits team2 and team4 and does not add an IsAlive gate.
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( !pPlayer || ( pPlayer->GetTeamNumber() != 2 && pPlayer->GetTeamNumber() != 4 ) )
		return;

	const unsigned int character = (unsigned int)pPlayer->GetSurvivorCharacter();
	if ( HasBeenUsedBySurvivor( character ) )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "upgrade_item_already_used" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetString( "upgradeclass", GetClassname() );
			gameeventmanager->FireEvent( pEvent );
		}
		return;
	}

	if ( !pPlayer->GiveUpgrade( GetUpgradeType() ) )
		return;

	IGameEvent *pEvent = gameeventmanager->CreateEvent( "upgrade_pack_added" );
	if ( pEvent )
	{
		pEvent->SetInt( "upgradeid", entindex() );
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}

	if ( IsLimitedUse() )
	{
		MarkUsedBySurvivor( character );
		// The original decrements even when character >3 cannot set a bit.
		m_itemCount = static_cast< int >( static_cast< unsigned int >( m_itemCount ) - 1u );
		if ( m_itemCount == 0 )
		{
			UnregisterVisibility();
			UTIL_Remove( this );
		}
	}
}

void CTriggerUpgradeLaserSight::Spawn()
{
	// S:0x356600. The touch callback grants directly; it does not route
	// through a fabricated inventory pickup or an extra damage/fire path.
	BaseClass::Spawn();
	InitTrigger();
	SetTouch( &CTriggerUpgradeLaserSight::UpgradeTouch );
}

void CTriggerUpgradeLaserSight::UpgradeTouch( CBaseEntity *pOther )
{
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pOther );
	if ( pPlayer && ( pPlayer->GetTeamNumber() == 2 || pPlayer->GetTeamNumber() == 4 ) )
		pPlayer->GiveUpgrade( GetUpgradeType() );
}

bool CTerrorPlayer::GiveUpgrade( int upgrade )
{
	// S:0x308EE0. Upgrades address the primary loadout slot, not whichever
	// item happens to be active. The actual gun owns acceptance/ammo bits.
	static const char *s_pNames[] = { "INCENDIARY_AMMO", "EXPLOSIVE_AMMO", "LASER_SIGHT" };
	static const char *s_pPickupNames[] = { "IncendiaryAmmo", "ExplosiveAmmo", "LaserSight" };
	if ( upgrade < 0 || upgrade >= ARRAYSIZE( s_pNames ) )
		return false;

	CTerrorWeapon *pPrimary = dynamic_cast< CTerrorWeapon * >( Weapon_GetSlot( 0 ) );
	if ( !pPrimary )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "upgrade_failed_no_primary" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", GetUserID() );
			pEvent->SetString( "upgrade", s_pNames[upgrade] );
			gameeventmanager->FireEvent( pEvent );
		}
		return false;
	}
	if ( !pPrimary->AddUpgrade( upgrade ) )
		return false;

	// S:0x2418D0 is a single-user script-sound emitter, not PAS audio.
	CSingleUserRecipientFilter filter( this );
	EmitSound( filter, entindex(), "Player.AwardUpgrade" );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "receive_upgrade" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		pEvent->SetString( "upgrade", s_pNames[upgrade] );
		gameeventmanager->FireEvent( pEvent );
	}
	char modifiers[64];
	Q_snprintf( modifiers, sizeof( modifiers ), "ItemPickedUp:%s", s_pPickupNames[upgrade] );
	SpeakIfAllowed( "PlayerPickup", 3, modifiers, NULL, 0, NULL );
	return true;
}

// Native server 0x3563f0; 0x5c443c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace upgrade_ammo_explosivePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "upgrade_ammo_explosive", 0x5c443c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "upgrade_ammo_explosive", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3563c0; 0x5c4434 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace upgrade_ammo_incendiaryPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "upgrade_ammo_incendiary", 0x5c4434 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "upgrade_ammo_incendiary", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x356420; 0x5c4444 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace upgrade_laser_sightPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "upgrade_laser_sight", 0x5c4444 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "upgrade_laser_sight", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#include "l4d2/weapons/upgrade_item_console_server_callbacks.inc"
