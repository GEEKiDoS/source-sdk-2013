//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "activitylist.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#define CItemBaseUpgradePack C_ItemBaseUpgradePack
#define CItemUpgradePackExplosive C_ItemUpgradePackExplosive
#define CItemUpgradePackIncendiary C_ItemUpgradePackIncendiary
#else
#include "l4d2/player/terrorplayer.h"
#include "igameevents.h"
#include "util.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/weapons/weapon_upgradepack_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar upgrade_pack_use_duration( "upgrade_pack_use_duration", "1.9", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
#ifndef CLIENT_DLL
ConVar upgradepack_forward_dist( "upgradepack_forward_dist", "30", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
ConVar upgradepack_max_drop_dist( "upgradepack_max_drop_dist", "30", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
#endif

class CItemBaseUpgradePack : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( CItemBaseUpgradePack, CBaseBackpackItem );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual void Precache() OVERRIDE;
	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode ) OVERRIDE;
	virtual float GetActionDuration() const OVERRIDE { return upgrade_pack_use_duration.GetFloat(); }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 0; }
#endif
	virtual int GetWeaponID() const OVERRIDE { return 0; }
	virtual bool IsPerformingAction() const OVERRIDE { return false; } // S3D36A0/C2FDCA0.
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual const char *GetUpgradeClassname() const { return NULL; }
};

class CItemUpgradePackExplosive : public CItemBaseUpgradePack
{
public:
	DECLARE_CLASS( CItemUpgradePackExplosive, CItemBaseUpgradePack );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_UPGRADEPACK_EXPLOSIVE; }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 7; }
#endif
	virtual const char *GetUpgradeClassname() const OVERRIDE { return "upgrade_ammo_explosive"; }
};

class CItemUpgradePackIncendiary : public CItemBaseUpgradePack
{
public:
	DECLARE_CLASS( CItemUpgradePackIncendiary, CItemBaseUpgradePack );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_UPGRADEPACK_INCENDIARY; }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 6; }
#endif
	virtual const char *GetUpgradeClassname() const OVERRIDE { return "upgrade_ammo_incendiary"; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( ItemBaseUpgradePack, DT_ItemBaseUpgradePack )
BEGIN_NETWORK_TABLE( CItemBaseUpgradePack, DT_ItemBaseUpgradePack )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItemBaseUpgradePack )
END_PREDICTION_DATA()

IMPLEMENT_NETWORKCLASS_ALIASED( ItemUpgradePackExplosive, DT_ItemUpgradePackExplosive )
BEGIN_NETWORK_TABLE( CItemUpgradePackExplosive, DT_ItemUpgradePackExplosive )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItemUpgradePackExplosive )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_upgradepack_explosive, CItemUpgradePackExplosive );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_upgradepack_explosive );
#endif

IMPLEMENT_NETWORKCLASS_ALIASED( ItemUpgradePackIncendiary, DT_ItemUpgradePackIncendiary )
BEGIN_NETWORK_TABLE( CItemUpgradePackIncendiary, DT_ItemUpgradePackIncendiary )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItemUpgradePackIncendiary )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_upgradepack_incendiary, CItemUpgradePackIncendiary );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_upgradepack_incendiary );
#endif

void CItemBaseUpgradePack::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Player.AmmoPackUse" );
}

CBaseEntity *CItemBaseUpgradePack::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
	return mode == 0 ? pPlayer : NULL;
}

void CItemBaseUpgradePack::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	pPlayer->EmitSound( "Player.AmmoPackUse" );
	pPlayer->SpeakIfAllowed( "PlayerDeployingUpgradeAmmo", 1, NULL, NULL, 0, NULL );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "upgrade_pack_begin" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
#endif
}

#ifndef CLIENT_DLL
void CItemBaseUpgradePack::OnActionStopped( CTerrorPlayer *pPlayer )
{
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_USE, 0 );
	pPlayer->StopSound( "Player.AmmoPackUse" );
}
#endif

#ifndef CLIENT_DLL
static bool IsUpgradePackMover( CBaseEntity *pEntity )
{
	return pEntity && ( FClassnameIs( pEntity, "func_movelinear" ) ||
		FClassnameIs( pEntity, "func_tracktrain" ) || FClassnameIs( pEntity, "func_elevator" ) );
}
#endif

#ifndef CLIENT_DLL
void CItemBaseUpgradePack::OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3D2630. The backpack owner has already detached/retired this item.
	const char *pszClassname = GetUpgradeClassname();
	if ( pszClassname && pszClassname[0] )
	{
		CBaseEntity *pUpgrade = CreateEntityByName( pszClassname );
		if ( !pUpgrade )
		{
			Msg( "Failed to spawn item by class %s\n", pszClassname );
			UTIL_Remove( this );
			return;
		}

		const Vector origin = pPlayer->GetAbsOrigin();
		const QAngle angles = pPlayer->GetAbsAngles();
		pUpgrade->SetAbsAngles( angles );
		pUpgrade->SetAbsOrigin( origin );
		DispatchSpawn( pUpgrade );
		Vector forward;
		AngleVectors( angles, &forward );
		const Vector forwardEnd = origin + forward * upgradepack_forward_dist.GetFloat();
		CTraceFilterSkipTwoEntities filter( pPlayer, pUpgrade, COLLISION_GROUP_DEBRIS );
		trace_t tr;
		UTIL_TraceEntity( pUpgrade, origin, forwardEnd, MASK_SOLID, &filter, &tr );
		const Vector dropEnd = tr.endpos - Vector( 0, 0, upgradepack_max_drop_dist.GetFloat() );
		UTIL_TraceEntity( pUpgrade, tr.endpos, dropEnd, MASK_SOLID, &filter, &tr );
		pUpgrade->SetAbsOrigin( tr.fraction < 1.0f ? tr.endpos : origin );

		if ( tr.DidHitNonWorldEntity() )
		{
			CBaseEntity *pGround = tr.m_pEnt;
			if ( IsUpgradePackMover( pGround ) ||
				( FClassnameIs( pGround, "prop_dynamic" ) && IsUpgradePackMover( pGround->GetMoveParent() ) ) )
			{
				// Parent to the hit prop, not to its moving parent.
				pUpgrade->SetParent( pGround );
			}
		}

		IGameEvent *pEvent = gameeventmanager->CreateEvent( "upgrade_pack_used" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", pPlayer->GetUserID() );
			pEvent->SetInt( "upgradeid", pUpgrade->entindex() );
			gameeventmanager->FireEvent( pEvent );
		}
	}

	const char *pszType = GetTerrorWpnData().m_szResponseRulesName;
	char modifiers[128];
	Q_snprintf( modifiers, sizeof( modifiers ), "Type:%s", pszType );
	pPlayer->SpeakIfAllowed( "PlayerUpgradeAmmoDeployed", 1, modifiers, NULL, 0, NULL );
}
#endif

static Activity UpgradePackActivity( const char *pszName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pszName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pszName ) : activity;
}

Activity CItemBaseUpgradePack::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Shove = UpgradePackActivity( "ACT_MELEE_SWEEP_UPGRADE_PACK" );
	static const Activity s_Primary = UpgradePackActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? s_Shove : s_Primary;
}

acttable_t *CItemBaseUpgradePack::ActivityList( int &iActivityCount )
{
	// S:0x7BA0E0: upgrade packs deliberately use the first-aid locomotion set.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_FIRSTAIDKIT" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_FIRSTAIDKIT" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_FIRSTAIDKIT" },
		{ "ACT_WALK", "ACT_WALK_FIRSTAIDKIT" },
		{ "ACT_RUN", "ACT_RUN_FIRSTAIDKIT" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_FIRSTAIDKIT" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_FIRSTAIDKIT" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_FIRSTAIDKIT" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_FIRSTAIDKIT" },
		{ "ACT_WALK_CALM", "ACT_WALK_FIRSTAIDKIT" },
		{ "ACT_RUN_CALM", "ACT_RUN_FIRSTAIDKIT" },
		{ "ACT_JUMP", "ACT_JUMP_ITEM" }
	};
	static acttable_t s_Table[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Table[i].baseAct = UpgradePackActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = UpgradePackActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

// Native server 0x3d3520; 0x5c4dc0 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_upgradepack_explosivePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_upgradepack_explosive", 0x5c4dc0 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_upgradepack_explosive", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

// Native server 0x3d3820; 0x5c4dd8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_upgradepack_incendiaryPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_upgradepack_incendiary", 0x5c4dd8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_upgradepack_incendiary", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CItemBaseUpgradePack )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CItemUpgradePackExplosive )
END_DATADESC()
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CItemUpgradePackIncendiary )
END_DATADESC()
#endif
