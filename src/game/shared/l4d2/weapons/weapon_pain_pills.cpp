//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_belt_item.h"
#include "igameevents.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#define CPainPills C_PainPills
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "EventLog.h"
#endif
#ifdef L4D2
#include "precache_register.h"
#endif
#include "tier0/memdbgon.h"

ConVar pain_pills_health_threshold( "pain_pills_health_threshold", "99", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Pills can't be used unless total health is less than this amount" );
ConVar pain_pills_health_value( "pain_pills_health_value", "50", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );

class CPainPills : public CBaseBeltItem
{
public:
	DECLARE_CLASS( CPainPills, CBaseBeltItem );
#ifndef CLIENT_DLL
	DECLARE_DATADESC();
#endif
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();
	virtual int GetWeaponID() const OVERRIDE { return TERROR_WEAPON_PAIN_PILLS; }
	virtual void Precache() OVERRIDE;
	virtual bool Deploy() OVERRIDE;
	virtual Activity GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity ) OVERRIDE;
	virtual Activity TranslateViewModelActivity( Activity activity ) OVERRIDE;

protected:
	virtual bool CanConsume( CTerrorPlayer *pPlayer ) OVERRIDE;
#ifndef CLIENT_DLL
	virtual void OnConsumeStarted( CTerrorPlayer *pPlayer ) OVERRIDE;
	virtual void ApplyConsumable( CTerrorPlayer *pPlayer ) OVERRIDE;
#endif
};

IMPLEMENT_NETWORKCLASS_ALIASED( PainPills, DT_WeaponPainPills )
BEGIN_NETWORK_TABLE( CPainPills, DT_WeaponPainPills )
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CPainPills )
END_PREDICTION_DATA()
LINK_ENTITY_TO_CLASS( weapon_pain_pills, CPainPills );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_pain_pills );
#endif

void CPainPills::Precache()
{
	BaseClass::Precache();
	PrecacheScriptSound( "PainPills.Deploy" );
}

bool CPainPills::Deploy()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer )
		pPlayer->EmitSound( "PainPills.Deploy" );
	return BaseClass::Deploy();
}

#pragma float_control( precise, on, push )
bool CPainPills::CanConsume( CTerrorPlayer *pPlayer )
{
	// S3DD560/C304820: sample threshold and permanent HP before the
	// effective-buffer getter, then compare the unspilled x87 sum.
	const float threshold = pain_pills_health_threshold.GetFloat();
	const int health = pPlayer->GetHealth();
	const double total = static_cast< double >( pPlayer->GetHealthBuffer() ) + health;
	// FCOMIP/JBE accepts equality AND unordered; do not replace with <=.
	if ( !( total > static_cast< double >( threshold ) ) )
		return true;
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "pills_used_fail" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
	return false;
}
#pragma float_control( pop )

#ifndef CLIENT_DLL
void CPainPills::OnConsumeStarted( CTerrorPlayer *pPlayer )
{
	pPlayer->SpeakIfAllowed( "EatPills", 1, NULL, NULL, 0, NULL );
}

void CPainPills::ApplyConsumable( CTerrorPlayer *pPlayer )
{
	// S:0x3DD700. GetHealthBuffer also owns the original decay event boundary.
	const int nStartingHealth = pPlayer->GetHealth();
	const float flStartingBuffer = pPlayer->GetHealthBuffer();
	pPlayer->GiveHealthBuffer( pain_pills_health_value.GetFloat(), (float)pPlayer->GetMaxHealth() );
	if ( TerrorGameRules()->IsTempHealthOnly() )
		pPlayer->SetReviveCount( 0 );
	pPlayer->m_checkpointPillsUsed = static_cast< int >( static_cast< unsigned int >( pPlayer->m_checkpointPillsUsed.Get() ) + 1u );

	char szIdentity[256];
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( pPlayer, szIdentity, sizeof( szIdentity ) );
	const int nEndingHealth = pPlayer->GetHealth();
	const float flEndingBuffer = pPlayer->GetHealthBuffer();
	char szLog[256];
	Q_snprintf( szLog, sizeof( szLog ), "%s used pills on %s starting with %d health and %.1f pill health, and ended with %d health and %.1f pill health\n",
		szIdentity, szIdentity, nStartingHealth, flStartingBuffer, nEndingHealth, flEndingBuffer );
	UTIL_LogPrintf( "%s", szLog );
	DevMsg( 2, "%s", szLog );

	IGameEvent *pEvent = gameeventmanager->CreateEvent( "pills_used" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", pPlayer->GetUserID() );
		pEvent->SetInt( "subject", pPlayer->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}
#endif

Activity CPainPills::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	return BeltActivity( "ACT_TERROR_USE_PILLS" );
}

Activity CPainPills::TranslateViewModelActivity( Activity activity )
{
	// S:0x3DD2B0. The same mapping is applied at both native consumers.
	static const Activity s_Activity[][2] =
	{
		{ BeltActivity( "ACT_VM_DRAW" ), BeltActivity( "ACT_VM_DEPLOY_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_IDLE" ), BeltActivity( "ACT_VM_IDLE_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_PRIMARYATTACK" ), BeltActivity( "ACT_VM_USE_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_MELEE" ), BeltActivity( "ACT_VM_MELEE_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_DEPLOY_PAINPILLS" ), BeltActivity( "ACT_VM_DEPLOY_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_MELEE_PAINPILLS" ), BeltActivity( "ACT_VM_MELEE_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_USE_PAINPILLS" ), BeltActivity( "ACT_VM_USE_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND" ), BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_LOOP" ), BeltActivity( "ACT_VM_ITEMPICKUP_LOOP_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT" ), BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_EXTEND" ), BeltActivity( "ACT_VM_HELPINGHAND_EXTEND_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_LOOP" ), BeltActivity( "ACT_VM_HELPINGHAND_LOOP_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_RETRACT" ), BeltActivity( "ACT_VM_HELPINGHAND_RETRACT_PAINPILLS" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND_PAINPILLS" ), BeltActivity( "ACT_VM_ITEMPICKUP_EXTEND_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_LOOP_PAINPILLS" ), BeltActivity( "ACT_VM_ITEMPICKUP_LOOP_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT_PAINPILLS" ), BeltActivity( "ACT_VM_ITEMPICKUP_RETRACT_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_EXTEND_PAINPILLS" ), BeltActivity( "ACT_VM_HELPINGHAND_EXTEND_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_LOOP_PAINPILLS" ), BeltActivity( "ACT_VM_HELPINGHAND_LOOP_PAINPILLS_LAYER" ) },
		{ BeltActivity( "ACT_VM_HELPINGHAND_RETRACT_PAINPILLS" ), BeltActivity( "ACT_VM_HELPINGHAND_RETRACT_PAINPILLS_LAYER" ) }
	};
	for ( int i = 0; i < ARRAYSIZE( s_Activity ); ++i )
	{
		if ( activity == s_Activity[i][0] )
			return s_Activity[i][1];
	}
	return BaseClass::TranslateViewModelActivity( activity );
}

// Native server 0x3dd1c0; 0x5c4f00 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_pain_pillsPrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_pain_pills", 0x5c4f00 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_pain_pills", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif

#ifndef CLIENT_DLL
BEGIN_DATADESC( CPainPills )
END_DATADESC()
#endif
