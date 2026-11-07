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
#define CSniper_Military C_Sniper_Military
#endif

class CSniper_Military : public CBaseSniperRifle
{
public:
	DECLARE_CLASS( CSniper_Military, CBaseSniperRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SNIPER_MILITARY; }
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity TranslatePlayerActivity( Activity activity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};

// RTTI retains CBaseSniperRifle, but the original wire/prediction base is
// CTerrorGun. Do not insert an extra base table merely to mirror C++ ancestry.
IMPLEMENT_NETWORKCLASS_ALIASED( Sniper_Military, DT_Sniper_Military )
BEGIN_NETWORK_TABLE_NOBASE( CSniper_Military, DT_Sniper_Military )
#ifdef CLIENT_DLL
	RecvPropDataTable( "baseclass", 0, 0, CTerrorGun::m_pClassRecvTable, DataTableRecvProxy_StaticDataTable ),
#else
	SendPropDataTable( "baseclass", 0, CTerrorGun::m_pClassSendTable, SendProxy_DataTableToDataTable ),
#endif
END_NETWORK_TABLE()
#if defined( CLIENT_DLL ) && !defined( NO_ENTITY_PREDICTION )
datamap_t CSniper_Military::m_PredMap = { 0, 0, "CSniper_Military", &CTerrorGun::m_PredMap };
datamap_t *CSniper_Military::GetPredDescMap() { return &m_PredMap; }
BEGIN_PREDICTION_DATA_GUTS( CSniper_Military )
END_PREDICTION_DATA()
#else
BEGIN_PREDICTION_DATA( CSniper_Military )
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_sniper_military, CSniper_Military );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_sniper_military );
#endif

acttable_t *CSniper_Military::ActivityList( int &iActivityCount )
{
	// S:0x3E1AA0, table0x7BD250.
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_SNIPER_MILITARY" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_SNIPER_MILITARY" },
		{ "ACT_RUN", "ACT_RUN_SNIPER_MILITARY" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_SNIPER_MILITARY" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_SNIPER_MILITARY" },
		{ "ACT_WALK", "ACT_WALK_SNIPER_MILITARY" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_SNIPER_MILITARY" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_SNIPER_MILITARY" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_SNIPER_MILITARY" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_SNIPER_MILITARY" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_SNIPER_MILITARY" },
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

Activity CSniper_Military::TranslatePlayerActivity( Activity activity )
{
	// S:0x3E1AE0; MILITARYZOOMED names intentionally have no underscore.
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

Activity CSniper_Military::TranslateViewModelActivity( Activity activity )
{
	// S:0x3E1AC0: bypass hunting's family-form translator on fallback.
	static const Activity s_PrimaryLayer = SniperActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	if ( activity == ACT_VM_PRIMARYATTACK )
		return s_PrimaryLayer;
	return CTerrorGun::TranslateViewModelActivity( activity );
}

// Native server 0x3e1a60; 0x5c50b4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_sniper_militaryPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_sniper_military", 0x5c50b4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_sniper_military" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_sniper_military", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
datamap_t *CSniper_Military::GetDataDescMap( void )
{
	return &m_DataMap;
}
BEGIN_SIMPLE_DATADESC_( CSniper_Military, CTerrorGun )
END_DATADESC()
#endif
