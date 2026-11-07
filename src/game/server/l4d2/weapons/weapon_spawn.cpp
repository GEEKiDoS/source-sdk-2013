//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_spawn.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_gun.h"
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/weapons/weapon_carried_item.h"
#include "l4d2/effects/glowproperty.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "ammodef.h"
#include "entitylist.h"
#include "l4d2/entities/visibility_monitor.h"
#include "tier1/strtools.h"

#include "l4d2/entities/terror_transition_entities.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

// S:29E980/29E360: construction order, including unspawned entities, is the
// order used by the pistol's dropped-weapon absorption search.
static CUtlVector< CWeaponSpawn * > g_WeaponSpawns;

const CUtlVector< CWeaponSpawn * > &CWeaponSpawn::GetWeaponSpawns()
{
	return g_WeaponSpawns;
}

IMPLEMENT_SERVERCLASS_ST( CWeaponSpawn, DT_WeaponSpawn )
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),
	SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
	SendPropExclude( "DT_BaseAnimating", "m_nNewSequenceParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nResetEventsParity" ),
	SendPropExclude( "DT_BaseAnimating", "m_nMuzzleFlashParity" ),
	SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),
	SendPropExclude( "DT_BaseFlex", "m_flexWeight" ),
	SendPropExclude( "DT_BaseFlex", "m_blinktoggle" ),
	SendPropExclude( "DT_ServerAnimationData", "m_flCycle" ),
	SendPropExclude( "DT_AnimTimeMustBeFirst", "m_flAnimTime" ),
	SendPropInt( SENDINFO( m_weaponID ), 8, 0 ),
	SendPropFloat( SENDINFO( m_flGlowRange ), 0, SPROP_NOSCALE ),
END_SEND_TABLE()

IMPLEMENT_SERVERCLASS_ST( CWeaponAmmoSpawn, DT_WeaponAmmoSpawn )
END_SEND_TABLE()

IMPLEMENT_SERVERCLASS_ST( CWeaponScavengeItemSpawn, DT_ScavengeItemSpawn )
END_SEND_TABLE()

BEGIN_DATADESC( CWeaponSpawn )
	DEFINE_FIELD( m_weaponID, FIELD_INTEGER ),
	DEFINE_KEYFIELD( m_itemCount, FIELD_INTEGER, "count" ),
	DEFINE_KEYFIELD( m_nWeaponSkin, FIELD_INTEGER, "weaponskin" ),
	DEFINE_KEYFIELD( m_flGlowRange, FIELD_FLOAT, "glowrange" ),
END_DATADESC()

BEGIN_DATADESC( CWeaponSpawnConfigurable )
	DEFINE_KEYFIELD( m_iszWeaponToSpawn, FIELD_STRING, "weapon_selection" ),
	DEFINE_KEYFIELD( m_bSpawnWithoutDirector, FIELD_BOOLEAN, "spawn_without_director" ),
	DEFINE_KEYFIELD( m_bNoCSWeapons, FIELD_BOOLEAN, "no_cs_weapons" ),
END_DATADESC()

BEGIN_DATADESC( CWeaponAmmoSpawn )
END_DATADESC()

BEGIN_DATADESC( CWeaponMeleeSpawn )
	DEFINE_KEYFIELD( m_iszMeleeWeapon, FIELD_STRING, "melee_weapon" ),
END_DATADESC()

BEGIN_DATADESC( CWeaponScavengeItemSpawn )
	DEFINE_KEYFIELD( m_iGlowState, FIELD_INTEGER, "glowstate" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SpawnItem", InputSpawnScavengeItem ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnGlowsOn", InputTurnGlowsOn ),
	DEFINE_INPUTFUNC( FIELD_VOID, "TurnGlowsOff", InputTurnGlowsOff ),
	DEFINE_OUTPUT( m_OnItemPickedUp, "OnItemPickedUp" ),
END_DATADESC()

LINK_ENTITY_TO_CLASS( weapon_spawn, CWeaponSpawnConfigurable );
LINK_ENTITY_TO_CLASS( weapon_ammo_spawn, CWeaponAmmoSpawn );
LINK_ENTITY_TO_CLASS( weapon_melee_spawn, CWeaponMeleeSpawn );
LINK_ENTITY_TO_CLASS( weapon_scavenge_item_spawn, CWeaponScavengeItemSpawn );

// The IDs are the fixed original aliases, never a registration ordinal.
// The replacement pairs are the original scavenge-mode conversions.
#define TERROR_FIXED_WEAPON_SPAWN( classname, cppclass, weaponID, replacementID, replacementName ) \
class cppclass : public CWeaponSpawn \
{ \
public: \
	DECLARE_CLASS( cppclass, CWeaponSpawn ); \
	virtual int GetWeaponID() const OVERRIDE { return weaponID; } \
	virtual int GetReplacementWeaponID() const OVERRIDE { return replacementID; } \
	virtual const char *GetReplacementEntityName() const OVERRIDE { return replacementName; } \
}; \
LINK_ENTITY_TO_CLASS( classname, cppclass )

TERROR_FIXED_WEAPON_SPAWN( weapon_pistol_spawn, CWeaponpistolSpawn, TERROR_WEAPON_PISTOL, 0, "weapon_pistol_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_pistol_magnum_spawn, CWeaponpistol_magnumSpawn, TERROR_WEAPON_PISTOL_MAGNUM, 0, "weapon_pistol_magnum_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_smg_spawn, CWeaponsmgSpawn, TERROR_WEAPON_SMG, TERROR_WEAPON_RIFLE, "weapon_rifle_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_smg_silenced_spawn, CWeaponsmg_silencedSpawn, TERROR_WEAPON_SMG_SILENCED, TERROR_WEAPON_RIFLE_DESERT, "weapon_rifle_desert_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_smg_mp5_spawn, CWeaponsmg_mp5Spawn, TERROR_WEAPON_SMG_MP5, 0, "weapon_smg_mp5_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_rifle_spawn, CWeaponrifleSpawn, TERROR_WEAPON_RIFLE, 0, "weapon_rifle_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_rifle_ak47_spawn, CWeaponrifle_ak47Spawn, TERROR_WEAPON_RIFLE_AK47, 0, "weapon_rifle_ak47_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_rifle_desert_spawn, CWeaponrifle_desertSpawn, TERROR_WEAPON_RIFLE_DESERT, 0, "weapon_rifle_desert_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_rifle_sg552_spawn, CWeaponrifle_sg552Spawn, TERROR_WEAPON_RIFLE_SG552, 0, "weapon_rifle_sg552_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_rifle_m60_spawn, CWeaponRifle_M60Spawn, TERROR_WEAPON_RIFLE_M60, 0, NULL );
TERROR_FIXED_WEAPON_SPAWN( weapon_pumpshotgun_spawn, CWeaponpumpshotgunSpawn, TERROR_WEAPON_PUMPSHOTGUN, TERROR_WEAPON_AUTOSHOTGUN, "weapon_autoshotgun_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_shotgun_chrome_spawn, CWeaponshotgun_chromeSpawn, TERROR_WEAPON_SHOTGUN_CHROME, TERROR_WEAPON_SHOTGUN_SPAS, "weapon_shotgun_spas_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_autoshotgun_spawn, CWeaponautoshotgunSpawn, TERROR_WEAPON_AUTOSHOTGUN, 0, "weapon_autoshotgun_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_shotgun_spas_spawn, CWeaponshotgun_spasSpawn, TERROR_WEAPON_SHOTGUN_SPAS, 0, "weapon_shotgun_spas_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_hunting_rifle_spawn, CWeaponhunting_rifleSpawn, TERROR_WEAPON_HUNTING_RIFLE, 0, "weapon_hunting_rifle_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_sniper_military_spawn, CWeaponsniper_militarySpawn, TERROR_WEAPON_SNIPER_MILITARY, 0, "weapon_sniper_military_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_sniper_scout_spawn, CWeaponsniper_scoutSpawn, TERROR_WEAPON_SNIPER_SCOUT, 0, "weapon_sniper_scout_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_sniper_awp_spawn, CWeaponsniper_awpSpawn, TERROR_WEAPON_SNIPER_AWP, 0, "weapon_sniper_awp_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_pipe_bomb_spawn, CWeaponpipe_bombSpawn, TERROR_WEAPON_PIPE_BOMB, 0, "weapon_pipe_bomb_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_molotov_spawn, CWeaponmolotovSpawn, TERROR_WEAPON_MOLOTOV, 0, "weapon_molotov_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_vomitjar_spawn, CWeaponvomitjarSpawn, TERROR_WEAPON_VOMITJAR, 0, "weapon_vomitjar_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_grenade_launcher_spawn, CWeaponGrenade_LauncherSpawn, TERROR_WEAPON_GRENADE_LAUNCHER, 0, NULL );
TERROR_FIXED_WEAPON_SPAWN( weapon_first_aid_kit_spawn, CWeaponfirst_aid_kitSpawn, TERROR_WEAPON_FIRST_AID_KIT, 0, "weapon_first_aid_kit_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_pain_pills_spawn, CWeaponpain_pillsSpawn, TERROR_WEAPON_PAIN_PILLS, 0, "weapon_pain_pills_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_adrenaline_spawn, CWeaponadrenalineSpawn, TERROR_WEAPON_ADRENALINE, 0, "weapon_adrenaline_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_defibrillator_spawn, CWeapondefibrillatorSpawn, TERROR_WEAPON_DEFIBRILLATOR, 0, "weapon_defibrillator_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_upgradepack_incendiary_spawn, CWeaponupgradepack_incendiarySpawn, TERROR_WEAPON_UPGRADEPACK_INCENDIARY, 0, "weapon_upgradepack_incendiary_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_upgradepack_explosive_spawn, CWeaponupgradepack_explosiveSpawn, TERROR_WEAPON_UPGRADEPACK_EXPLOSIVE, 0, "weapon_upgradepack_explosive_spawn" );
TERROR_FIXED_WEAPON_SPAWN( weapon_gascan_spawn, CWeapongascanSpawn, TERROR_WEAPON_GASCAN, 0, "weapon_gascan_spawn" );
#undef TERROR_FIXED_WEAPON_SPAWN

// S:29F340/29F350: the historical HE spawner really gives a pipe bomb.
class CWeaponHEGrenadeSpawn : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponHEGrenadeSpawn, CWeaponSpawn );
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PIPE_BOMB; }
	virtual const char *GetWeaponModel() const OVERRIDE { return "models/w_models/weapons/w_eq_pipebomb.mdl"; }
};
LINK_ENTITY_TO_CLASS( weapon_hegrenade_spawn, CWeaponHEGrenadeSpawn );

class CWeaponChainsawSpawn : public CWeaponSpawn
{
public:
	DECLARE_CLASS( CWeaponChainsawSpawn, CWeaponSpawn );
	DECLARE_DATADESC();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_CHAINSAW; }
	virtual const char *GetResponseRulesName() const OVERRIDE { return "Chainsaw"; } // S29CC70
	virtual const char *GetWeaponModel() const OVERRIDE
	{
		const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( TERROR_WEAPON_CHAINSAW );
		return pInfo ? pInfo->szWorldModel : "";
	}
};
LINK_ENTITY_TO_CLASS( weapon_chainsaw_spawn, CWeaponChainsawSpawn );

CWeaponSpawn::CWeaponSpawn()
{
	g_WeaponSpawns.AddToTail( this );
	m_weaponID = 0;
	m_nWeaponSkin = -1;
	m_flGlowRange = 0.0f;
	m_bReplacement = false;
}

CWeaponSpawn::~CWeaponSpawn()
{
	g_WeaponSpawns.FindAndRemove( this );
}

void CWeaponSpawn::SetWeaponID( int id )
{
	m_weaponID = id;
}

const char *CWeaponSpawn::GetWeaponName() const
{
	const int id = GetWeaponID();
	return id ? WeaponIDToAlias( id ) : ""; // S29CA50 has its OWN ID0 guard.
}

const char *CWeaponSpawn::GetWeaponModel() const
{
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( GetWeaponID() );
	if ( !pInfo )
		return "";
	return pInfo->m_szWorldModel[0] ? pInfo->m_szWorldModel : pInfo->szWorldModel;
}

const char *CWeaponSpawn::GetResponseRulesName() const
{
	const int id = GetWeaponID(); // S29CF30, not alias/subtype/model.
	const CTerrorWeaponInfo *pInfo = id ? GetTerrorWeaponInfo( id ) : NULL;
	return pInfo ? pInfo->m_szResponseRulesName : "";
}

void CWeaponSpawn::Precache()
{
	PrecacheTerrorTransitionEntities(); // S29C9F0 -> S254980.
	const char *pszModel = GetWeaponModel();
	if ( pszModel && pszModel[0] )
		PrecacheModel( pszModel );
	CBaseAnimating::Precache();
}

void CWeaponSpawn::SetSpawnModel()
{
	SetModel( GetWeaponModel() );
	SetViewOffset( WorldSpaceCenter() - GetAbsOrigin() );
	if ( m_nWeaponSkin >= 0 )
		m_nSkin = m_nWeaponSkin;
}



static bool WeaponSpawnVisible( CBaseEntity *entity, CBasePlayer *observer )
{
	// S29E090. False suppresses entity_visible, not the monitor's seen bit.
	CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entity );
	IGameEvent *event = gameeventmanager->CreateEvent( "weapon_spawn_visible" );
	if ( event && spawn )
	{
		if ( observer )
			event->SetInt( "userid", observer->GetUserID() );
		event->SetInt( "subject", spawn->entindex() );
		event->SetString( "weaponname", spawn->GetWeaponName() );
		event->SetString( "subtype", spawn->GetWeaponSubType() );
		gameeventmanager->FireEvent( event );
	}
	return false;
}

static bool WeaponSpawnVisibilityPredicate( CBaseEntity *entity, CBasePlayer *observer )
{
	const bool player = observer && observer->IsPlayer();
	CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( entity );
	if ( !player || !spawn )
		return false;
	const char *classname = WeaponIDToClassname( spawn->GetWeaponID() );
	if ( !classname )
		return true;
	CBaseCombatWeapon *weapon = observer->Weapon_OwnsThisType( classname, 0 );
	if ( !weapon )
		return true;
	CTerrorGun *gun = weapon->GetTerrorGun();
	return !gun || ( gun->CanDualWield() && !gun->IsDualWielding() ); // S2FCEF0.
}

void CWeaponSpawn::RegisterVisibility( float flRange, VisibilityMonitorCallback_t predicate )
{
	VisibilityMonitor_AddEntity( this, flRange, WeaponSpawnVisible, predicate ? predicate : WeaponSpawnVisibilityPredicate );
}

void CWeaponSpawn::UnregisterVisibility()
{
	VisibilityMonitor_RemoveEntity( this );
}

bool CWeaponSpawn::ReplaceWithScriptedWeapon()
{
	// S:2A03A0. Conversions are genuine Director script decisions; there is
	// no local random replacement or success fallback for an absent Director.
	if ( m_bReplacement || !GetWeaponID() || HasSpawnFlags( SF_WEAPON_SPAWN_INFINITE ) )
		return false;
	int id = GetWeaponID();
	CHandle< CWeaponSpawn > previous = this;
	const char *pszReplacement = NULL;
	if ( TerrorGameRules()->IsScavengeMode() && GetReplacementWeaponID() )
	{
		id = GetReplacementWeaponID();
		pszReplacement = GetReplacementEntityName();
	}
	char szConverted[64];
	if ( TerrorGameRules()->GetWeaponSpawnReplacement( id, szConverted, sizeof( szConverted ) ) )
		pszReplacement = szConverted;
	if ( !pszReplacement )
		return false;
	CBaseEntity *pEntity = CreateEntityByName( pszReplacement );
	if ( !pEntity )
		return false;
	CWeaponSpawn *pPrevious = previous.Get();
	if ( pPrevious )
	{
		pEntity->SetAbsOrigin( pPrevious->GetAbsOrigin() );
		pEntity->SetAbsAngles( pPrevious->GetAbsAngles() );
		pEntity->SetName( pPrevious->GetEntityName() );
		pEntity->AddSpawnFlags( pPrevious->GetSpawnFlags() );
		pEntity->AddEffects( pPrevious->GetEffects() );
	}
	CWeaponSpawn *pSpawn = dynamic_cast< CWeaponSpawn * >( pEntity );
	if ( pSpawn )
	{
		if ( pPrevious )
			pSpawn->m_itemCount = pPrevious->m_itemCount ? pPrevious->m_itemCount : 1;
		pSpawn->m_bReplacement = true;
	}
	EHANDLE replacement = pEntity;
	pEntity->Precache();
	if ( replacement.Get() )
		DispatchSpawn( replacement.Get() );
	if ( previous.Get() )
		UTIL_Remove( previous.Get() );
	return true;
}

void CWeaponSpawn::Spawn()
{
	// S:2A2350. This base's original v237 is true for all implemented leaves;
	// the engine's other population gate is consequently short-circuited.
	CHandle< CWeaponSpawn > self = this;
	const bool bBlocked = !HasSpawnFlags( SF_WEAPON_SPAWN_INFINITE ) && !TerrorGameRules()->AllowWeaponSpawn( GetWeaponID() );
	if ( !self.Get() )
		return;
	if ( bBlocked )
	{
		if ( GetWeaponID() == TERROR_WEAPON_AMMO )
		{
			for ( CBaseEntity *pProp = gEntList.FindEntityByClassname( NULL, "prop_dynamic" ); pProp; )
			{
				CBaseEntity *pNext = gEntList.FindEntityByClassname( pProp, "prop_dynamic" );
				if ( pProp->GetModelName() == GetModelName() )
					UTIL_Remove( pProp );
				pProp = pNext;
			}
		}
		UTIL_Remove( this );
		return;
	}
	const bool bConverted = ReplaceWithScriptedWeapon();
	if ( bConverted || !self.Get() )
		return;
	m_flFadeScale = 0.0f;
	if ( CBaseEntity::IsPrecacheAllowed() )
		Precache();
	SetSpawnModel();
	m_weaponID = GetWeaponID();
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( m_weaponID );
	if ( pInfo && ( pInfo->m_WeaponType == TERROR_WEAPONTYPE_HEALING || pInfo->m_WeaponType == TERROR_WEAPONTYPE_REVIVING || pInfo->m_WeaponType == TERROR_WEAPONTYPE_UPGRADEPACK ) )
		m_itemCount = 1;

	// S2A24C6..2A2523 temporarily inverts raw flag1 for the SOLE native
	// CItem::Spawn substrate (its flag1 requests the fixed constraint), then
	// restores the mapper's physics flag. Do not copy that substrate here.
	const bool bPhysics = HasSpawnFlags( SF_WEAPON_SPAWN_PHYSICS );
	if ( bPhysics )
		RemoveSpawnFlags( SF_WEAPON_SPAWN_PHYSICS );
	else
		AddSpawnFlags( SF_WEAPON_SPAWN_PHYSICS );
	BaseClass::Spawn();
	if ( bPhysics )
		AddSpawnFlags( SF_WEAPON_SPAWN_PHYSICS );
	else
		RemoveSpawnFlags( SF_WEAPON_SPAWN_PHYSICS );
	if ( !bPhysics )
		SetMoveType( MOVETYPE_NONE );
	if ( m_itemCount <= 0 )
	{
		if ( pInfo && ( pInfo->iSlot == 0 || pInfo->iSlot == 2 ) )
			AddEffects( EF_NODRAW );
		else
			UTIL_Remove( this );
	}
	if ( m_itemCount > 0 )
		RegisterVisibility();
	m_initialItemCount = m_itemCount;
	m_takedamage = DAMAGE_NO;
	AddEffects( EF_NOSHADOW );
}


int CWeaponSpawn::ObjectCaps()
{
	return IsEffectActive( EF_NODRAW ) ? FCAP_WCEDIT_POSITION : FCAP_WCEDIT_POSITION | FCAP_ACROSS_TRANSITION | FCAP_IMPULSE_USE | FCAP_USE_IN_RADIUS;
}

void CWeaponSpawn::ItemTouch( CBaseEntity *pOther )
{
	// S:10E3B0's vehicle/occlusion/OnCacheInteraction/permission order is
	// identical to the reached native CItem path. MyTouch is always false
	// for these spawners (S:29E3D0), so Use alone performs acquisition.
	CItem::ItemTouch( pOther );
}

bool CWeaponSpawn::ShouldConsumeCount( const CTerrorWeaponInfo *pInfo ) const
{
	// S:29E050: versus primary piles do not consume a count.
	return !( TerrorGameRules()->IsVersusMode() && pInfo && pInfo->iSlot == 0 ) && !HasSpawnFlags( SF_WEAPON_SPAWN_INFINITE );
}

bool CWeaponSpawn::CanReplaceWeapon( CTerrorWeapon *pWeapon ) const
{
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( GetWeaponID() );
	if ( !pInfo )
		return false;
	return GetWeaponID() == TERROR_WEAPON_CHAINSAW || pInfo->m_WeaponType == TERROR_WEAPONTYPE_CARRIED_PROP || !FClassnameIs( pWeapon, pInfo->szClassName );
}

CBaseEntity *CWeaponSpawn::GiveToPlayer( CTerrorPlayer *pPlayer, const char *pszClassname )
{
	// S:29E410: skin changes happen after acquisition, not on the prototype.
	CHandle< CWeaponSpawn > self = this;
	CBaseEntity *pItem = pPlayer->GiveNamedItemInternal( pszClassname, 0, true, NULL );
	CWeaponSpawn *pSpawn = self.Get();
	if ( pSpawn && pSpawn->m_nWeaponSkin >= 0 && pItem && pItem->GetBaseAnimating() )
	{
		pItem->GetBaseAnimating()->m_nSkin = pSpawn->m_nWeaponSkin;
		CBaseCombatWeapon *pWeapon = dynamic_cast< CBaseCombatWeapon * >( pItem );
		if ( pWeapon )
			pWeapon->SetViewModel();
		if ( pSpawn->m_nWeaponSkin == 1 && !Q_stricmp( pszClassname, "crowbar" ) )
		{
			IGameEvent *pEvent = gameeventmanager->CreateEvent( "golden_crowbar_pickup" );
			if ( pEvent )
				gameeventmanager->FireEvent( pEvent );
		}
	}
	return pItem;
}

void CWeaponSpawn::RetireEmptySpawn()
{
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( m_weaponID );
	if ( pInfo && ( pInfo->iSlot == 0 || pInfo->iSlot == 2 ) )
		AddEffects( EF_NODRAW );
	else
		UTIL_Remove( this );
	TerrorGameRules()->RemoveItemFromSpawnManager( this );
	UnregisterVisibility();
}

void CWeaponSpawn::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// S:29DCB0. Do not prepend target-CItem disabled/count/touch policies.
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( !pPlayer )
		return;
	const int id = GetWeaponID();
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( id );
	if ( !pInfo || !pPlayer->IsWeaponAllowedForTeam( id ) )
		return;
	CHandle< CWeaponSpawn > self = this;
	CHandle< CTerrorPlayer > player = pPlayer;
	const char *szClassname = WeaponIDToClassname( id );
	CTerrorWeapon *pOwned = pPlayer->GetWeaponOfType( id );
	bool bGiveItem = true;
	if ( pOwned )
	{
		int nAmmo = FClassnameIs( pOwned, szClassname ) ? pPlayer->GetAmmoCount( pInfo->iAmmoType ) : 0;
		const bool bNeedsClip = pOwned->Clip1() <= 0 || pOwned->Clip1() < pOwned->GetMaxClip1();
		CTerrorGun *pGun = pOwned->GetTerrorGun();
		if ( pGun && pGun->CanDualWield() )
		{
			if ( pGun->IsDualWielding() )
			{
				if ( nAmmo < GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ) )
					pPlayer->GiveAmmo( GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ), pInfo->iAmmoType );
				return;
			}
			if ( !pGun->AcquireDualWeapons() )
				return;
			pPlayer->OnPickedUpWeapon( pGun );
			bGiveItem = false;
		}
		else if ( pGun )
		{
			if ( FClassnameIs( pOwned, szClassname ) && nAmmo >= GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ) && !bNeedsClip )
				return;
		}
		else if ( !CanReplaceWeapon( pOwned ) )
			return;
	}
	if ( bGiveItem )
	{
		if ( !GiveToPlayer( pPlayer, szClassname ) )
			return;
		pPlayer = player.Get();
		if ( self.Get() && pPlayer && pPlayer->IsFakeClient() && ( pInfo->m_WeaponType == TERROR_WEAPONTYPE_HEALING || pInfo->m_WeaponType == TERROR_WEAPONTYPE_REVIVING ) )
		{
			Vector mins, maxs;
			self->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			CBaseEntity *pCabinet = gEntList.FindEntityByClassnameWithin( NULL, "prop_health_cabinet", mins, maxs );
			if ( pCabinet )
				pCabinet->Use( pPlayer, pPlayer, USE_TOGGLE, 0.0f );
		}
	}
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "spawner_give_item" );
	if ( pEvent )
	{
		if ( player.Get() )
			pEvent->SetInt( "userid", player->GetUserID() );
		pEvent->SetString( "item", szClassname );
		if ( self.Get() )
			pEvent->SetInt( "spawner", self->entindex() );
		gameeventmanager->FireEvent( pEvent );
	}
	pPlayer = player.Get();
	if ( pPlayer )
		pPlayer->GiveAmmo( GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ), pInfo->iAmmoType );
	CWeaponSpawn *pSpawn = self.Get();
	if ( pSpawn && pSpawn->m_itemCount )
	{
		if ( pSpawn->ShouldConsumeCount( GetTerrorWeaponInfo( pSpawn->m_weaponID ) ) )
			pSpawn->m_itemCount = (int)( (unsigned int)pSpawn->m_itemCount - 1U );
		if ( !pSpawn->m_itemCount )
			pSpawn->RetireEmptySpawn();
	}
}

int CWeaponSpawn::GetUseType( CBaseEntity *user ) // S29CDF0, entity use-policy dispatch.
{
	CBasePlayer *pPlayer = ToBasePlayer( user );
	if ( !pPlayer )
		return 0;
	const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( m_weaponID );
	const int nMaxCarry = pInfo ? GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ) : -1;
	return GetTerrorWeaponGlowType( pPlayer, m_weaponID, -1, nMaxCarry, NULL, false );
}

bool CWeaponSpawn::AbsorbDroppedWeapon( CTerrorWeapon *pWeapon )
{
	// S:2A0520. HasAnyUpgrades deliberately retains its mutating gun mask.
	if ( !pWeapon || ( ( !HasSpawnFlags( SF_WEAPON_SPAWN_ABSORB_ANY ) || pWeapon->GetSlot() == 2 ) && pWeapon->HasAnyUpgrades() ) || ( pWeapon->GetSlot() == 2 && m_initialItemCount == m_itemCount ) )
		return false;
	if ( !m_itemCount )
	{
		RegisterVisibility();
		RemoveEffects( EF_NODRAW );
	}
	m_itemCount = (int)( (unsigned int)m_itemCount + 1U );
	TerrorGameRules()->RemoveItemFromSpawnManager( pWeapon );
	UTIL_Remove( pWeapon );
	return true;
}

void AssociateDroppedTerrorWeaponWithSpawn( CTerrorWeapon *pNewWeapon, const CTerrorWeapon *pDroppedFrom )
{
	// S:3DF110: use the dropped-from pistol's position, not the split pistol.
	for ( int i = 0; i < g_WeaponSpawns.Count(); ++i )
	{
		CWeaponSpawn *pSpawn = g_WeaponSpawns[i];
		if ( pSpawn->GetWeaponID() == TERROR_WEAPON_PISTOL && ( pSpawn->GetAbsOrigin() - pDroppedFrom->GetAbsOrigin() ).LengthSqr() < 40000.0f && pSpawn->AbsorbDroppedWeapon( pNewWeapon ) )
			break;
	}
}

bool MergeDroppedTerrorWeaponWithSpawn( CTerrorWeapon *pWeapon )
{
	// S:247710: ordinary inventory drop uses centers and matching skins;
	// this is deliberately different from the split-pistol origin search.
	for ( int i = 0; i < g_WeaponSpawns.Count(); ++i )
	{
		CWeaponSpawn *pSpawn = g_WeaponSpawns[i];
		if ( ( ( pSpawn->GetWeaponID() == pWeapon->GetWeaponID() && pSpawn->m_nSkin == pWeapon->m_nSkin ) || pSpawn->HasSpawnFlags( SF_WEAPON_SPAWN_ABSORB_ANY ) ) &&
			( pSpawn->WorldSpaceCenter() - pWeapon->WorldSpaceCenter() ).LengthSqr() < 40000.0f && pSpawn->AbsorbDroppedWeapon( pWeapon ) )
			return true;
	}
	return false;
}


int CWeaponSpawnConfigurable::ParseWeaponSelection()
{
	// S:29D130. An omitted key does not acquire the FGD's authoring default.
	m_requestedWeaponID = 0;
	m_requestedTier = 0;
	m_requestedType = TERROR_WEAPONTYPE_UNKNOWN;
	m_requestedSlot = -1;
	const char *pszSelection = STRING( m_iszWeaponToSpawn );
	if ( !pszSelection[0] )
	{
		UTIL_Remove( this );
		return m_selectionType;
	}
	if ( !Q_strnicmp( pszSelection, "any_", 4 ) )
	{
		const char *pszType = pszSelection + 4;
		if ( !Q_strcmp( pszType, "pistol" ) )
		{
			m_requestedSlot = 1;
			m_requestedType = TERROR_WEAPONTYPE_PISTOL;
		}
		else if ( !Q_strcmp( pszType, "primary" ) )
			m_requestedSlot = 0;
		else
			m_requestedType = TerrorWeaponTypeFromName( pszType );
	}
	else if ( !Q_strnicmp( pszSelection, "tier", 4 ) )
	{
		int tier = 0;
		char szType[32];
		int parsed = 0;
		if ( sscanf( pszSelection, "tier%d_%31s%n", &tier, szType, &parsed ) == 2 )
		{
			if ( (unsigned int)tier <= 2U )
				m_requestedTier = tier;
			// Main-approved S29D130 overflow disposition: retain the tier,
			// skip only the type assignment if the native token exceeds31.
			const char next = pszSelection[parsed];
			if ( !next || next == ' ' || next == '\t' || next == '\n' ||
				next == '\r' || next == '\f' || next == '\v' )
				m_requestedType = TerrorWeaponTypeFromName( szType );
		}
	}
	else
	{
		for ( int id = 1; id < 56; ++id )
		{
			const char *pszClassname = WeaponIDToClassname( id );
			if ( pszClassname && !Q_strcmp( pszClassname, pszSelection ) )
				m_requestedWeaponID = id;
		}
	}
	m_selectionType = m_requestedWeaponID ? 4 : m_requestedTier ? ( m_requestedType == TERROR_WEAPONTYPE_UNKNOWN ? 2 : 3 ) : ( m_requestedType != TERROR_WEAPONTYPE_UNKNOWN ? 1 : 0 );
	return m_selectionType;
}

void CWeaponSpawnConfigurable::Spawn()
{
	// S:2A26E0. The Director-owned clustered selection is not substituted by
	// the unclustered random selection when spawn_without_director is false.
	CHandle< CWeaponSpawnConfigurable > self = this;
	if ( !m_weaponID )
	{
		ParseWeaponSelection();
		if ( !m_bSpawnWithoutDirector )
			TerrorGameRules()->RegisterWeaponSpawn( this );
	}
	if ( m_bSpawnWithoutDirector )
		SelectWeapon();
	if ( self.Get() )
		BaseClass::Spawn();
}

void CWeaponSpawnConfigurable::SetWeaponID( int id )
{
	BaseClass::SetWeaponID( id );
	if ( id == TERROR_WEAPON_GRENADE_LAUNCHER || id == TERROR_WEAPON_RIFLE_M60 )
		m_itemCount = 1;
}

bool CWeaponSpawnConfigurable::ShouldConsumeCount( const CTerrorWeaponInfo *pInfo ) const
{
	// S:29E170: the two super-weapons override even the infinite-items flag.
	return m_weaponID == TERROR_WEAPON_GRENADE_LAUNCHER || m_weaponID == TERROR_WEAPON_RIFLE_M60 || BaseClass::ShouldConsumeCount( pInfo );
}

void CWeaponSpawnConfigurable::SelectWeapon( const WeaponSpawnSelection_t *pNeighbors )
{
	// S:2A0660. Preserve candidate enumeration and RNG consumption order.
	CHandle< CWeaponSpawnConfigurable > self = this;
	int id = 0;
	if ( m_requestedWeaponID )
	{
		const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( m_requestedWeaponID );
		if ( !pInfo || !engine->IsModelPrecached( pInfo->m_szWorldModel[0] ? pInfo->m_szWorldModel : pInfo->szWorldModel ) )
			return;
		id = m_requestedWeaponID;
	}
	else
	{
		CUtlVector< int > used;
		int nNeighborTier = 0;
		if ( pNeighbors )
		{
			const WeaponSpawnSelection_t *pNeighborItem = pNeighbors;
			do
			{
				CWeaponSpawn *pNeighbor = dynamic_cast< CWeaponSpawn * >( pNeighborItem->entity.Get() );
				const int neighborID = pNeighbor ? pNeighbor->GetWeaponID() : 0;
				if ( neighborID && !pNeighbor->IsReplacement() )
				{
					const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( neighborID );
					if ( !nNeighborTier && pInfo )
						nNeighborTier = pInfo->m_iTier;
					if ( used.Find( neighborID ) == used.InvalidIndex() )
						used.AddToTail( neighborID );
				}
				pNeighborItem = pNeighborItem->next;
			}
			while ( pNeighborItem && pNeighborItem != pNeighbors );
		}
		CUtlVector< int > candidates;
		for ( int candidate = 1; candidate < 56; ++candidate )
		{
			if ( m_bNoCSWeapons && ( candidate == TERROR_WEAPON_SMG_MP5 || candidate == TERROR_WEAPON_RIFLE_SG552 || candidate == TERROR_WEAPON_SNIPER_SCOUT || candidate == TERROR_WEAPON_SNIPER_AWP ) )
				continue;
			if ( !TerrorGameRules()->CanSpawnCSWeapon( candidate, false ) )
				continue;
			const bool bAllowed = TerrorGameRules()->AllowWeaponSpawn( candidate );
			if ( !self.Get() )
				return;
			if ( !bAllowed )
				continue;
			const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( candidate );
			if ( !pInfo || (unsigned int)pInfo->iSlot > 1 || candidate == TERROR_WEAPON_GRENADE_LAUNCHER || candidate == TERROR_WEAPON_CHAINSAW || candidate == TERROR_WEAPON_RIFLE_M60 || pInfo->m_iTeam != TEAM_SURVIVOR )
				continue;
			if ( ( m_requestedSlot != -1 && pInfo->iSlot != m_requestedSlot ) || ( m_requestedTier && pInfo->m_iTier != m_requestedTier ) || ( nNeighborTier && pInfo->m_iTier != nNeighborTier ) || ( m_requestedType != TERROR_WEAPONTYPE_UNKNOWN && pInfo->m_WeaponType != m_requestedType ) )
				continue;
			const char *pszModel = pInfo->m_szWorldModel[0] ? pInfo->m_szWorldModel : pInfo->szWorldModel;
			if ( pInfo->m_WeaponType == TERROR_WEAPONTYPE_MELEE || !engine->IsModelPrecached( pszModel ) || used.Find( candidate ) != used.InvalidIndex() )
				continue;
			candidates.AddToTail( candidate );
		}
		if ( candidates.Count() > 0 )
			id = candidates[RandomInt( 0, candidates.Count() - 1 )];
	}
	char szConverted[64];
	const bool bConverted = TerrorGameRules()->GetWeaponSpawnReplacement( id, szConverted, sizeof( szConverted ) );
	if ( !self.Get() )
		return;
	if ( bConverted )
	{
		// S24EB90 exact _spawn-name lookup runs BEFORE S24EB00's first
		// case-insensitive alias SUBSTRING match. A converted smg_silenced
		// _spawn therefore maps7, while a nonexact string containing "smg"
		// can map2. Table entries do NOT imply native factories exist.
		static const char *const spawnNames[] =
		{
			"weapon_none", "weapon_pistol_spawn", "weapon_smg_spawn",
			"weapon_pumpshotgun_spawn", "weapon_autoshotgun_spawn", "weapon_rifle_spawn",
			"weapon_hunting_rifle_spawn", "weapon_smg_silenced_spawn", "weapon_shotgun_chrome_spawn",
			"weapon_rifle_desert_spawn", "weapon_sniper_military_spawn", "weapon_shotgun_spas_spawn",
			"weapon_first_aid_kit_spawn", "weapon_molotov_spawn", "weapon_pipe_bomb_spawn",
			"weapon_pain_pills_spawn", "weapon_gascan_spawn", "weapon_propanetank_spawn",
			"weapon_oxygentank_spawn", "weapon_melee_spawn", "weapon_chainsaw_spawn",
			"weapon_grenade_launcher_spawn", "weapon_ammo_pack_spawn", "weapon_adrenaline_spawn",
			"weapon_defibrillator_spawn", "weapon_vomitjar_spawn", "weapon_rifle_ak47_spawn",
			"weapon_gnome_spawn", "weapon_cola_bottles_spawn", "weapon_fireworkcrate_spawn",
			"weapon_upgradepack_incendiary_spawn", "weapon_upgradepack_explosive_spawn",
			"weapon_pistol_magnum_spawn", "weapon_smg_mp5_spawn", "weapon_rifle_sg552_spawn",
			"weapon_sniper_awp_spawn", "weapon_sniper_scout_spawn", "weapon_rifle_m60_spawn"
		};
		id = 0;
		for ( int candidate = 0; candidate < ARRAYSIZE( spawnNames ); ++candidate )
		{
			if ( !Q_stricmp( szConverted, spawnNames[candidate] ) )
			{
				id = candidate;
				break;
			}
		}
		if ( !id )
		{
			for ( int candidate = 0; candidate < 56; ++candidate )
			{
				const char *pszAlias = WeaponIDToAlias( candidate );
				if ( pszAlias && Q_stristr( szConverted, pszAlias ) )
				{
					id = candidate;
					break;
				}
			}
		}
		m_bReplacement = true;
	}
	SetWeaponID( id );
	if ( !m_weaponID )
	{
		UTIL_Remove( this );
		return;
	}
	SetSpawnModel();
	RegisterVisibility();
	CreateItemVPhysicsObject();
	if ( HasSpawnFlags( SF_WEAPON_SPAWN_START_CONSTRAINED ) )
		Constrain();
}

int CWeaponAmmoSpawn::GetWeaponID() const
{
	return TERROR_WEAPON_AMMO;
}

const char *CWeaponAmmoSpawn::GetWeaponModel() const
{
	return GetModelName() != NULL_STRING && STRING( GetModelName() )[0] ? STRING( GetModelName() ) : "models/props/terror/ammo_stack.mdl";
}

void CWeaponAmmoSpawn::Precache()
{
	PrecacheModel( "models/props/terror/ammo_stack.mdl" );
	BaseClass::Precache();
}

void CWeaponAmmoSpawn::Spawn()
{
	m_itemCount = 1;
	m_weaponID = TERROR_WEAPON_AMMO;
	BaseClass::Spawn();
}

void CWeaponAmmoSpawn::Use( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float value )
{
	// S:29F5F0. Refill the primary selection role, not the active weapon;
	// neither count nor magazine nor loaded upgraded rounds are refilled.
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pActivator );
	if ( !pPlayer )
		return;
	CTerrorWeapon *pWeapon = dynamic_cast< CTerrorWeapon * >( pPlayer->Weapon_GetSlot( 0 ) );
	if ( !pWeapon )
		return;
	const int id = pWeapon->GetWeaponID();
	if ( id == TERROR_WEAPON_GRENADE_LAUNCHER || id == TERROR_WEAPON_RIFLE_M60 )
	{
		IGameEvent *pEvent = gameeventmanager->CreateEvent( "ammo_pile_weapon_cant_use_ammo" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			gameeventmanager->FireEvent( pEvent );
		}
		return;
	}
	const int nAmmoType = pWeapon->GetPrimaryAmmoType();
	if ( !pWeapon->UsesPrimaryAmmo() || !GetAmmoDef()->GetAmmoOfIndex( nAmmoType ) )
		return;
	const int nTotalCapacity = (int)( (unsigned int)GetAmmoDef()->MaxCarry( nAmmoType, pPlayer ) + (unsigned int)pWeapon->GetMaxClip1() );
	int nClip = pWeapon->Clip1();
	const unsigned char nUpgraded = static_cast< unsigned char >( pWeapon->m_nUpgradedPrimaryAmmoLoaded.Get() );
	if ( nUpgraded )
		nClip = (int)( (unsigned int)nClip - (unsigned int)nUpgraded );
	if ( (int)( (unsigned int)nClip + (unsigned int)pPlayer->GetAmmoCount( nAmmoType ) ) < nTotalCapacity )
	{
		pPlayer->SetAmmoCount( (int)( (unsigned int)nTotalCapacity - (unsigned int)nClip ), nAmmoType );
		pPlayer->OnAmmoPickup();
	}
}

CWeaponMeleeSpawn::CWeaponMeleeSpawn()
{
	m_meleeWeapon = INVALID_MELEE_WEAPON_INFO_HANDLE;
}

int CWeaponMeleeSpawn::GetWeaponID() const
{
	return TERROR_WEAPON_MELEE;
}

const char *CWeaponMeleeSpawn::GetWeaponModel() const
{
	const CMeleeWeaponInfo *pInfo = MeleeWeaponInfoStore()->GetMeleeWeaponInfo( m_meleeWeapon );
	// S:29CAF0's original invalid-info model, not a replacement melee item.
	return pInfo ? pInfo->m_szPlayerModel : "models/props_junk/gascan001a.mdl";
}

const char *CWeaponMeleeSpawn::GetWeaponSubType() const
{
	const CMeleeWeaponInfo *pInfo = MeleeWeaponInfoStore()->GetMeleeWeaponInfo( m_meleeWeapon );
	return pInfo ? pInfo->m_szName : "";
}

const char *CWeaponMeleeSpawn::GetResponseRulesName() const
{
	const CMeleeWeaponInfo *pInfo = MeleeWeaponInfoStore()->GetMeleeWeaponInfo( m_meleeWeapon );
	return pInfo ? pInfo->m_szName : ""; // S29CB10, real selected melee entry.
}

bool CWeaponMeleeSpawn::CanReplaceWeapon( CTerrorWeapon *pWeapon ) const
{
	CTerrorMeleeWeapon *pMelee = dynamic_cast< CTerrorMeleeWeapon * >( pWeapon );
	return pMelee && Q_stricmp( GetWeaponSubType(), pMelee->GetMeleeWeaponScriptName() ) != 0;
}

CBaseEntity *CWeaponMeleeSpawn::GiveToPlayer( CTerrorPlayer *pPlayer, const char *pszClassname )
{
	if ( !pPlayer->IsWeaponAllowedForTeam( TERROR_WEAPON_MELEE ) )
		return NULL;
	const CMeleeWeaponInfo *pInfo = MeleeWeaponInfoStore()->GetMeleeWeaponInfo( m_meleeWeapon );
	return pInfo ? BaseClass::GiveToPlayer( pPlayer, pInfo->m_szName ) : NULL;
}

void CWeaponMeleeSpawn::SelectMeleeWeapon()
{
	// S:2A0CA0: only campaign-loaded scripts participate. Duplicates in the
	// comma list remain duplicates and hence preserve their random weight.
	m_meleeWeapon = INVALID_MELEE_WEAPON_INFO_HANDLE;
	const char *pszSelection = STRING( m_iszMeleeWeapon );
	if ( !Q_stricmp( pszSelection, "any" ) )
		m_meleeWeapon = MeleeWeaponInfoStore()->GetRandomMeleeWeaponInfo();
	else
	{
		CUtlVector< MeleeWeaponInfoHandle_t > candidates;
		char szName[256];
		const char *pszNext = nexttoken( szName, sizeof( szName ), pszSelection, ',' );
		while ( szName[0] )
		{
			MeleeWeaponInfoHandle_t handle = MeleeWeaponInfoStore()->GetMeleeWeaponInfoIndex( szName );
			if ( handle == INVALID_MELEE_WEAPON_INFO_HANDLE )
				Warning( "weapon_melee_spawn specifying an invalid melee weapon ( for this campaign? ) : %s\n", szName );
			else
				candidates.AddToTail( handle );
			pszNext = nexttoken( szName, sizeof( szName ), pszNext, ',' );
		}
		if ( !candidates.Count() )
		{
			Warning( "weapon_melee_spawn failed to spawn a melee weapon - using string '%s'\n", pszSelection );
			UTIL_Remove( this );
			return;
		}
		m_meleeWeapon = candidates[RandomInt( 0, candidates.Count() - 1 )];
	}
	if ( m_meleeWeapon == INVALID_MELEE_WEAPON_INFO_HANDLE )
	{
		Warning( "weapon_melee_spawn trying to spawn an invalid melee weapon type - using string '%s'\n", pszSelection );
		UTIL_Remove( this );
	}
}

void CWeaponMeleeSpawn::Spawn()
{
	// S:2A2730, unlike firearm spawners, converts an explicit zero to one.
	if ( !m_itemCount )
		m_itemCount = 1;
	if ( m_meleeWeapon == INVALID_MELEE_WEAPON_INFO_HANDLE )
		SelectMeleeWeapon();
	BaseClass::Spawn();
}

CWeaponScavengeItemSpawn::CWeaponScavengeItemSpawn()
{
	// S:29EBE0. The item handle and output have their native owners; the
	// output is fired by the carried item's actual pickup lifecycle.
	m_hItem = NULL;
	m_iGlowState = 3;
}

int CWeaponScavengeItemSpawn::GetWeaponID() const
{
	return TERROR_WEAPON_GASCAN;
}

void CWeaponScavengeItemSpawn::Spawn()
{
	// S:2A2820. Cluster selection, not source Spawn, creates the real item.
	CHandle< CWeaponScavengeItemSpawn > self = this;
	m_itemCount = 1;
	m_weaponID = TERROR_WEAPON_GASCAN;
	BaseClass::Spawn();
	if ( self.Get() )
		AddEffects( EF_NODRAW );
}

void CWeaponScavengeItemSpawn::InputSpawnScavengeItem( inputdata_t &inputdata )
{
	// S:29E580. Repeated calls intentionally replace the stored handle
	// without deleting or reparenting an already-created item.
	if ( !GetTerrorWeaponInfo( GetWeaponID() ) )
		return;
	const char *szClassname = WeaponIDToClassname( GetWeaponID() );
	CBaseEntity *pItem = CreateEntityByName( szClassname );
	if ( !pItem )
	{
		m_hItem = NULL;
		Msg( "Failed to spawn item by class %s\n", szClassname );
		UTIL_Remove( this );
		return;
	}
	m_hItem = pItem;
	pItem->SetAbsOrigin( GetAbsOrigin() );
	pItem->SetAbsAngles( GetAbsAngles() );
	pItem->Precache();
	Vector origin = pItem->GetAbsOrigin();
	origin.z += 12.0f;
	pItem->SetAbsOrigin( origin );
	DispatchSpawn( pItem );
	pItem->GlowProp()->SetType( m_iGlowState );
	pItem->GlowProp()->SetRange( 0.0f );
	Color glowColor;
	glowColor.SetRawColor( 0xFFFFFF );
	pItem->GlowProp()->SetColor( glowColor );
	CCarriedProp *pCarried = dynamic_cast< CCarriedProp * >( pItem );
	if ( pCarried )
		pCarried->SetWeaponSpawn( this );
	// Gas SetWeaponSpawn sets its own native skin before this authored
	// skin override. Moving this assignment earlier changes the result.
	if ( m_nWeaponSkin >= 0 )
		pItem->GetBaseAnimating()->m_nSkin = m_nWeaponSkin;
	Vector mins, maxs;
	pItem->CollisionProp()->WorldSpaceTriggerBounds( &mins, &maxs );
	const float flOffset = pItem->GetAbsOrigin().z - mins.z;
	if ( flOffset > 0.0f )
	{
		origin = pItem->GetAbsOrigin();
		origin.z += flOffset;
		pItem->SetAbsOrigin( origin );
	}
}

void CWeaponScavengeItemSpawn::SetGlowState( int state )
{
	// S:29E1C0 only changes type on the current valid native item handle.
	m_iGlowState = state;
	if ( m_hItem )
		m_hItem->GlowProp()->SetType( state );
}

void CWeaponScavengeItemSpawn::InputTurnGlowsOn( inputdata_t &inputdata )
{
	SetGlowState( 3 );
}

void CWeaponScavengeItemSpawn::InputTurnGlowsOff( inputdata_t &inputdata )
{
	SetGlowState( 0 );
}

// Native server 0x29cd50; 0x5c380c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_ammo_spawnPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_ammo_spawn", 0x5c380c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_ammo_spawn", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

BEGIN_DATADESC( CWeaponChainsawSpawn )
END_DATADESC()
