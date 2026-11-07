//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/weapon_carried_item.h"
#include "activitylist.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/entities/c_point_prop_use_target.h"
#define CColaBottles C_ColaBottles
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/entities/point_prop_use_target.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar cola_bottles_use_duration( "cola_bottles_use_duration", "1.95", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar cola_bottles_use_range( "cola_bottles_use_range", "75", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar cola_bottles_use_tolerance( "cola_bottles_use_tolerance", "0.1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

class CColaBottles : public CCarriedPropWeapon
{
public:
	DECLARE_CLASS( CColaBottles, CCarriedPropWeapon );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_COLA_BOTTLES; }
	virtual bool IsPerformingAction() const OVERRIDE { return false; } // S3CC7E0/C2FACC0.
	virtual void Precache() OVERRIDE;
	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode ) OVERRIDE;
	virtual bool StartAction( int mode, int buttons ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
	virtual float GetActionDuration() const OVERRIDE { return cola_bottles_use_duration.GetFloat(); }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 9; }
#endif
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity GetDrawActivity() OVERRIDE { return ACT_VM_DRAW; }
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual const char *GetCarriedPropModel() const OVERRIDE { return "models/w_models/weapons/w_cola.mdl"; }
};

IMPLEMENT_NETWORKCLASS_ALIASED( ColaBottles, DT_ColaBottles )
BEGIN_NETWORK_TABLE( CColaBottles, DT_ColaBottles )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CColaBottles )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_cola_bottles, CColaBottles );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_cola_bottles );
#endif

void CColaBottles::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Player.UsingColaBottles" );
}

CBaseEntity *CColaBottles::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
#ifdef CLIENT_DLL
	return CPointPropUseTarget::FindUseTarget( pPlayer, cola_bottles_use_range.GetFloat(), cola_bottles_use_tolerance.GetFloat(), CPointPropUseTarget::USE_COLA, this );
#else
	return CPointPropUseTarget::FindUseTarget( pPlayer, cola_bottles_use_range.GetFloat(), cola_bottles_use_tolerance.GetFloat(), CPointPropUseTarget::USE_COLA );
#endif
}

bool CColaBottles::StartAction( int mode, int buttons )
{
	// S:0x3CC450. Unlike gascan, eligibility has NO target-busy/active
	// check. In particular, do not "fix" competition by sharing its gate.
	return mode == 1 ? false : BaseClass::StartAction( mode, buttons );
}

void CColaBottles::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	EmitSound( "Player.UsingColaBottles" );
	pPlayer->SpeakIfAllowed( "PlayerUsingColaBottles", 1, NULL, NULL, 0, NULL );
#endif
	BaseClass::OnActionStarted( mode, pPlayer, pTarget, duration );
}

#ifndef CLIENT_DLL
void CColaBottles::OnActionStopped( CTerrorPlayer *pPlayer )
{
	BaseClass::OnActionStopped( pPlayer );
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_COLA_USE, 0 );
	// S:0x3CC3C0 really stops GAS sound, not the cola sound begun above.
	StopSound( "Player.UsingGasCan" );
}
#endif

Activity CColaBottles::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? CarriedActivity( "ACT_MELEE_SWEEP_COLA" ) : ACT_INVALID;
}

Activity CColaBottles::TranslateViewModelActivity( Activity activity )
{
	// Cola inherits the generic backpack mapping, not gascan's mapping.
	return CBaseBackpackItem::TranslateViewModelActivity( activity );
}

acttable_t *CColaBottles::ActivityList( int &iActivityCount )
{
	// S:0x7B8DC8. Cola's survivor locomotion is the medkit set.
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

// Native server 0x3cc2f0; 0x5c4c38 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_cola_bottlesPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_cola_bottles", 0x5c4c38 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_cola_bottles", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CColaBottles )
END_DATADESC()
#endif
