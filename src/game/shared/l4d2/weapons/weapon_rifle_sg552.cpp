//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_sniperrifle.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#ifdef L4D2
#include "l4d2/weapons/terror_precache_weapon.h"
#endif
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#define CRifle_SG552 C_Rifle_SG552
#endif

class CRifle_SG552 : public CBaseSniperRifle
{
public:
	DECLARE_CLASS( CRifle_SG552, CBaseSniperRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_RIFLE_SG552; }
	virtual bool IsFullyAutomatic() const OVERRIDE { return true; }
	virtual int GetZoomFOV() const OVERRIDE { return 55; }
#ifdef CLIENT_DLL
	virtual bool HideViewModelWhenZoomed() const OVERRIDE { return false; } // C3973070D0, client-only native policy.
#endif
	virtual double GetFireRate() OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};

// RTTI retains CBaseSniperRifle, but the original wire/prediction base is
// CTerrorGun. Do not insert an extra base table merely to mirror C++ ancestry.
IMPLEMENT_NETWORKCLASS_ALIASED( Rifle_SG552, DT_Rifle_SG552 )
BEGIN_NETWORK_TABLE_NOBASE( CRifle_SG552, DT_Rifle_SG552 )
#ifdef CLIENT_DLL
	RecvPropDataTable( "baseclass", 0, 0, CTerrorGun::m_pClassRecvTable, DataTableRecvProxy_StaticDataTable ),
#else
	SendPropDataTable( "baseclass", 0, CTerrorGun::m_pClassSendTable, SendProxy_DataTableToDataTable ),
#endif
END_NETWORK_TABLE()
#if defined( CLIENT_DLL ) && !defined( NO_ENTITY_PREDICTION )
datamap_t CRifle_SG552::m_PredMap = { 0, 0, "CRifle_SG552", &CTerrorGun::m_PredMap };
datamap_t *CRifle_SG552::GetPredDescMap() { return &m_PredMap; }
BEGIN_PREDICTION_DATA_GUTS( CRifle_SG552 )
END_PREDICTION_DATA()
#else
BEGIN_PREDICTION_DATA( CRifle_SG552 )
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_rifle_sg552, CRifle_SG552 );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_rifle_sg552 );
#endif

double CRifle_SG552::GetFireRate()
{
	// S:0x3E08D0 / C:0x307090. Test actual FOV before the inherited
	// incapacitated/empty-clip cycle getter; no weapon zoom bool.
	return IsZoomed() ? static_cast< double >( 0.13500001f ) : CTerrorGun::GetFireRate();
}

acttable_t *CRifle_SG552::ActivityList( int &iActivityCount )
{
	// S:0x3E07E0, table0x7BC710.
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_RIFLE" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_RIFLE" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_RIFLE" },
		{ "ACT_WALK", "ACT_WALK_RIFLE" },
		{ "ACT_RUN", "ACT_RUN_RIFLE" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_RIFLE" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_RIFLE" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_RIFLE" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_RIFLE" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_RIFLE" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_RIFLE" },
		{ "ACT_JUMP", "ACT_JUMP_RIFLE" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = SniperActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = SniperActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CRifle_SG552::TranslatePlayerActivity( Activity activity )
{
	// S:0x3E0860; MILITARYZOOMED names intentionally have no underscore.
	static const Activity s_ZoomActivities[][2] =
	{
		{ SniperActivity( "ACT_IDLE" ), SniperActivity( "ACT_IDLE_SNIPER_MILITARYZOOMED" ) },
		{ SniperActivity( "ACT_RUN" ), SniperActivity( "ACT_WALK_SNIPER_MILITARY" ) },
		{ SniperActivity( "ACT_CROUCHIDLE" ), SniperActivity( "ACT_CROUCHIDLE_SNIPER_MILITARYZOOMED" ) },
		{ SniperActivity( "ACT_IDLE_INJURED" ), SniperActivity( "ACT_IDLE_INJURED_SNIPER_MILITARYZOOMED" ) }
	};
	if ( IsZoomed() )
	{
		for ( int i = 0; i < ARRAYSIZE( s_ZoomActivities ); ++i )
		{
			if ( activity == s_ZoomActivities[i][0] )
				return s_ZoomActivities[i][1];
		}
	}
	return CTerrorGun::TranslatePlayerActivity( activity );
}

Activity CRifle_SG552::TranslateViewModelActivity( Activity activity )
{
	// S:0x3E0840: bypass hunting's family-form translator on fallback.
	static const Activity s_PrimaryLayer = SniperActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	if ( activity == ACT_VM_PRIMARYATTACK )
		return s_PrimaryLayer;
	return CTerrorGun::TranslateViewModelActivity( activity );
}

// Native server 0x3e0800; 0x5c5008 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_rifle_sg552Precache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_rifle_sg552", 0x5c5008 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_rifle_sg552" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_rifle_sg552", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
datamap_t *CRifle_SG552::GetDataDescMap( void )
{
	return &m_DataMap;
}
BEGIN_SIMPLE_DATADESC_( CRifle_SG552, CTerrorGun )
END_DATADESC()
#endif
