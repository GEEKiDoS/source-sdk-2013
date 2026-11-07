//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "AI_Criteria.h"
#endif
#include "l4d2/weapons/base_backpack_item.h"
#include "activitylist.h"
#include <xmmintrin.h>
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar first_aid_kit_use_duration( "first_aid_kit_use_duration", "5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
#ifndef CLIENT_DLL
ConVar first_aid_kit_max_heal( "first_aid_kit_max_heal", "100", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar first_aid_heal_percent( "first_aid_heal_percent", "0.8", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
,
	"Percent of injuries to heal", true, 0.0f, true, 1.0f );
#endif

#ifdef CLIENT_DLL
#define CFirstAidKit C_FirstAidKit
#endif

class CFirstAidKit : public CBaseBackpackItem
{
public:
	DECLARE_CLASS( CFirstAidKit, CBaseBackpackItem );
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif

	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_FIRST_AID_KIT; }
	virtual float GetActionDuration() const OVERRIDE { return first_aid_kit_use_duration.GetFloat(); }
#ifndef CLIENT_DLL
	virtual int GetActionCode() const OVERRIDE { return 1; }
#endif
	virtual bool CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual bool CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons ) OVERRIDE;
	virtual void OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget ) OVERRIDE;
	virtual void OnActionStopped( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
};

IMPLEMENT_NETWORKCLASS_ALIASED( FirstAidKit, DT_WeaponFirstAidKit )
BEGIN_NETWORK_TABLE( CFirstAidKit, DT_WeaponFirstAidKit )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CFirstAidKit )
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CFirstAidKit )
END_DATADESC()
#endif
LINK_ENTITY_TO_CLASS( weapon_first_aid_kit, CFirstAidKit );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_first_aid_kit );
#endif

bool CFirstAidKit::CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
#ifndef CLIENT_DLL
	// S:0x3CCD30: a 99-health survivor is rejected at the default max100.
	// Temporary health and the player's maximum health are NOT this test.
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	const int threshold = static_cast< int >( static_cast< unsigned int >( first_aid_kit_max_heal.GetInt() ) - 1u );
	if ( !pSubject || pSubject->GetHealth() >= threshold )
		return false;
#endif
	return BaseClass::CanPerformAction( mode, pPlayer, pTarget );
}

void CFirstAidKit::OnActionStarted( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget, float duration )
{
#ifndef CLIENT_DLL
	// S:0x3CCD80. The sound is on the healer, not the held weapon.
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	pPlayer->EmitSound( "Player.BandagingWounds" );
	if ( mode )
	{
		pPlayer->SpeakIfAllowed( "PlayerHealingOther", 1 );
		pSubject->SpeakIfAllowed( "PlayerBeingHealed", 1 );
	}
	else if ( pPlayer->HasLeftCheckpoint() )
	{
		pPlayer->SpeakIfAllowed( "PlayerHealing", 1 );
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "heal_begin" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pSubject->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
#endif
}

bool CFirstAidKit::CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons )
{
	if ( BaseClass::CanContinueAction( pPlayer, pTarget, buttons ) )
		return true;
#ifndef CLIENT_DLL
	// S:0x3CCEB0: interrupted precedes the ordinary stop/end callback.
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	IGameEvent *event = gameeventmanager->CreateEvent( "heal_interrupted" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pSubject->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "Subject", pPlayer->GetResponseCharacterName(), 1.0f );
	QueueResponseConceptForEntity( "HealMeInterrupted", criteria, gpGlobals->curtime, pSubject, pPlayer );
#endif
	return false;
}

#ifndef CLIENT_DLL
void CFirstAidKit::OnActionStopped( CTerrorPlayer *pPlayer )
{
	// S:0x3CCFF0. The original heal_end subject is ALWAYS the healer,
	// including when healing somebody else; do not repair this event bug.
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_USE, 0 );
	pPlayer->StopSound( "Player.BandagingWounds" );
	IGameEvent *event = gameeventmanager->CreateEvent( "heal_end" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}

void CFirstAidKit::OnActionComplete( CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3CD1F0: L owns TakeHealth, revive/third-strike reset and the raw
	// buffer timestamp. The event reports truncated REQUESTED float health.
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	const float restored = pSubject->HealSurvivor( first_aid_kit_max_heal.GetFloat(), first_aid_heal_percent.GetFloat() );
	IGameEvent *event = gameeventmanager->CreateEvent( "heal_success" );
	if ( event )
	{
		event->SetInt( "userid", pPlayer->GetUserID() );
		event->SetInt( "subject", pSubject->GetUserID() );
		event->SetInt( "health_restored", _mm_cvttss_si32( _mm_set_ss( restored ) ) );
		gameeventmanager->FireEvent( event );
	}
	pPlayer->m_checkpointMedkitsUsed = static_cast< int >( static_cast< unsigned int >( pPlayer->m_checkpointMedkitsUsed.Get() ) + 1u );
	pSubject->SpeakIfAllowed( "RelaxedSigh", 1 );
	if ( pSubject != pPlayer )
	{
		AI_CriteriaSet criteria;
		criteria.AppendCriteria( "Subject", pPlayer->GetResponseCharacterName(), 1.0f );
		QueueResponseConceptForEntity( "HealedByFriend", criteria, gpGlobals->curtime + 1.0f, pSubject, pPlayer );
	}
}
#endif

static Activity FirstAidActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

Activity CFirstAidKit::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3CCC60 / C:0x2FB190: all four shove events use the same kit sweep.
	static const Activity s_Melee = FirstAidActivity( "ACT_MELEE_SWEEP_FIRSTAIDKIT" );
	static const Activity s_Primary = FirstAidActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP ? s_Melee : s_Primary;
}

// Native server 0x3ccbe0; 0x5c4c74 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_first_aid_kitPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_first_aid_kit", 0x5c4c74 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_first_aid_kit", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
