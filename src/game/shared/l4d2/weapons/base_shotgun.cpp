//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "animationlayer.h"
#include "prediction.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/base_shotgun.h"
#include "baseviewmodel_shared.h"
#include "activitylist.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
BEGIN_RECV_TABLE_NOBASE( CBaseShotgun, DT_LocalActiveShotgunData )
	RecvPropInt( RECVINFO( m_reloadState ) ),
	RecvPropInt( RECVINFO( m_reloadAnimState ) ),
	RecvPropInt( RECVINFO( m_reloadNumShells ) ),
	RecvPropFloat( RECVINFO( m_reloadStartTime ) ),
	RecvPropFloat( RECVINFO( m_reloadStartDuration ) ),
	RecvPropFloat( RECVINFO( m_reloadInsertDuration ) ),
	RecvPropFloat( RECVINFO( m_reloadEndDuration ) ),
	RecvPropInt( RECVINFO( m_shellsInserted ) ),
END_RECV_TABLE()
#else
extern void *SendProxy_SendLocalWeaponDataTable( const SendProp *, const void *, const void *, CSendProxyRecipients *, int );
// S:0x3C2AB0. The original owner proxy does not require an active weapon.
BEGIN_SEND_TABLE_NOBASE( CBaseShotgun, DT_LocalActiveShotgunData )
	SendPropInt( SENDINFO( m_reloadState ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_reloadAnimState ), 3, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_reloadNumShells ), 4, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_reloadStartTime ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadStartDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadInsertDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadEndDuration ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_shellsInserted ), 4, SPROP_UNSIGNED ),
END_SEND_TABLE()
#endif

#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_BaseShotgun, DT_BaseShotgun, CBaseShotgun )
#else
IMPLEMENT_SERVERCLASS( CBaseShotgun, DT_BaseShotgun )
#endif
BEGIN_NETWORK_TABLE( CBaseShotgun, DT_BaseShotgun )
#ifdef CLIENT_DLL
	RecvPropDataTable( "LocalShotgunData", 0, 0, &REFERENCE_RECV_TABLE( DT_LocalActiveShotgunData ) ),
#else
	SendPropDataTable( "LocalShotgunData", 0, &REFERENCE_SEND_TABLE( DT_LocalActiveShotgunData ), SendProxy_SendLocalWeaponDataTable ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
// C:0x6EED34: all eight fields, and only the four times have a tolerance.
BEGIN_PREDICTION_DATA( CBaseShotgun )
	DEFINE_PRED_FIELD( m_reloadState, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_reloadAnimState, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_reloadNumShells, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_reloadStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.001f ),
	DEFINE_PRED_FIELD_TOL( m_reloadStartDuration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.001f ),
	DEFINE_PRED_FIELD_TOL( m_reloadInsertDuration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.001f ),
	DEFINE_PRED_FIELD_TOL( m_reloadEndDuration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.001f ),
	DEFINE_PRED_FIELD( m_shellsInserted, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

static Activity ShotgunActivity( const char *pName )
{
	Activity activity = (Activity)ActivityList_IndexForName( pName );
	return activity == ACT_INVALID ? ActivityList_RegisterPrivateActivity( pName ) : activity;
}

CBaseShotgun::CBaseShotgun()
{
	// S:0x3C2E10 / C:0x2F1C20. OnlyPump remains the native base field;
	// neither original shotgun family has a writer that turns it on.
	m_reloadState = 0;
	m_reloadAnimState = 0;
	m_reloadNumShells = 0;
	m_shellsInserted = 0;
	m_reloadStartTime = 0.0f;
	m_reloadStartDuration = 0.0f;
	m_reloadInsertDuration = 0.0f;
	m_reloadEndDuration = 0.0f;
}

void CBaseShotgun::ItemBusyFrame()
{
	// S:0x3C29D0 / C:0x2F19D0, slot273 is BUSY, not PostFrame.
	// Shells advance even while the player's full-reload attack gate is shut.
	BaseClass::ItemBusyFrame();
	CheckReload();
}

void CBaseShotgun::WeaponIdle()
{
	// Original null slot275 S:0x3C29C0 / C:0x2F19C0.
}

void CBaseShotgun::FinishReload()
{
	// Original null slot279 S:0x3C29F0 / C:0x2F19F0. In particular, do not
	// enter CTerrorGun's magazine transfer, including when OnlyPump is set.
}

double CBaseShotgun::GetReloadAnimationScale()
{
	// S:0x3C29A0 / C:0x2F19A0: owned shotgun reloads ignore the generic
	// reload-duration multiplier. The unowned path retains its base behavior.
	return GetTerrorPlayerOwner() ? 1.0f : BaseClass::GetReloadAnimationScale();
}

void CBaseShotgun::SetReloadAttackDelay( float flDuration )
{
	// S:0x3E7FF0 / C:0x30C0B0. These are command-time deadlines, not
	// shell-count-derived integer ticks and not the helping-hand timer clock.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( pPlayer )
		pPlayer->SetNextAttack( gpGlobals->curtime + flDuration );
	m_flNextPrimaryAttack = gpGlobals->curtime + flDuration;
	m_flNextSecondaryAttack = gpGlobals->curtime + flDuration;
	SetWeaponIdleTime( gpGlobals->curtime + flDuration );
}


void CBaseShotgun::PlayReloadStage( Activity activity, TerrorPlayerAnimEvent event, int nShells )
{
	// S:0x3C2D20 / C:0x2F1B30. Only player animation is first-predicted;
	// the viewmodel sequence and playback rate are restored on every replay.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
#ifdef CLIENT_DLL
	if ( !prediction->InPrediction() || prediction->IsFirstTimePredicted() )
#endif
		pPlayer->DoTerrorAnimationEvent( event, nShells );
	EmitTerrorWeaponReloadSound( pPlayer, this, event == TERROR_ANIMEVENT_RELOAD_END ? RELOAD_PUMP : RELOAD );
	SendWeaponAnim( activity );
	SetPlaybackRate( 1.0f / GetReloadAnimationScale() );
	CBaseViewModel *pViewModel = pPlayer->GetViewModel( m_nViewModelIndex, false );
	if ( pViewModel && pViewModel->GetNumAnimOverlays() > 0 )
		pViewModel->GetAnimOverlay( 0 )->m_flPlaybackRate = GetPlaybackRate();
}

bool CBaseShotgun::Reload()
{
	// S:0x3C2F80 / C:0x2F1CF0. Empty and partial reloads enter the same
	// native sequence/state machine; its first animation contains a shell.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || ( pPlayer->m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) ) )
		return false;
	if ( pPlayer->m_reviveTarget.Get() != NULL ||
		( pPlayer->m_iCurrentUseAction != 0 && pPlayer->m_useActionOwner.Get() == pPlayer ) ||
		( m_flNextPrimaryAttack > gpGlobals->curtime && !m_inInitialPickup ) )
	{
		m_reloadQueuedStartTime = gpGlobals->curtime + 1.0f;
		return false;
	}
	if ( m_reloadState != 0 )
		return false;

	m_reloadQueuedStartTime = 0.0f;
	m_reloadStartActivityTime = gpGlobals->curtime;
	pPlayer->m_iShotsFired = 0;
	const int nShells = MIN( GetMaxClip1() - m_iClip1, pPlayer->GetAmmoCount( GetPrimaryAmmoType() ) );
	if ( nShells <= 0 )
		return false;
#ifndef CLIENT_DLL
	pPlayer->OnTerrorReloadStarted( true, m_iClip1, m_iClip1 != 0 );
#endif
	m_reloadState = 1;
	m_reloadNumShells = nShells;
	m_reloadStartTime = gpGlobals->curtime;
	m_bInReload = true;
	m_reloadStartDuration = GetReloadAnimationScale() * SequenceDuration( SelectWeightedSequence( TranslateViewModelActivity( ACT_VM_RELOAD ) ) );
	m_reloadInsertDuration = GetReloadAnimationScale() * SequenceDuration( SelectWeightedSequence( ShotgunActivity( "ACT_VM_RELOAD_LOOP_LAYER" ) ) );
	m_reloadAnimState = 1;
	PlayReloadStage( ACT_VM_RELOAD, TERROR_ANIMEVENT_RELOAD_START, nShells );
	m_shellsInserted = 1;
	m_reloadEndDuration = GetReloadAnimationScale() * SequenceDuration( SelectWeightedSequence( TranslateViewModelActivity( ShotgunActivity( "ACT_VM_RELOAD_END" ) ) ) );
	SetReloadAttackDelay( (float)( m_reloadNumShells - 1 ) * m_reloadInsertDuration + m_reloadStartDuration + m_reloadEndDuration );
	m_flNextSecondaryAttack = gpGlobals->curtime;
	SuppressHelpingHand( (float)( m_reloadNumShells - 1 ) * m_reloadInsertDuration + m_reloadEndDuration );
	return true;
}

void CBaseShotgun::CheckReload()
{
	// S:0x3C38E0 / C:0x2F2200. This is the sole shotgun shell transfer.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
	{
		m_bInReload = false;
		return;
	}
	if ( ( pPlayer->m_reviveTarget.Get() != NULL ||
		( pPlayer->m_iCurrentUseAction != 0 && pPlayer->m_useActionOwner.Get() == pPlayer ) ) && m_bInReload )
	{
		AbortReload();
		return;
	}
	if ( m_reloadState != 1 )
	{
		m_bInReload = false;
		return;
	}
	if ( m_reloadAnimState == 3 )
	{
		if ( gpGlobals->curtime > ( m_reloadStartDuration + m_reloadStartTime ) + (float)( m_reloadNumShells - 1 ) * m_reloadInsertDuration + m_reloadEndDuration )
		{
			m_reloadState = 0;
			m_bInReload = false;
#ifndef CLIENT_DLL
			pPlayer->OnTerrorReloadFinished();
#endif
		}
		return;
	}
	if ( m_reloadAnimState != 1 && m_reloadAnimState != 2 )
		return;

	m_bInReload = true;
	const int nInsert = m_shellsInserted > 0 ? m_shellsInserted - 1 : 0;
	if ( gpGlobals->curtime > ( m_reloadStartDuration + m_reloadStartTime ) + (float)nInsert * m_reloadInsertDuration )
	{
		if ( m_shellsInserted >= m_reloadNumShells )
		{
			m_reloadAnimState = 3;
			PlayReloadStage( ShotgunActivity( "ACT_VM_RELOAD_END" ), TERROR_ANIMEVENT_RELOAD_END );
		}
		else
		{
			++m_shellsInserted;
			m_reloadAnimState = 2;
			PlayReloadStage( ShotgunActivity( "ACT_VM_RELOAD_LOOP" ), TERROR_ANIMEVENT_RELOAD_LOOP );
		}
		// Deliberately ONE shell per invocation, after the animation, even
		// on a late frame. No reserve recheck, clamping or catch-up loop.
		++m_iClip1;
		pPlayer->RemoveAmmo( 1, GetPrimaryAmmoType() );
	}
}

void CBaseShotgun::AbortReload()
{
	// S:0x3C3660 / C:0x2F20D0. A held primary cannot abort an empty gun.
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
	{
		m_reloadState = 0;
		m_bInReload = false;
		return;
	}
	if ( ( pPlayer->m_nButtons & IN_ATTACK ) && m_iClip1 == 0 )
		return;
#ifdef CLIENT_DLL
	// The original client alone clears layers zero AND one on cancellation.
	CBaseViewModel *pViewModel = pPlayer->GetViewModel( m_nViewModelIndex, false );
	if ( pViewModel )
	{
		for ( int i = 0; i < 2 && i < pViewModel->GetNumAnimOverlays(); ++i )
		{
			pViewModel->GetAnimOverlay( i )->Reset();
			pViewModel->GetAnimOverlay( i )->SetOrder( 15 );
		}
	}
#endif
	SetReloadAttackDelay( 0.1f );
	m_reloadState = 0;
	// S:0x3E6F50 / C:0x30B360. The base also stops the client reload
	// sound before clearing reload, sending VM_IDLE and the player END event.
	BaseClass::AbortReload();
}

bool CBaseShotgun::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	// S:0x3C3750 / C:0x2F2180. Do not transfer, refund, finish or reset
	// historical timing/stage/shell fields on holster, even if it is rejected.
	m_reloadState = 0;
	m_bInReload = false;
	return BaseClass::Holster( pSwitchingTo );
}

#pragma float_control( precise, on, push )
void CBaseShotgun::UpdateReloadQueue()
{
	// S:0x3C3500 / C:0x2F2020: unlike a magazine gun, either attack
	// button cancels the queued reload. Retrying must not extend its window.
	if ( !( m_reloadQueuedStartTime > 0.0f ) )
		return;
	if ( gpGlobals->curtime > m_reloadQueuedStartTime )
	{
		m_reloadQueuedStartTime = 0.0f;
		return;
	}
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
	if ( pPlayer->m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) )
	{
		m_reloadQueuedStartTime = 0.0f;
		return;
	}
	const float flQueuedTime = m_reloadQueuedStartTime;
	Reload();
	if ( m_reloadQueuedStartTime > 0.0f && m_reloadQueuedStartTime != flQueuedTime )
		m_reloadQueuedStartTime = flQueuedTime;
}
#pragma float_control( pop )

Activity CBaseShotgun::TranslateViewModelActivity( Activity activity )
{
	// S:0x3C2A10 / C:0x2F1A00, original activity identities, not IDs.
	static const Activity s_PrimaryLayer = ShotgunActivity( "ACT_VM_PRIMARYATTACK_LAYER" );
	static const Activity s_Loop = ShotgunActivity( "ACT_VM_RELOAD_LOOP" );
	static const Activity s_LoopLayer = ShotgunActivity( "ACT_VM_RELOAD_LOOP_LAYER" );
	static const Activity s_End = ShotgunActivity( "ACT_VM_RELOAD_END" );
	static const Activity s_EndLayer = ShotgunActivity( "ACT_VM_RELOAD_END_LAYER" );
	if ( activity == ACT_VM_PRIMARYATTACK )
		return s_PrimaryLayer;
	if ( activity == s_Loop )
		return s_LoopLayer;
	if ( activity == s_End )
		return s_EndLayer;
	return BaseClass::TranslateViewModelActivity( activity );
}

acttable_t *CBaseShotgun::ActivityList( int &iActivityCount )
{
	// S:0x3C2980/0x3C2990, original table S:0x7B8178.
	static const char *s_pNames[][2] =
	{
		{ "ACT_IDLE", "ACT_IDLE_PUMPSHOTGUN" },
		{ "ACT_CROUCHIDLE", "ACT_CROUCHIDLE_PUMPSHOTGUN" },
		{ "ACT_RUN_CROUCH", "ACT_RUN_CROUCH_PUMPSHOTGUN" },
		{ "ACT_WALK", "ACT_WALK_PUMPSHOTGUN" },
		{ "ACT_RUN", "ACT_RUN_PUMPSHOTGUN" },
		{ "ACT_IDLE_INJURED", "ACT_IDLE_INJURED_PUMPSHOTGUN" },
		{ "ACT_WALK_INJURED", "ACT_WALK_INJURED_PUMPSHOTGUN" },
		{ "ACT_RUN_INJURED", "ACT_RUN_INJURED_PUMPSHOTGUN" },
		{ "ACT_IDLE_CALM", "ACT_IDLE_CALM_PUMPSHOTGUN" },
		{ "ACT_WALK_CALM", "ACT_WALK_CALM_PUMPSHOTGUN" },
		{ "ACT_RUN_CALM", "ACT_RUN_CALM_PUMPSHOTGUN" },
		{ "ACT_JUMP", "ACT_JUMP_SHOTGUN" }
	};
	static acttable_t s_Table[ARRAYSIZE( s_pNames )];
	static bool s_bInitialized = false;
	if ( !s_bInitialized )
	{
		for ( int i = 0; i < ARRAYSIZE( s_pNames ); ++i )
		{
			s_Table[i].baseAct = ShotgunActivity( s_pNames[i][0] );
			s_Table[i].weaponAct = ShotgunActivity( s_pNames[i][1] );
			s_Table[i].required = false;
		}
		s_bInitialized = true;
	}
	iActivityCount = ARRAYSIZE( s_Table );
	return s_Table;
}

Activity CBaseShotgun::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	// S:0x3C2A50 ->0x24EA30: all shotgun shove/melee translations are
	// intentionally the rifle names; only the firearm activity is pump-specific.
	static const Activity s_Melee[][2] =
	{
		{ ShotgunActivity( "ACT_MELEE_SWEEP_RIFLE_IDLE" ), ShotgunActivity( "ACT_MELEE_SWEEP_RIFLE_RUN" ) },
		{ ShotgunActivity( "ACT_MELEE_SHOVE_RIFLE_IDLE" ), ShotgunActivity( "ACT_MELEE_SHOVE_RIFLE_RUN" ) },
		{ ShotgunActivity( "ACT_MELEE_STRAIGHT_RIFLE_IDLE" ), ShotgunActivity( "ACT_MELEE_STRAIGHT_RIFLE_RUN" ) },
		{ ShotgunActivity( "ACT_MELEE_STOMP_RIFLE_IDLE" ), ShotgunActivity( "ACT_MELEE_STOMP_RIFLE_WALK" ) }
	};
	if ( event >= TERROR_ANIMEVENT_MELEE_SWEEP && event <= TERROR_ANIMEVENT_MELEE_STOMP )
		return s_Melee[event - TERROR_ANIMEVENT_MELEE_SWEEP][bMoving ? 1 : 0];
	static const Activity s_Primary = ShotgunActivity( "ACT_PRIMARYATTACK_PUMPSHOTGUN" );
	return s_Primary;
}

Activity CBaseShotgun::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3C2A80: event5 is loop, event38 start, everything else end.
	static const Activity s_Loop = ShotgunActivity( "ACT_RELOAD_PUMPSHOTGUN_LOOP" );
	static const Activity s_Start = ShotgunActivity( "ACT_RELOAD_PUMPSHOTGUN_START" );
	static const Activity s_End = ShotgunActivity( "ACT_RELOAD_PUMPSHOTGUN_END" );
	if ( event == TERROR_ANIMEVENT_RELOAD_LOOP )
		return s_Loop;
	if ( event == TERROR_ANIMEVENT_RELOAD_START )
		return s_Start;
	return s_End;
}

Activity CBaseShotgun::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	// S:0x3C1550 is shared by both shotgun families.
	static const Activity s_Deploy = ShotgunActivity( "ACT_DEPLOY_SHOTGUN" );
	return s_Deploy;
}

#ifndef CLIENT_DLL
BEGIN_DATADESC( CBaseShotgun )
END_DATADESC()
#endif
