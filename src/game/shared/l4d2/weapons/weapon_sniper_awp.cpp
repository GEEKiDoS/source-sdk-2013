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
#define CSniper_AWP C_Sniper_AWP
#endif

class CSniper_AWP : public CBaseSniperRifle
{
public:
	DECLARE_CLASS( CSniper_AWP, CBaseSniperRifle );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_SNIPER_AWP; }
	virtual int GetZoomFOV() const OVERRIDE { return 40; }
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
};

// RTTI retains CBaseSniperRifle, but the original wire/prediction base is
// CTerrorGun. Do not insert an extra base table merely to mirror C++ ancestry.
IMPLEMENT_NETWORKCLASS_ALIASED( Sniper_AWP, DT_Sniper_AWP )
BEGIN_NETWORK_TABLE_NOBASE( CSniper_AWP, DT_Sniper_AWP )
#ifdef CLIENT_DLL
	RecvPropDataTable( "baseclass", 0, 0, CTerrorGun::m_pClassRecvTable, DataTableRecvProxy_StaticDataTable ),
#else
	SendPropDataTable( "baseclass", 0, CTerrorGun::m_pClassSendTable, SendProxy_DataTableToDataTable ),
#endif
END_NETWORK_TABLE()
#if defined( CLIENT_DLL ) && !defined( NO_ENTITY_PREDICTION )
datamap_t CSniper_AWP::m_PredMap = { 0, 0, "CSniper_AWP", &CTerrorGun::m_PredMap };
datamap_t *CSniper_AWP::GetPredDescMap() { return &m_PredMap; }
BEGIN_PREDICTION_DATA_GUTS( CSniper_AWP )
END_PREDICTION_DATA()
#else
BEGIN_PREDICTION_DATA( CSniper_AWP )
END_PREDICTION_DATA()
#endif

LINK_ENTITY_TO_CLASS( weapon_sniper_awp, CSniper_AWP );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_sniper_awp );
#endif

Activity CSniper_AWP::TranslateViewModelActivity( Activity activity )
{
	// S:0x3E1830 / C:0x307E70. Bolt/model event timing stays native;
	// these guns do not use hunting's multi-stage sniper-form translation.
	static const Activity s_Deploy = SniperActivity( "ACT_VM_DEPLOY" );
	static const Activity s_DeployLayer = SniperActivity( "ACT_VM_DEPLOY_LAYER" );
	static const Activity s_PrimaryLayer = SniperActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	static const Activity s_ReloadLayer = SniperActivity( "ACT_VM_RELOAD_LAYER" );
	if ( activity == s_Deploy )
		return s_DeployLayer;
	if ( activity == ACT_VM_PRIMARYATTACK )
		return s_PrimaryLayer;
	if ( activity == ACT_VM_RELOAD )
		return s_ReloadLayer;
	// The original calls the gun/weapon translator, not the hunting override.
	return CTerrorGun::TranslateViewModelActivity( activity );
}

// Native server 0x3e17f0; 0x5c509c is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_sniper_awpPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_sniper_awp", 0x5c509c ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		if ( !ignoreConditionals && IsWeaponAllowedForPrecache( "weapon_sniper_awp" ) )
			handler->Precache( RESOURCETYPE_ENTITY, "weapon_sniper_awp", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
datamap_t *CSniper_AWP::GetDataDescMap( void )
{
	return &m_DataMap;
}
BEGIN_SIMPLE_DATADESC_( CSniper_AWP, CTerrorGun )
END_DATADESC()
#endif
