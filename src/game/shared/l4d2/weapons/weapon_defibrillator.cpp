//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/effects/survivor_death_model.h"
#include "npcevent.h"
#endif
#include "l4d2/weapons/base_backpack_item.h"
#include "activitylist.h"
#include "datacache/imdlcache.h"
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

extern ConVar player_use_radius;
ConVar defibrillator_use_duration( "defibrillator_use_duration", "3", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

#ifdef CLIENT_DLL
#define CItemDefibrillator C_ItemDefibrillator
#endif

class CItemDefibrillator : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( CItemDefibrillator, CBaseBackpackItem );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	CItemDefibrillator();
	virtual void Precache() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual const char *GetWorldModel() const OVERRIDE;
	virtual const char *GetPlayerModel() const OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE { return activity; }
	virtual acttable_t *ActivityList( int &iActivityCount ) OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_DEFIBRILLATOR; }
	virtual bool IsPerformingAction() const OVERRIDE { return false; } // S3D2C40/C2FDEF0.
	virtual float GetActionDuration() const OVERRIDE { return defibrillator_use_duration.GetFloat(); }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 4; }
#endif
	virtual CBaseEntity *FindActionTarget( CTerrorPlayer *pPlayer, int mode ) OVERRIDE;
	virtual bool CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual bool CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
#ifndef CLIENT_DLL
	virtual void Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator ) OVERRIDE;
#endif

private:
#ifndef CLIENT_DLL
	float m_fNextFailMessageTime;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( ItemDefibrillator, DT_Defibrillator )
BEGIN_NETWORK_TABLE( CItemDefibrillator, DT_Defibrillator )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CItemDefibrillator )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CItemDefibrillator )

	DEFINE_FIELD( m_fNextFailMessageTime, FIELD_TIME ),
END_DATADESC()
#endif
LINK_ENTITY_TO_CLASS( weapon_defibrillator, CItemDefibrillator );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_defibrillator );
#endif

CItemDefibrillator::CItemDefibrillator()
{
#ifndef CLIENT_DLL
	m_fNextFailMessageTime = 0.0f;
#endif
}

void CItemDefibrillator::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "Defibrillator.UseStart" );
	PrecacheScriptSound( "Defibrillator.Use" );
	PrecacheModel( "models/w_models/weapons/w_eq_defibrillator_paddles.mdl" );
	PrecacheModel( "models/w_models/weapons/w_eq_defibrillator_no_paddles.mdl" );
	PrecacheParticleSystem( "item_defibrillator_paddle" );
	PrecacheParticleSystem( "item_defibrillator_body" );
}

bool CItemDefibrillator::Deploy()
{
	// S:0x3D2CF0 / C:0x2FDF60 deliberately bypass TerrorWeapon::Deploy's
	// helping-hand reset and uses the held paddle model in DefaultDeploy.
	MDLCACHE_CRITICAL_SECTION();
	return DefaultDeploy( (char *)GetViewModel(), (char *)GetPlayerModel(), GetDrawActivity(), (char *)GetAnimPrefix() );
}

const char *CItemDefibrillator::GetWorldModel() const
{
	return "models/w_models/weapons/w_eq_defibrillator.mdl";
}

const char *CItemDefibrillator::GetPlayerModel() const
{
	return "models/w_models/weapons/w_eq_defibrillator_paddles.mdl";
}

CBaseEntity *CItemDefibrillator::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
#ifndef CLIENT_DLL
	// S:0x3D2D90: select the body first; only callbacks resolve its current
	// character-to-survivor mapping. Never prefilter an alive/reused owner.
	CSurvivorDeathModel *pBody = CSurvivorDeathModel::FindNearest( pPlayer, player_use_radius.GetFloat() );
	if ( pBody )
		return pBody;
#endif
	// The original client C:0x2FDF20 only predicts this ordinary selector.
	return pPlayer->FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
}

bool CItemDefibrillator::CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
#ifndef CLIENT_DLL
	// S:0x3D2ED0. Failed primary on a player has its OWN strict five-second
	// event throttle. Secondary rejection does not emit this feedback.
	if ( pTarget->IsPlayer() && mode != 1 )
	{
		if ( gpGlobals->curtime > m_fNextFailMessageTime )
		{
			CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
			IGameEvent *event = gameeventmanager->CreateEvent( "defibrillator_used_fail" );
			if ( event )
			{
				event->SetInt( "userid", pPlayer->GetUserID() );
				event->SetInt( "subject", pSubject->GetUserID() );
				gameeventmanager->FireEvent( event );
			}
			m_fNextFailMessageTime = gpGlobals->curtime + 5.0f;
		}
		return false;
	}
	if ( !dynamic_cast< CSurvivorDeathModel * >( pTarget ) )
		return false;
#endif
	return BaseClass::CanPerformAction( mode, pPlayer, pTarget );
}

void CItemDefibrillator::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	pPlayer->EmitSound( "Defibrillator.UseStart" );
	pPlayer->SpeakIfAllowed( "PlayerUsingDefibrillator", 1 );
	CSurvivorDeathModel *pBody = dynamic_cast< CSurvivorDeathModel * >( pTarget );
	CTerrorPlayer *pSubject = pBody ? pBody->GetSurvivor() : NULL;
	if ( !pSubject )
		return;
	IGameEvent *event = gameeventmanager->CreateEvent( "defibrillator_begin" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pSubject->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
#endif
}

bool CItemDefibrillator::CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons )
{
	if ( BaseClass::CanContinueAction( pPlayer, pTarget, buttons ) )
		return true;
#ifndef CLIENT_DLL
	CSurvivorDeathModel *pBody = dynamic_cast< CSurvivorDeathModel * >( pTarget );
	CTerrorPlayer *pSubject = pBody ? pBody->GetSurvivor() : NULL;
	if ( pSubject )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "defibrillator_interrupted" );
		if ( event )
		{
			event->SetInt( "userid", pPlayer->GetUserID() );
			event->SetInt( "subject", pSubject->GetUserID() );
			gameeventmanager->FireEvent( event );
		}
	}
#endif
	return false;
}

#ifndef CLIENT_DLL
void CItemDefibrillator::OnActionStopped( CTerrorPlayer *pPlayer )
{
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_USE, 0 );
	pPlayer->StopSound( "Defibrillator.UseStart" );
}

void CItemDefibrillator::OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3D31B0. L restores immediately after the charge and separately
	// owns action5/self + the return-to-life timer. Do not add its three
	// seconds to this item's charge deadline or delay the Spawn instead.
	CSurvivorDeathModel *pBody = dynamic_cast< CSurvivorDeathModel * >( pTarget );
	CTerrorPlayer *pSubject = pBody ? pBody->GetSurvivor() : NULL;
	if ( !pSubject || !pBody->Defibrillate( pPlayer ) )
		return;
	pPlayer->RumbleEffect( 33, 0, 4 );
	IGameEvent *event = gameeventmanager->CreateEvent( "defibrillator_used" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pSubject->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}
#endif

#ifndef CLIENT_DLL
void CItemDefibrillator::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	if ( pEvent->event != 73 )
	{
		BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
		return;
	}
	// S:0x3D3280: the model's shock event is visual/sound only. It never
	// restores health and never consumes the item a second time.
	CTerrorPlayer *pPlayer = ToTerrorPlayer( pOperator );
	if ( pPlayer )
	{
		pPlayer->EmitSound( "Defibrillator.Use" );
		CBaseEntity *pTarget = pPlayer->m_useActionTarget.Get();
		if ( pTarget )
		{
			variant_t value;
			pTarget->AcceptInput( "Shock", this, this, value, 0 );
		}
	}
}
#endif

static Activity DefibrillatorActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

Activity CItemDefibrillator::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Melee = DefibrillatorActivity( "ACT_MELEE_SWEEP_DEFIBRILLATOR" );
	static const Activity s_Primary = DefibrillatorActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? s_Melee : s_Primary;
}

acttable_t *CItemDefibrillator::ActivityList( int &iActivityCount )
{
	// Original S:0x7BA230: the defibrillator really uses the ELITES rows.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_ELITES" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_ELITES" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_ELITES" },
		{ "ACT_WALK", "ACT_WALK_ELITES" },
		{ "ACT_RUN", "ACT_RUN_ELITES" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_ELITES" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_ELITES" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_ELITES" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_ELITES" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_ELITES" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_ELITES" },
		{ "ACT_JUMP", "ACT_JUMP_ITEM" }
	};
	static acttable_t s_Table[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Table[i].baseAct = DefibrillatorActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = DefibrillatorActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

// Native server 0x3d2b00; 0x5c4da8 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_defibrillatorPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_defibrillator", 0x5c4da8 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_defibrillator", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
