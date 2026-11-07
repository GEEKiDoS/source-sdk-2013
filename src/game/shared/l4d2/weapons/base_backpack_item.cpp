//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/base_backpack_item.h"
#include "activitylist.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

extern ConVar player_use_radius;
extern ConVar z_use_belt_item_tolerance;
ConVar adrenaline_backpack_speedup( "adrenaline_backpack_speedup", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
,
	"Percent of normal backpack item use duration to use when affected by Adrenaline", true, 0.01f, true, 1.0f );

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseBackpackItem, DT_BaseBackpackItem, CBaseBackpackItem )
#else
IMPLEMENT_SERVERCLASS( CBaseBackpackItem, DT_BaseBackpackItem )
#endif
BEGIN_NETWORK_TABLE( CBaseBackpackItem, DT_BaseBackpackItem )
#ifdef CLIENT_DLL
	RecvPropBool( RECVINFO( m_bPerformingAction ) ),
#else
	SendPropBool( SENDINFO( m_bPerformingAction ) ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CBaseBackpackItem )
	DEFINE_PRED_FIELD( m_bPerformingAction, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

#ifndef CLIENT_DLL
// S:0x7B7E04: sentinel-only. Action timer/mode are not SAVE fields.
BEGIN_DATADESC( CBaseBackpackItem )
END_DATADESC()
#endif

static Activity BackpackActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

CBaseBackpackItem::CBaseBackpackItem()
{
	m_bPerformingAction = false;
	m_actionTimer.Invalidate();
	m_actionMode = 0;
}

void CBaseBackpackItem::Spawn()
{
	Precache();
	BaseClass::Spawn();
	m_bPerformingAction = false;
	m_actionTimer.Invalidate();
}

void CBaseBackpackItem::WeaponIdle()
{
	// Original S:0x3C1890 / C:0x2F0AF0: no automatic idle animation.
}

bool CBaseBackpackItem::SendWeaponAnim( int activity )
{
	// S:0x3C1A80 / C:0x2F0B20. The native common viewmodel consumer
	// performs its separate mapping after this family-side translation.
	return BaseClass::SendWeaponAnim( TranslateViewModelActivity( (Activity)activity ) );
}

acttable_t *CBaseBackpackItem::ActivityList( int &iActivityCount )
{
	// S:0x7B7E20, identical to the kit's original S:0x7B9208.
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
			s_Table[i].baseAct = BackpackActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = BackpackActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

CBaseEntity *CBaseBackpackItem::FindActionTarget( CTerrorPlayer *pPlayer, int mode )
{
	// S:0x3C1940 / C:0x2F0D00. Mode zero is self, not a use trace.
	return mode ? pPlayer->FindUseEntity( player_use_radius.GetFloat(), 0.0f,
		z_use_belt_item_tolerance.GetFloat(), NULL, true ) : pPlayer;
}

bool CBaseBackpackItem::CanPerformAction( int mode, CTerrorPlayer *pPlayer, CBaseEntity *pTarget )
{
	// S:0x3C18A0 / C:0x2F0C50. No health/fullness test belongs here.
	if ( pPlayer->m_iCurrentUseAction != 0 )
		return false;
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	if ( pSubject && ( ( pSubject->GetTeamNumber() != TEAM_SURVIVOR && pSubject->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		pSubject->IsIncapacitated() || pSubject->IsDominated() || pSubject->m_iCurrentUseAction != 0 ) )
		return false;
	return !pPlayer->IsAnimationBusy();
}

bool CBaseBackpackItem::StartAction( int mode, int buttons )
{
	// S:0x3C1F00 / C:0x2F0F90. The action speedup is sampled ONCE.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return false;
	CBaseEntity *pTarget = FindActionTarget( pPlayer, mode );
	if ( !pTarget || !CanPerformAction( mode, pPlayer, pTarget ) )
		return false;
	m_bPerformingAction = true;
	float duration = GetActionDuration();
	if ( pPlayer->m_bAdrenalineActive )
		duration *= adrenaline_backpack_speedup.GetFloat();
#ifndef CLIENT_DLL
	m_actionTimer.Start( duration );
	pPlayer->SetUseAction( GetActionCode(), pTarget, duration, true );
#endif
	m_actionMode = mode;
	OnActionStarted( mode, pPlayer, pTarget, duration );
	return true;
}

int CBaseBackpackItem::GetActionButtons( int mode ) const
{
	return mode ? IN_ATTACK2 : IN_ATTACK;
}

bool CBaseBackpackItem::CanContinueAction( CTerrorPlayer *pPlayer, CBaseEntity *pTarget, int buttons )
{
	// S:0x3C1990. There is deliberately no new LOS/range/damage check.
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	if ( pSubject && ( !pSubject->IsAlive() || pSubject->IsIncapacitated() ||
		( pSubject->GetTeamNumber() != TEAM_SURVIVOR && pSubject->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ) )
		return false;
	if ( !( GetActionButtons( m_actionMode ) & buttons ) )
		return false;
#ifdef CLIENT_DLL
	const Vector &velocity = pPlayer->GetAbsVelocity();
#else
	Vector velocity;
	pPlayer->GetVelocity( &velocity, NULL );
#endif
	return velocity == vec3_origin || pPlayer->IsIncapacitated();
}

void CBaseBackpackItem::UpdateAction( int buttons )
{
#ifndef CLIENT_DLL
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	CBaseEntity *pTarget = pPlayer ? pPlayer->m_useActionTarget.Get() : NULL;
	if ( !pPlayer || !pTarget || !CanContinueAction( pPlayer, pTarget, buttons ) )
		StopAction();
#endif
}

void CBaseBackpackItem::ItemPostFrame()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
	const int buttons = pPlayer->m_nButtons;
	BaseClass::ItemPostFrame();
#ifndef CLIENT_DLL
	// S:0x3C21C0 saves the original buttons before common input filtering.
	if ( m_bPerformingAction )
	{
		UpdateAction( buttons );
		if ( gpGlobals->curtime >= m_actionTimer.m_timestamp )
		{
			pPlayer->m_nButtons = buttons;
			CompleteAction();
		}
	}
#endif
}

void CBaseBackpackItem::PrimaryAttack()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer && !HandlePrimaryAttackUse() )
	{
		pPlayer->m_iShotsFired = 1;
		if ( !m_bPerformingAction )
			StartAction( 0, 0 );
	}
}

void CBaseBackpackItem::SecondaryAttack()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || m_bPerformingAction )
		return;
#ifndef CLIENT_DLL
	if ( pPlayer->IsInCombat( false ) )
	{
		BaseClass::SecondaryAttack();
		return;
	}
#endif
	if ( StartAction( 1, 1 ) )
		m_flNextSecondaryAttack = 0.0f;
	else
		BaseClass::SecondaryAttack();
}

bool CBaseBackpackItem::UseAsPrimary()
{
	// S:0x3C1B80 / C:0x2F0BF0: distinct entry (argument2) used by +use.
	return GetTerrorPlayerOwner() && !m_bPerformingAction && StartAction( 0, 2 );
}

void CBaseBackpackItem::CompleteAction()
{
#ifndef CLIENT_DLL
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	CBaseEntity *pTarget = pPlayer ? pPlayer->m_useActionTarget.Get() : NULL;
	if ( !pTarget )
		return;
	CTerrorPlayer *pSubject = ToTerrorPlayer( pTarget );
	if ( pSubject && ( !pSubject->IsAlive() || pSubject->IsIncapacitated() ||
		( pSubject->GetTeamNumber() != TEAM_SURVIVOR && pSubject->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ) )
		return;
	// S:0x3C1C40. The native drop/holster produces end BEFORE success.
	m_actionTimer.Invalidate();
	pPlayer->Weapon_Drop( this, NULL, NULL );
	UTIL_Remove( this );
	OnActionComplete( pPlayer, pTarget );
#endif
}

void CBaseBackpackItem::StopAction()
{
#ifndef CLIENT_DLL
	if ( !m_bPerformingAction )
		return;
	m_bPerformingAction = false;
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer )
	{
		pPlayer->SetProgressBarTime( 0.0f );
		pPlayer->StopUseAction();
		OnActionStopped( pPlayer );
		m_actionTimer.Invalidate();
	}
#endif
}

void CBaseBackpackItem::ExtendAction()
{
#ifndef CLIENT_DLL
	// S:0x3C1BF0: restarting with the original duration preserves the
	// separate action-duration snapshot instead of resampling adrenaline.
	if ( m_bPerformingAction )
		m_actionTimer.Start( m_actionTimer.m_duration );
#endif
}

bool CBaseBackpackItem::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#ifndef CLIENT_DLL
	StopAction();
#endif
	return BaseClass::Holster( pSwitchingTo );
}

#ifndef CLIENT_DLL
void CBaseBackpackItem::Drop( const Vector &vecVelocity )
{
	// S:0x3C2150: consume/drop stops use before the native weapon detach.
	StopAction();
	BaseClass::Drop( vecVelocity );
}
#endif

Activity CBaseBackpackItem::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	static const Activity s_Melee[][2] =
	{
		{ BackpackActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), BackpackActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ BackpackActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), BackpackActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ BackpackActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), BackpackActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ BackpackActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), BackpackActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return s_Melee[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
	static const Activity s_Primary = BackpackActivity( "ACT_PRIMARYATTACK_RIFLE" );
	return s_Primary;
}

Activity CBaseBackpackItem::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Reload = BackpackActivity( "ACT_RELOAD_RIFLE" );
	return s_Reload;
}

Activity CBaseBackpackItem::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	static const Activity s_Deploy = BackpackActivity( "ACT_DEPLOY_GREN" );
	return s_Deploy;
}
