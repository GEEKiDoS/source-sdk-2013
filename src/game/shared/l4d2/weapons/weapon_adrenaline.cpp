//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_belt_item.h"
#include "igameevents.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#define CItem_Adrenaline C_Item_Adrenaline
#else
#include "l4d2/player/terrorplayer.h"
#include "EventLog.h"
#include "rumble_shared.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar adrenaline_duration( "adrenaline_duration", "15.0f", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
ConVar adrenaline_health_buffer( "adrenaline_health_buffer", "25", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );

class CItem_Adrenaline : public CBaseBeltItem
{
public:
	DECLARE_CLASS( CItem_Adrenaline, CBaseBeltItem );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_ADRENALINE; }
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE { return activity; }
	virtual Activity TranslateViewModelLayerActivity( Activity activity ) OVERRIDE;

protected:
	virtual double GetConsumeDuration() OVERRIDE { return static_cast< double >( 1.2f ); }
#ifndef CLIENT_DLL
	virtual void OnConsumeStarted( CTerrorPlayer *pPlayer ) OVERRIDE;
	virtual void ApplyConsumable( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( Item_Adrenaline, DT_Item_Adrenaline )
BEGIN_NETWORK_TABLE( CItem_Adrenaline, DT_Item_Adrenaline )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItem_Adrenaline )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_adrenaline, CItem_Adrenaline );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_adrenaline );
#endif

#ifndef CLIENT_DLL
void CItem_Adrenaline::OnConsumeStarted( CTerrorPlayer *pPlayer )
{
	pPlayer->SpeakIfAllowed( "UseAdrenaline", 1, NULL, NULL, 0, NULL );
}

void CItem_Adrenaline::ApplyConsumable( CTerrorPlayer *pPlayer )
{
	// S:0x3D1F10: adrenaline timer/speed change precedes health and rumble.
	pPlayer->UseAdrenaline( adrenaline_duration.GetFloat() );
	char szIdentity[256];
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( pPlayer, szIdentity, sizeof( szIdentity ) );
	char szLog[256];
	Q_snprintf( szLog, sizeof( szLog ), "%s used adrenaline\n", szIdentity );
	UTIL_LogPrintf( "%s", szLog );
	DevMsg( 2, "%s", szLog );
	pPlayer->GiveHealthBuffer( adrenaline_health_buffer.GetFloat(), (float)pPlayer->GetMaxHealth() );
	pPlayer->RumbleEffect( RUMBLE_ADRENALINE, 0, RUMBLE_FLAG_RESTART );

	IGameEvent *pEvent = gameeventmanager->CreateEvent( "adrenaline_used" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		pEvent->SetInt( "subject", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}
#endif

Activity CItem_Adrenaline::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	return BeltActivity( "ACT_TERROR_USE_ADRENALINE" );
}

Activity CItem_Adrenaline::TranslateViewModelLayerActivity( Activity activity )
{
	// S:0x3D1DA0 is v429, NOT v430 (which is identity for adrenaline).
	static const Activity s_Activity[][2] =
	{
		{ BeltActivity( "ACT_VM_DRAW" ), BeltActivity( "ACT_VM_DEPLOY_LAYER" ) },
		{ BeltActivity( "ACT_VM_PRIMARYATTACK" ), BeltActivity( "ACT_VM_PRIMARYATTACK_LAYER" ) },
		{ BeltActivity( "ACT_VM_MELEE" ), BeltActivity( "ACT_VM_MELEE_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND" ), BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_LOOP" ), BeltActivity( "ACT_VM_ITEMPICKUP_LOOP_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT" ), BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_EXTEND" ), BeltActivity( "ACT_VM_HELPINGHAND_EXTEND_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_LOOP" ), BeltActivity( "ACT_VM_HELPINGHAND_LOOP_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_RETRACT" ), BeltActivity( "ACT_VM_HELPINGHAND_RETRACT_LAYER" ) }
	};
	for ( int i = 0; i < ARRAYSIZE( s_Activity ); ++i )
	{
		if ( activity == s_Activity[i][0] )
			return s_Activity[i][1];
	}
	return activity;
}

// Native server 0x3d1cc0; 0x5c4d54 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_adrenalinePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_adrenaline", 0x5c4d54 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_adrenaline", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CItem_Adrenaline )
END_DATADESC()
#endif
