//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/weapons/base_belt_item.h"
#include "activitylist.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "tier0/memdbgon.h"

ConVar z_use_belt_item_tolerance( "z_use_belt_item_tolerance", "0.98", 
#if defined( CLIENT_DLL )
	0x0000600A
#elif defined( GAME_DLL )
	0x00006006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseBeltItem, DT_BaseBeltItem, CBaseBeltItem )
#else
IMPLEMENT_SERVERCLASS( CBaseBeltItem, DT_BaseBeltItem )
#endif
BEGIN_NETWORK_TABLE( CBaseBeltItem, DT_BaseBeltItem )
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CBaseBeltItem )
END_PREDICTION_DATA()

#ifndef CLIENT_DLL
// S:0x7B7F54: the own server datamap contains only its sentinel.
BEGIN_DATADESC( CBaseBeltItem )
END_DATADESC()
#endif

CBaseBeltItem::CBaseBeltItem()
{
	m_UseTimer.Invalidate();
}

void CBaseBeltItem::Spawn()
{
	// S:0x3C2490 / C:0x2F1330: invalidate before the common spawn.
	m_UseTimer.Invalidate();
	BaseClass::Spawn();
}

bool CBaseBeltItem::Deploy()
{
	m_UseTimer.Invalidate();
	return BaseClass::Deploy();
}

bool CBaseBeltItem::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	m_UseTimer.Invalidate();
	return BaseClass::Holster( pSwitchingTo );
}

bool CBaseBeltItem::SendWeaponAnim( int activity )
{
	// S:0x3C22C0 / C:0x2F1190: family mapping precedes common dispatch.
	return BaseClass::SendWeaponAnim( TranslateViewModelActivity( (Activity)activity ) );
}

void CBaseBeltItem::PrimaryAttack()
{
	// S:0x3C2560 / C:0x2F1400. Release is not a cancellation condition.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || m_UseTimer.m_timestamp > 0.0f || HandlePrimaryAttackUse() || !CanConsume( pPlayer ) )
		return;

	SendWeaponAnim( ACT_VM_PRIMARYATTACK );
	pPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_FIRE_PRIMARY, 0 );
#ifndef CLIENT_DLL
	OnConsumeStarted( pPlayer );
#endif
	m_UseTimer.Start( static_cast< float >( GetConsumeDuration() ) );
}

void CBaseBeltItem::SecondaryAttack()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || m_UseTimer.m_timestamp > 0.0f )
		return;

#ifndef CLIENT_DLL
	// The original client deliberately omits this server combat query.
	if ( pPlayer->IsInCombat( false ) )
	{
		BaseClass::SecondaryAttack();
		return;
	}
#endif
	CTerrorPlayer *pTarget = ToTerrorPlayer( pPlayer->FindUseEntity( 256.0f, 0.0f, z_use_belt_item_tolerance.GetFloat(), NULL, true ) );
	if ( pTarget && ( pTarget->GetTeamNumber() == TEAM_SURVIVOR || pTarget->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && !pTarget->IsIncapacitated() )
	{
#ifndef CLIENT_DLL
		pPlayer->GiveBeltItem( pTarget );
#endif
		// A valid candidate suppresses shove even if the server cannot give.
		return;
	}
	BaseClass::SecondaryAttack();
}

void CBaseBeltItem::ItemPostFrame()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
#ifndef CLIENT_DLL
	// S:0x3C2640 snapshots buttons before the common input queue mutates them.
	const int nButtons = pPlayer->m_nButtons;
#endif
	BaseClass::ItemPostFrame();
#ifndef CLIENT_DLL
	if ( m_UseTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_UseTimer.m_timestamp )
	{
		pPlayer = GetTerrorPlayerOwner();
		if ( pPlayer && ( pPlayer->GetTeamNumber() == TEAM_SURVIVOR || pPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && !pPlayer->IsIncapacitated() && pPlayer->IsAlive() )
		{
			ApplyConsumable( pPlayer );
			pPlayer->m_nButtons = nButtons;
			// The native inventory drop owns ammunition/detachment/next selection.
			pPlayer->Weapon_Drop( this, NULL, NULL );
			UTIL_Remove( this );
			return;
		}
		m_UseTimer.Invalidate();
	}
#endif
}

double CBaseBeltItem::GetConsumeDuration()
{
	return SequenceDuration(); // Whole S3C2600/C2F1200, NO F32 return spill.
}

#pragma float_control( precise, on, push )
bool CBaseBeltItem::CanDisplayHelpingHand() const
{
	// S3C2380/C2F11D0: JA rejects only ordered-positive timestamps.
	return !( m_UseTimer.m_timestamp > 0.0f ) && BaseClass::CanDisplayHelpingHand();
}
#pragma float_control( pop )

Activity CBaseBeltItem::BeltActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

acttable_t *CBaseBeltItem::ActivityList( int &iActivityCount )
{
	// The concrete pills/adrenaline tables S:0x7BB778/0x7B9D50 are identical.
	static const char *s_pActivityNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_GREN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_GREN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_GREN" },
		{ "ACT_WALK", "ACT_WALK_GREN" },
		{ "ACT_RUN", "ACT_RUN_GREN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_GREN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_GREN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_GREN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_GREN" },
		{ "ACT_WALK_CALM", "ACT_WALK_GREN" },
		{ "ACT_RUN_CALM", "ACT_RUN_GREN" },
		{ "ACT_JUMP", "ACT_JUMP_ITEM" }
	};
	static acttable_t s_ActivityTable[ARRAYSIZE( s_pActivityNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pActivityNames ); ++i )
		{
			s_ActivityTable[i].baseAct = BeltActivity( s_pActivityNames[i][0] );
			s_ActivityTable[i].weaponAct = BeltActivity( s_pActivityNames[i][1] );
			s_ActivityTable[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_ActivityTable );
	return s_ActivityTable;
}

Activity CBaseBeltItem::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	// Both leaves retain the common rifle shove selector for events34..37.
	static const Activity s_ShoveActivity[][2] =
	{
		{ BeltActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), BeltActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ BeltActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), BeltActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ BeltActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), BeltActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ BeltActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), BeltActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return s_ShoveActivity[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
	return BeltActivity( "ACT_PRIMARYATTACK_RIFLE" );
}

Activity CBaseBeltItem::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return BeltActivity( "ACT_RELOAD_RIFLE" );
}

Activity CBaseBeltItem::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return BeltActivity( "ACT_DEPLOY_GREN" );
}
