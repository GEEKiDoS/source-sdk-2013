//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/effects/terror_glow_scene.h"
#else
#include "l4d2/player/terrorplayer.h"
#endif
#include "l4d2/weapons/terror_weapon.h"
#include "in_buttons.h"
#include "activitylist.h"
#include "l4d2/terror_shareddefs.h"
#ifndef CLIENT_DLL
#include "l4d2/player/terror_lagcompensation.h"
#include "ilagcompensationmanager.h"
#else
#include "effect_dispatch_data.h"
#include "c_te_effect_dispatch.h"
#endif
#ifdef CLIENT_DLL
#include "l4d2/weapons/c_weapon_spawn.h"
#include "l4d2/entities/c_prop_health_cabinet.h"
#else
#include "l4d2/weapons/weapon_spawn.h"
#include "l4d2/entities/prop_health_cabinet.h"
#endif
#include "l4d2/effects/terror_fx_shared.h"
#include "ammodef.h"
extern ConVar player_use_radius;
extern bool g_bDebugPlayerUse;
extern ConVar survivor_allow_crawling;
#include "l4d2/gamerules/terror_gamerules.h"
#include "debugoverlay_shared.h"
#include "datacache/imdlcache.h"
#include <xmmintrin.h>
#include "rumble_shared.h"
#ifdef CLIENT_DLL
#include "cliententitylist.h"
#include "c_rumble.h"
#endif
#include "precache_register.h"
#include "particle_parse.h"
#include "l4d2/weapons/terror_weapon_console_server_cvars.inc"
#include "l4d2/terror_trace_filter.h"
#include "tier0/memdbgon.h"

ConVar allow_weapon_fire_to_use( "allow_weapon_fire_to_use", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
ConVar survivor_helping_hand_inhibit_duration( "survivor_helping_hand_inhibit_duration", "1", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
ConVar helping_hand_debug( "helping_hand_debug", "0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
ConVar survivor_incapacitated_reload_multiplier( "survivor_incapacitated_reload_multiplier", "1.25", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Multiplier to reload time when incapacitated." );
static ConVar survivor_incapacitated_dizzy_timer( "survivor_incapacitated_dizzy_timer", "2.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
,
	"How often our incap'd dizzy-drift changes directions." );
static ConVar survivor_incapacitated_dizzy_severity( "survivor_incapacitated_dizzy_severity", "2.0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
,
	"From negative to positive this, how much bump we add to a incap'd player's mouse." );

#ifndef CLIENT_DLL
BEGIN_DATADESC( CTerrorWeapon )
	DEFINE_THINKFUNC( GiveThink ),
END_DATADESC()
#endif

BEGIN_NETWORK_TABLE_NOBASE( IntervalTimer, DT_IntervalTimer )
#ifdef CLIENT_DLL
	RecvPropFloat( RECVINFO( m_timestamp ) ),
#else
	SendPropFloat( SENDINFO( m_timestamp ), 0, SPROP_NOSCALE ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA_NO_BASE( IntervalTimer )
	DEFINE_PRED_FIELD( m_timestamp, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

BEGIN_NETWORK_TABLE_NOBASE( CountdownTimer, DT_CountdownTimer )
#ifdef CLIENT_DLL
	RecvPropFloat( RECVINFO( m_duration ) ),
	RecvPropFloat( RECVINFO( m_timestamp ) ),
#else
	SendPropFloat( SENDINFO( m_duration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_timestamp ), 0, SPROP_NOSCALE ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA_NO_BASE( CountdownTimer )
	DEFINE_PRED_FIELD( m_duration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_timestamp, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

BEGIN_SIMPLE_DATADESC( CountdownTimer )
	DEFINE_KEYFIELD( m_duration, FIELD_FLOAT, "m_duration" ),
	DEFINE_KEYFIELD( m_timestamp, FIELD_TIME, "m_timestamp" ),
END_DATADESC()

#ifdef CLIENT_DLL
BEGIN_RECV_TABLE_NOBASE( CTerrorWeapon, DT_LocalActiveL4DWeaponData )
	RecvPropDataTable( RECVINFO_DT( m_helpingHandSuppressionTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_helpingHandTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_helpingHandTargetTimer ), 0, &REFERENCE_RECV_TABLE( DT_IntervalTimer ) ),
	RecvPropInt( RECVINFO( m_helpingHandState ) ),
	RecvPropEHandle( RECVINFO( m_helpingHandTarget ) ),
	RecvPropFloat( RECVINFO( m_helpingHandExtendDuration ) ),
	RecvPropFloat( RECVINFO( m_reloadQueuedStartTime ) ),
	RecvPropBool( RECVINFO( m_releasedAltFireButton ) ),
	RecvPropBool( RECVINFO( m_releasedFireButton ) ),
	RecvPropBool( RECVINFO( m_isHoldingAltFireButton ) ),
	RecvPropBool( RECVINFO( m_isHoldingFireButton ) ),
	RecvPropBool( RECVINFO( m_bPickedUpOnTransition ) ),
	RecvPropInt( RECVINFO( m_DroppedByInfectedGender ) ),
	RecvPropInt( RECVINFO( m_iBloodyWeaponLevel ) ),
END_RECV_TABLE()
#else
extern void *SendProxy_SendLocalWeaponDataTable( const SendProp *, const void *, const void *, CSendProxyRecipients *, int );
BEGIN_SEND_TABLE_NOBASE( CTerrorWeapon, DT_LocalActiveL4DWeaponData )
	SendPropDataTable( SENDINFO_DT( m_helpingHandSuppressionTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ), SendProxy_DataTableToDataTable ),
	SendPropDataTable( SENDINFO_DT( m_helpingHandTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropDataTable( SENDINFO_DT( m_helpingHandTargetTimer ), &REFERENCE_SEND_TABLE( DT_IntervalTimer ) ),
	SendPropInt( SENDINFO( m_helpingHandState ), 32 ),
	SendPropEHandle( SENDINFO( m_helpingHandTarget ) ),
	SendPropFloat( SENDINFO( m_helpingHandExtendDuration ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_reloadQueuedStartTime ), 0, SPROP_NOSCALE ),
	SendPropBool( SENDINFO( m_releasedAltFireButton ) ),
	SendPropBool( SENDINFO( m_releasedFireButton ) ),
	SendPropBool( SENDINFO( m_isHoldingAltFireButton ) ),
	SendPropBool( SENDINFO( m_isHoldingFireButton ) ),
	SendPropBool( SENDINFO( m_bPickedUpOnTransition ) ),
	SendPropInt( SENDINFO( m_DroppedByInfectedGender ), 6, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iBloodyWeaponLevel ), 4, SPROP_UNSIGNED ),
END_SEND_TABLE()
#endif
#ifdef CLIENT_DLL
IMPLEMENT_CLIENTCLASS( C_TerrorWeapon, DT_TerrorWeapon, CTerrorWeapon )
#else
IMPLEMENT_SERVERCLASS( CTerrorWeapon, DT_TerrorWeapon )
#endif

#ifdef CLIENT_DLL
// C30F490: the active weapon and its clone share the actual owner's stencil.
bool CTerrorWeapon::GetStencilState( ShaderStencilState_t &state )
{
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() || m_iState != WEAPON_IS_ACTIVE )
		return BaseClass::GetStencilState( state );
	owner = GetOwner(); // Genuine second owner lookup after the admission checks.
	C_TerrorPlayer *player = owner && owner->IsPlayer() ? static_cast< C_TerrorPlayer * >( owner ) : NULL;
	if ( TerrorGlow_GetParentStencilState( player, state ) )
		return true;
	return BaseClass::GetStencilState( state );
}
#endif
BEGIN_NETWORK_TABLE( CTerrorWeapon, DT_TerrorWeapon )
#ifdef CLIENT_DLL
	RecvPropDataTable( "LocalL4DWeaponData", 0, 0, &REFERENCE_RECV_TABLE( DT_LocalActiveL4DWeaponData ) ),
	RecvPropDataTable( RECVINFO_DT( m_attackTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropDataTable( RECVINFO_DT( m_swingTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
	RecvPropInt( RECVINFO( m_nUpgradedPrimaryAmmoLoaded ) ),
#else
	SendPropDataTable( "LocalL4DWeaponData", 0, &REFERENCE_SEND_TABLE( DT_LocalActiveL4DWeaponData ), SendProxy_SendLocalWeaponDataTable ),
	SendPropDataTable( SENDINFO_DT( m_attackTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropDataTable( SENDINFO_DT( m_swingTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
	SendPropInt( SENDINFO( m_nUpgradedPrimaryAmmoLoaded ), 8, SPROP_UNSIGNED ),
#endif
END_NETWORK_TABLE()

#ifdef CLIENT_DLL
BEGIN_PREDICTION_DATA( CTerrorWeapon )
#ifndef NO_ENTITY_PREDICTION
	{ FIELD_EMBEDDED, "m_helpingHandTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_helpingHandTimer ), 0 },
		1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &IntervalTimer::m_PredMap },
#else
	DEFINE_FIELD_NULL,
#endif
	DEFINE_PRED_FIELD( m_helpingHandState, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_helpingHandTarget, FIELD_EHANDLE, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_helpingHandExtendDuration, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD_TOL( m_reloadQueuedStartTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE, 0.001f ),
	DEFINE_PRED_FIELD( m_releasedAltFireButton, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_releasedFireButton, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_isHoldingAltFireButton, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_isHoldingFireButton, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
#ifndef NO_ENTITY_PREDICTION
	{ FIELD_EMBEDDED, "m_swingTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_swingTimer ), 0 },
		1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
#else
	DEFINE_FIELD_NULL,
#endif
#ifndef NO_ENTITY_PREDICTION
	{ FIELD_EMBEDDED, "m_attackTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_attackTimer ), 0 },
		1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
#else
	DEFINE_FIELD_NULL,
#endif
	DEFINE_PRED_FIELD( m_iBloodyWeaponLevel, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
END_PREDICTION_DATA()
#endif

// S3F0C10/C311220: real timer/handle/container member lifetimes run above.
// Their native bodies explicitly store ONLY queued-reload and server gender.
CTerrorWeapon::CTerrorWeapon()
{
	m_reloadQueuedStartTime = 0.0f;
#ifndef CLIENT_DLL
	m_DroppedByInfectedGender = 0;
#endif
}

// S3ED3A0, original virtual387. Equality is not attacking.
bool CTerrorWeapon::IsAttacking() const
{
	return static_cast< double >( m_attackTimer.m_timestamp.Get() ) -
		static_cast< double >( gpGlobals->curtime ) > 0.0;
}

void CTerrorWeapon::Spawn()
{
	BaseClass::Spawn();
	m_bFiresUnderwater = true;
	m_bAltFiresUnderwater = true;
	m_attackTimer.Invalidate();
	m_swingTimer.Invalidate();
	m_bSwingInProgress = false;
#ifdef CLIENT_DLL
	m_incapacitatedDizzyMouseX = 0.0f; // Actual C311512/C31151A, before HelpingHand.
	m_incapacitatedDizzyMouseY = 0.0f;
#endif
	SetHelpingHandState( 0 );
	m_bSendingHelpingHandAnim = false;
#ifndef CLIENT_DLL
	m_ownerTransferTimer.Invalidate();
	m_hPreviousOwner = NULL;
	m_hGiveTarget = NULL;
	SetContextThink( NULL, 0.0f, "Weapon_GiveThink" );
#endif
	m_bPickedUpOnTransition = false;
}

void CTerrorWeapon::Precache()
{
	BaseClass::Precache();
	PrecacheEffect( "ExplosionForce" );
	if ( CBaseEntity::IsPrecacheAllowed() )
	{
		GetTerrorWpnData().Precache();
		PrecacheParticleSystem( "weapon_laser_sight" );
		PrecacheParticleSystem( "weapon_laser_sight_FP" );
		PrecacheParticleSystem( "weapon_laser_sight_worldmodel" );
	}
}

CTerrorPlayer *CTerrorWeapon::GetTerrorPlayerOwner() const
{
	return ToTerrorPlayer( GetOwner() );
}

const char *CTerrorWeapon::GetViewModel( int ) const
{
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return BaseClass::GetViewModel( 0 );
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( owner );
	const int team = player->GetTeamNumber();
	if ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR )
	{
		const int character = player->GetSurvivorCharacter();
		const CTerrorWeaponInfo &info = GetTerrorWpnData();
		const bool dual = IsDualWielding();
		return info.GetCharacterViewModel( GetSurvivorCharacterName( character ), dual );
	}
	if ( player->GetTeamNumber() != TEAM_INFECTED )
		return BaseClass::GetViewModel( 0 );
#ifdef CLIENT_DLL
	const int zombieClass = player->GetZombieClass();
#else
	const int infectedTeam = player->GetTeamNumber();
	const int zombieClass = infectedTeam == TEAM_SURVIVOR || infectedTeam == TEAM_L4D1_SURVIVOR ? 9 : player->GetZombieClass();
#endif
	const CTerrorWeaponInfo &info = GetTerrorWpnData();
	return info.GetCharacterViewModel( GetZombieClassName( zombieClass ), false );
}

Activity CTerrorWeapon::TranslatePlayerActivity( Activity activity )
{
	// S3EC760 is the genuine separate v388 wrapper around v355.
	return ActivityOverride( activity, NULL );
}

bool CTerrorWeapon::CanFidget()
{
	CTerrorPlayer *pOwner = GetTerrorPlayerOwner();
	if ( !pOwner || !pOwner->IsPlayer() || pOwner->IsIncapacitated() )
		return false;
#ifndef CLIENT_DLL
	if ( pOwner->m_dragTarget.Get() )
		return false;
	CBaseCombatWeapon *pActiveWeapon = pOwner->GetActiveWeapon();
	if ( pActiveWeapon && pActiveWeapon->m_bInReload )
		return false;
	if ( !( pOwner->GetTimeSinceLastInjury( TEAM_ANY ) >= 5.0f ) )
		return false;
#endif
	return pOwner->GetViewEntity() == NULL &&
		!pOwner->m_isHangingFromLedge &&
		pOwner->GetMoveType() != MOVETYPE_LADDER &&
		pOwner->m_reviveTarget.Get() == NULL &&
		pOwner->m_reviveOwner.Get() == NULL &&
		!pOwner->m_isHangingFromTongue &&
		!( pOwner->m_tongueOwner.Get() && !pOwner->m_isHangingFromTongue &&
			pOwner->m_tongueVictimTimer.IsGreaterThen( 1.0f ) ) &&
		pOwner->GetUseEntity() == NULL &&
		pOwner->m_pushEntity.Get() == NULL &&
		pOwner->m_pounceAttacker.Get() == NULL &&
		pOwner->m_pummelAttacker.Get() == NULL &&
		!pOwner->IsKnockedDown();
}

void CTerrorWeapon::PreventFireUntilButtonRelease()
{
	m_releasedFireButton = false;
}

static ConVar z_gun_swing_interval( "z_gun_swing_interval", "0.7", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_duration( "z_gun_swing_duration", "0.2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_range( "z_gun_range", "75", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_vs_cooldown( "z_gun_swing_vs_cooldown", "1.0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_vs_restore_time( "z_gun_swing_vs_restore_time", "4.0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_vs_amt_penalty( "z_gun_swing_vs_amt_penalty", "1.0", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_coop_min_penalty( "z_gun_swing_coop_min_penalty", "5", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
static ConVar z_gun_swing_coop_max_penalty( "z_gun_swing_coop_max_penalty", "8", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );

ConVar z_show_swings( "z_show_swings", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_gun_swing_vs_min_penalty( "z_gun_swing_vs_min_penalty", "3", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
ConVar z_gun_swing_vs_max_penalty( "z_gun_swing_vs_max_penalty", "6", 
#if defined( CLIENT_DLL )
	0x0000200A
#elif defined( GAME_DLL )
	0x00002006
#else
	FCVAR_DEVELOPMENTONLY | FCVAR_REPLICATED
#endif
 );
#pragma float_control( precise, on, push )
void CTerrorWeapon::ItemPostFrame()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( player )
	{
		m_releasedAltFireButton = m_releasedAltFireButton || !( player->m_nButtons & IN_ATTACK2 );
		m_releasedFireButton = m_releasedFireButton || !( player->m_nButtons & IN_ATTACK );
		m_isHoldingAltFireButton = m_isHoldingAltFireButton && ( player->m_nButtons & IN_ATTACK2 ) != 0;
		m_isHoldingFireButton = m_isHoldingFireButton && ( player->m_nButtons & IN_ATTACK ) != 0;
		UpdateReloadInterruption();
	}
	// Native2502A0 has NO owner argument: ASM proves its own GetOwner/RTTI.
	if ( UseCSBaseItemPostFrame() )
		BaseClass::ItemPostFrame();
	else if ( IsWeaponVisible() )
		CBaseCombatWeapon::ItemPostFrame();
	else
	{
		UpdateReloadQueue();
		return;
	}
	UpdateHelpingHand();
	if ( !player || ( static_cast< double >( gpGlobals->curtime ) >=
		static_cast< double >( player->m_stunTimer.m_timestamp.Get() ) &&
		!player->IsBeingPounced() && !player->IsPummelVictim() ) )
	{
		if ( static_cast< double >( gpGlobals->curtime ) <
			static_cast< double >( m_swingTimer.m_timestamp.Get() ) )
			UpdateSwing();
		else if ( m_bSwingInProgress )
		{
			m_bSwingInProgress = false;
			UpdateSwing();
			OnSwingEnd( m_swingAffectedEntities.Count() > 0 );
		}
	}
	else
		CancelSwing(); // Native interruption samples count before clearing; NO final sweep.
	UpdateReloadQueue();
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
void CTerrorWeapon::UpdateReloadQueue()
{
	if ( m_reloadQueuedStartTime > 0.0f )
	{
		if ( gpGlobals->curtime <= m_reloadQueuedStartTime )
		{
			CTerrorPlayer *player = GetTerrorPlayerOwner();
			if ( player )
			{
				if ( player->m_nButtons & ( IN_ATTACK | IN_ATTACK2 ) )
					m_reloadQueuedStartTime = 0.0f;
				else
				{
					const float queued = m_reloadQueuedStartTime;
					Reload();
					if ( m_reloadQueuedStartTime > 0.0f && m_reloadQueuedStartTime != queued )
						m_reloadQueuedStartTime = queued;
				}
			}
		}
		else
			m_reloadQueuedStartTime = 0.0f;
	}
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
// S3F12C0/C311AC0: real common busy path, NOT the post-frame driver/queue copy.
void CTerrorWeapon::ItemBusyFrame()
{
	CBaseCombatWeapon::ItemBusyFrame(); // Native04DC90/C017E40 FIRST.
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( owner );
	m_releasedAltFireButton = m_releasedAltFireButton || !( player->m_nButtons & IN_ATTACK2 );
	m_releasedFireButton = m_releasedFireButton || !( player->m_nButtons & IN_ATTACK );
	if ( !( player->m_nButtons & IN_ATTACK ) && m_isHoldingFireButton )
		m_isHoldingFireButton = false;
	if ( !( player->m_nButtons & IN_ATTACK2 ) && m_isHoldingAltFireButton )
		m_isHoldingAltFireButton = false;
	if ( static_cast< double >( gpGlobals->curtime ) <
		static_cast< double >( player->m_stunTimer.m_timestamp.Get() ) ||
		player->IsBeingPounced() || player->IsPummelVictim() )
	{
		if ( m_bSwingInProgress )
		{
			const bool hit = m_swingAffectedEntities.Count() > 0;
			m_bSwingInProgress = false;
			OnSwingEnd( hit ); // No final sweep, affected-list clear or timer reset here.
		}
		UpdateReloadQueue();
		return;
	}
	if ( ( player->m_nButtons & IN_ATTACK2 ) && CanUseSecondaryShove() )
	{
		if ( CanUseSecondaryShoveWhileReloading() || !m_bInReload )
			player->SetNextAttack( gpGlobals->curtime );
	}
	if ( m_bInReload )
		UpdateReloadInterruption();
	UpdateReloadQueue();
}
#pragma float_control( pop )

Activity CTerrorWeapon::GetDeployActivity()
{
	return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_DEPLOY ); // S24D730.
}

// S3EC600 / C30EC20: the Terror use-state policy precedes CS deployment.
bool CTerrorWeapon::CanDeploy()
{
	return CanBeUsedBy( GetTerrorPlayerOwner() ) && BaseClass::CanDeploy();
}

// S3F1180: released/held snapshots and suppression precede the real CS deploy.
bool CTerrorWeapon::Deploy()
{
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return false;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( owner );
	m_releasedFireButton = !( player->m_nButtons & IN_ATTACK );
	m_releasedAltFireButton = !( player->m_nButtons & IN_ATTACK2 );
	m_isHoldingFireButton = false;
	m_isHoldingAltFireButton = false;
	SetContextThink( NULL, 0.0f, "Weapon_GiveThink" );
	SuppressHelpingHand( 2.0f );
	return BaseClass::Deploy();
}

bool CTerrorWeapon::SendWeaponAnim( int iActivity )
{
	const int layer = GetViewModelLayer( static_cast< Activity >( iActivity ) );
	const Activity mappedIdle = TranslateViewModelActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_IDLE ) );
	if ( !m_bSendingHelpingHandAnim && iActivity != mappedIdle && layer == 0 )
	{
		m_helpingHandTargetTimer.Invalidate();
		SetHelpingHandState( 0 );
	}
	return BaseClass::SendWeaponAnim( iActivity );
}

bool CTerrorWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	CancelSwing();
	return BaseClass::Holster( pSwitchingTo );
}

#pragma float_control( precise, on, push )
void CTerrorWeapon::SecondaryAttack()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player || player->IsIncapacitated() || !player->CanAttack() )
		return;
	// Native3D0FB0 cancels actual HelpingHand state and then returns false.
	SetHelpingHandState( 0 );
	if ( !IsWeaponVisible() )
		return;
	if ( !CanUseSecondaryShove() )
	{
		BaseClass::SecondaryAttack(); // Real CS24FA80, not inherited generic nullsub.
		return;
	}
	if ( player->GetFOV() != player->GetDefaultFOV() )
	{
		if ( HasScope() )
			ToggleZoom();
		else
			player->SetFOV( player, player->GetDefaultFOV(), 0.0f, 0 );
	}
	Swing( z_gun_swing_interval.GetFloat(), z_gun_swing_duration.GetFloat(), z_gun_range.GetFloat() );
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
Vector CTerrorWeapon::GetSwingDirection() const // S:3ECBB0
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner ) return vec3_origin;
	Vector forward;
	owner->EyeVectors( &forward );
	return forward;
}

float CTerrorWeapon::GetSwingStartYaw() const { return -45.0f; }

float CTerrorWeapon::GetSwingEndYaw() const { return 45.0f; }

float CTerrorWeapon::GetSwingStartPitch() const { return 0.0f; }

float CTerrorWeapon::GetSwingEndPitch() const { return 0.0f; }

void CTerrorWeapon::CancelSwing()
{
	if ( m_bSwingInProgress )
	{
		const bool hitAny = m_swingAffectedEntities.Count() > 0;
		m_bSwingInProgress = false;
		OnSwingEnd( hitAny );
	}
}

void CTerrorWeapon::Swing( float interval, float duration, float range ) // S:3F06F0, inherited native435
{
	if ( IsAttacking() ) return;
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player || !player->IsShoveAvailable() || player->IsIncapacitated() || player->IsGhost() ) return;
	if ( player->IsLunging() ) return;
	if ( player->m_isHangingFromTongue ||
		( player->m_tongueOwner.Get() && !player->m_isHangingFromTongue && static_cast< double >( gpGlobals->curtime ) - player->m_tongueVictimTimer.GetStartTime() > 1.0 ) ||
		static_cast< double >( gpGlobals->curtime ) < player->m_stunTimer.m_timestamp.Get() || player->m_pounceAttacker.Get() || player->m_pummelAttacker.Get() || !m_releasedAltFireButton ) return;
	SuppressHelpingHand( 0.0f );
	float penaltyTime = 0.0f;
	if ( ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) && !player->IsBot() )
	{
		const int oldPenalty = player->m_iShovePenalty;
		if ( player->IsAdrenalineActive() ) player->m_iShovePenalty = 0;
		else
		{
			const float sinceShove = static_cast< float >( static_cast< double >( gpGlobals->curtime ) -
				static_cast< double >( player->m_flNextShoveTime ) );
			const float cool = z_gun_swing_vs_cooldown.GetFloat();
			if ( cool != 0.0f )
			{
				const int minimum = TerrorGameRules()->IsCoopMode() ? z_gun_swing_coop_min_penalty.GetInt() : z_gun_swing_vs_min_penalty.GetInt(); // native queries mode independently for min/max
				const int maximum = TerrorGameRules()->IsCoopMode() ? z_gun_swing_coop_max_penalty.GetInt() : z_gun_swing_vs_max_penalty.GetInt();
				// S3F08B6/C3117E2: JB also takes the restoration path when unordered.
				const float change = !( cool >= sinceShove ) ? -RemapValClamped( sinceShove - cool, 0.0f, z_gun_swing_vs_restore_time.GetFloat(), 0.0f, static_cast< float >( maximum ) ) : 1.0f;
				float value = static_cast< float >( oldPenalty ) + change;
				if ( value > maximum ) value = static_cast< float >( maximum ); else if ( value < 0.0f ) value = 0.0f;
				const int penalty = _mm_cvttss_si32( _mm_set_ss( value ) ); // Native3F0932.
				if ( penalty >= minimum ) penaltyTime = RemapValClamped( static_cast< float >( penalty ), static_cast< float >( minimum ), static_cast< float >( maximum ), 0.0f, z_gun_swing_vs_amt_penalty.GetFloat() );
				player->m_iShovePenalty = penalty;
			}
		}
#ifdef CLIENT_DLL
		player->m_flLastShoveTime = gpGlobals->curtime; // C311620 ->actual client7344, human survivor path ONLY
#endif
	}
	m_swingTimer.Start( duration );
	m_attackTimer.Start( interval );
	m_bSwingInProgress = true;
	m_flSwingRange = range;
	m_swingAffectedEntities.RemoveAll();
	m_nSwingFrames = 0;
	m_vecLastSwingDirection = vec3_origin;
	UpdateSwing();
	#ifdef CLIENT_DLL
	Vector forward;
	player->EyeVectors( &forward );
	CEffectData impact;
	impact.m_vStart = player->Weapon_ShootPosition();
	impact.m_vOrigin = impact.m_vStart + forward * m_flSwingRange;
	impact.m_nDamageType = DMG_CLUB;
	// Native C311620 begins MDLCache004 lock26 HERE, before dispatch; its
	// matching27 runs only AFTER timers, next-shove setter and virtualOnSwing.
	MDLCACHE_CRITICAL_SECTION();
	DispatchEffect( "RagdollImpact", impact );
	#endif
	const float next = gpGlobals->curtime + interval;
	if ( m_bInReload )
	{
		if ( !( m_flNextPrimaryAttack > next ) ) m_flNextPrimaryAttack = next;
		if ( !( m_flNextSecondaryAttack > next ) ) m_flNextSecondaryAttack = next;
	}
	else { m_flNextPrimaryAttack = next; m_flNextSecondaryAttack = next; }
	player->ExtendNextShoveTime( next + penaltyTime ); // S2CB6E0/C26CFC0: strict greater-only update.
	OnSwing();
}

// Native3F14F0: caller ignores incidental EAX; distinct from CheckReload04E0C0.
void CTerrorWeapon::UpdateReloadInterruption()
{
	CTerrorPlayer *player = GetTerrorPlayerOwner();
	if ( !player || IsAttacking() || !m_bInReload )
		return;
	if ( player->m_reviveTarget.Get() ||
		( m_bReloadsSingly && ( player->m_nButtons & IN_ATTACK ) && m_iClip1 > 0 ) )
	{
		if ( gpGlobals->curtime >= m_reloadStartActivityTime )
		{
			player->SetNextAttack( gpGlobals->curtime );
			m_bInReload = false;
			m_flNextSecondaryAttack = gpGlobals->curtime;
			m_flNextPrimaryAttack = m_flNextSecondaryAttack;
			AbortReload(); // Native virtual1120; Gun3E6F50 is the real override.
		}
	}
}

#ifdef CLIENT_DLL
bool CTerrorWeapon::TraceSwing( const Vector &direction ) // C312420, CLIENT producer is not a damage alias
{
	CBasePlayer *player = ToBasePlayer( GetOwner() );
	if ( !player ) return false;
	const Vector start = player->Weapon_ShootPosition();
	const Vector end = player->Weapon_ShootPosition() + direction * m_flSwingRange;
	const Vector mins( -3.0f, -3.0f, -3.0f ), maxs( 3.0f, 3.0f, 3.0f );
	CTraceFilterSimpleListNoGhosts filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( player );
	for ( int i = 0; i < m_swingAffectedEntities.Count(); ++i )
	{
		CBaseEntity *entity = m_swingAffectedEntities[i].Get();
		if ( entity && entity->MyCombatCharacterPointer() ) filter.AddEntityToIgnore( entity );
	}
	filter.AddEntityToIgnore( cl_entitylist->GetBaseEntityFromHandle( CBaseHandle::UnsafeFromIndex( 0 ) ) ); // C10044C70 resolves raw handle0, not NULL/invalid and not a viewmodel
	trace_t trace;
	UTIL_TraceHull( start, end, mins, maxs, MASK_SHOT | CONTENTS_GRATE, &filter, &trace );
	if ( r_visualizetraces.GetBool() ) NDebugOverlay::Line( trace.startpos, trace.endpos, 255, 255, 0, true, -1.0f );
	if ( z_show_swings.GetInt() != 0 )
	{
		if ( z_show_swings.GetInt() <= 1 ) NDebugOverlay::Line( start, end, 255, 255, 0, true, 1.0f );
		else NDebugOverlay::SweptBox( start, end, mins, maxs, vec3_angle, 255, 255, 0, 255, 3.0f );
	}
	CBaseEntity *entity = trace.m_pEnt;
	if ( !trace.DidHit() || !entity ) return false; // native3127B6 XOR AL,AL; decompiler's cookie return is misleading
	if ( m_swingAffectedEntities.Find( entity ) != m_swingAffectedEntities.InvalidIndex() ) return OnSwingHit( trace, direction, false );
	if ( m_swingAffectedEntities.Count() < 128 ) m_swingAffectedEntities.AddToTail( entity );
	return OnSwingHit( trace, direction, true ); // C30FBC0 append happens BEFORE generic callback
}
// Native base client callbacks are real asymmetries, not server damage aliases.
void CTerrorWeapon::OnSwingEnd( bool hitAny ) {} // C:30F0E0 is genuinely empty
bool CTerrorWeapon::OnSwingHit( trace_t &trace, const Vector &direction, bool newHit ) // C:30F0F0
{
	if ( newHit && trace.m_pEnt && ( trace.m_pEnt->IsPlayer() || trace.m_pEnt->IsNextBot() ) ) RumbleEffect( 5, 0, RUMBLE_FLAG_RESTART );
	return false;
}
#endif

#pragma float_control( pop )
void CTerrorWeapon::OnSwing()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
#ifndef CLIENT_DLL
	pPlayer->SetNoiseLevel( GetTerrorWpnData().m_flNoiseFactor );
	m_bStompTarget = false;
	m_hStompTarget = NULL;
#endif
	SuppressHelpingHand();
	// The common VM send intentionally overwrites a melee script secondary.
	SendWeaponAnim( ActivityList_IndexForName( "ACT_VM_MELEE" ) );
#ifndef CLIENT_DLL
	// S3F1882 precedes target selection; the client C30EFE0 has no such tail.
	pPlayer->DelayCalm( 2.0f );
	pPlayer->DoTerrorAnimationEvent( pPlayer->SelectTerrorShoveAnimation( this ), 0 );
	pPlayer->RumbleEffect( RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART );
	pPlayer->EmitSound( "Weapon.Swing" );
	pPlayer->OnShove();
#endif
}

#pragma float_control( precise, on, push )
void CTerrorWeapon::UpdateSwing()
{
 CTerrorPlayer *player = GetTerrorPlayerOwner();
 if ( !player ) return;
#ifndef CLIENT_DLL
 StartTerrorLagCompensation( player, TERROR_LAG_COMPENSATE_BOUNDS,
                            vec3_origin, vec3_angle, 0.0f );
#endif
 Vector forward = GetSwingDirection();
 const float elapsed = static_cast<float>( static_cast<double>(gpGlobals->curtime)
                       - m_swingTimer.m_timestamp.Get() + m_swingTimer.m_duration.Get() );
 const float duration = m_swingTimer.m_timestamp > 0.0f ?
                        m_swingTimer.m_duration.Get() : 0.0f;
 float progress = elapsed / duration;
 if ( progress > 1.0f ) progress = 1.0f;
 else if ( progress < 0.0f ) progress = 0.0f;
 // Unordered passes through, as native COMISS/JBE does. No invented zero guard.
 QAngle angles;
 VectorAngles( forward, angles );
 const float inverse = 1.0f - progress;
 angles.y = static_cast<float>(static_cast<double>(GetSwingStartYaw()) * inverse + angles.y);
 angles.y = static_cast<float>(static_cast<double>(GetSwingEndYaw()) * progress + angles.y);
 angles.x = static_cast<float>(static_cast<double>(GetSwingStartPitch()) * inverse + angles.x);
 angles.x = static_cast<float>(static_cast<double>(GetSwingEndPitch()) * progress + angles.x);
 AngleVectors( angles, &forward );
 if ( m_vecLastSwingDirection != vec3_origin )
 {
  const int last = 4 - ( m_nSwingFrames != 0 );
  for ( int i = m_nSwingFrames != 0; i <= last; ++i )
  {
   Vector direction = m_vecLastSwingDirection +
                      (forward - m_vecLastSwingDirection) * ((float)i / (float)last);
   VectorNormalize( direction );
#ifdef CLIENT_DLL
   if ( TraceSwing( direction ) ) m_swingTimer.Invalidate();
#else
   if ( player->TraceTerrorShove( this, direction ) ) m_swingTimer.Invalidate();
#endif
  }
  ++m_nSwingFrames;
 }
 m_vecLastSwingDirection = forward;
#ifndef CLIENT_DLL
 lagcompensation->FinishLagCompensation( player );
#endif
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
bool CTerrorWeapon::Reload()
{
    const bool result = BaseClass::Reload();
    if ( result )
        SuppressHelpingHand( 2.5f );
    return result;
}

void CTerrorWeapon::SuppressHelpingHand( float duration ) // S:3D0880
{
	if ( duration <= 0.0f ) duration = survivor_helping_hand_inhibit_duration.GetFloat();
	const double now = static_cast< double >( gpGlobals->curtime );
	if ( m_helpingHandSuppressionTimer.m_timestamp <= 0.0f || duration > m_helpingHandSuppressionTimer.m_timestamp.Get() - now )
	{
		const double expiry = now + duration;
		if ( m_helpingHandSuppressionTimer.m_timestamp.Get() != expiry ) m_helpingHandSuppressionTimer.m_timestamp.GetForModify() = static_cast< float >( expiry );
		m_helpingHandSuppressionTimer.m_duration = duration;
	}
}

void CTerrorWeapon::SendHelpingHandAnim( Activity activity )
{
    m_bSendingHelpingHandAnim = true;
    SendWeaponAnim( activity );
    m_bSendingHelpingHandAnim = false;
}

void CTerrorWeapon::SetHelpingHandState( int state )
{
    for ( ;; )
    {
        m_helpingHandState = state;
        m_helpingHandTimer.Start();
        CTerrorPlayer *owner = GetTerrorPlayerOwner();
        if ( !owner )
            return;
        if ( helping_hand_debug.GetInt() == owner->entindex() )
#ifdef CLIENT_DLL
            Msg( "client: new helping hand state: %d\n", state );
#else
            Msg( "server: new helping hand state: %d\n", state );
#endif
        if ( owner->GetActiveWeapon() != this )
            return;
        switch ( m_helpingHandState )
        {
        case 0:
            if ( m_helpingHandTarget.Get() )
                m_helpingHandTarget = NULL;
            return;
        case 2:
        {
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND ) );
            const double duration = GetViewModelSequenceDuration();
            if ( static_cast< double >( m_helpingHandExtendDuration.Get() ) != duration )
                m_helpingHandExtendDuration.SetDirect( static_cast< float >( duration ) );
            return;
        }
        case 3:
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_LOOP ) );
            return;
        case 4:
        {
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND ) );
            const double duration = GetViewModelSequenceDuration();
            if ( static_cast< double >( m_helpingHandExtendDuration.Get() ) != duration )
                m_helpingHandExtendDuration.SetDirect( static_cast< float >( duration ) );
            return;
        }
        case 5:
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_LOOP ) );
            return;
        case 6:
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_RETRACT ) );
            state = 0;
            continue;
        case 7:
            SendHelpingHandAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_RETRACT ) );
            state = 0;
            continue;
        default:
            return;
        }
    }
}

void CTerrorWeapon::UpdateHelpingHand()
{
    CTerrorPlayer *owner = GetTerrorPlayerOwner();
    if ( !owner )
        return;
    bool scan = true;
#ifdef CLIENT_DLL
    CUserCmd *cmd = owner->m_pCurrentCommand;
#else
    CUserCmd *cmd = owner->GetCurrentCommand();
#endif
    if ( cmd )
    {
        const int period = (int)( 0.5f / gpGlobals->interval_per_tick + 0.5f );
        if ( ( cmd->command_number + entindex() ) % period != 0 )
        {
            if ( m_helpingHandState != 2 && m_helpingHandState != 4 )
                return;
            scan = false;
        }
    }
    if ( m_bInReload )
        SuppressHelpingHand( 2.5f );
    if ( !CanDisplayHelpingHand() || owner->m_dragTarget.Get() )
        SuppressHelpingHand();
    bool haveTarget = false;
    CBaseEntity *target = NULL;
    if ( m_helpingHandSuppressionTimer.m_timestamp > 0.0f &&
         gpGlobals->curtime < m_helpingHandSuppressionTimer.m_timestamp )
    {
        if ( m_helpingHandTarget.Get() )
            m_helpingHandTarget = NULL;
        m_helpingHandTargetTimer.Invalidate();
    }
    else
    {
        if ( owner->m_reviveTarget.Get() )
        {
            haveTarget = true;
            target = owner->m_reviveTarget.Get();
        }
        else if ( owner->CanAttack() &&
                  ( ( gpGlobals->curtime > m_flNextPrimaryAttack &&
                      !( owner->m_nButtons & ( IN_ATTACK2 | IN_RELOAD ) ) ) || m_helpingHandState != 0 ) )
        {
            target = scan ? FindHelpingHandTarget() : m_helpingHandTarget.Get();
            haveTarget = target != NULL;
        }
        if ( ( m_helpingHandTarget.Get() != NULL ) == haveTarget )
            m_helpingHandTargetTimer.Start();
        if ( haveTarget )
        {
            const bool player = target->IsPlayer();
            switch ( m_helpingHandState )
            {
            case 0:
                if ( owner->m_reviveTarget.Get() ||
                     ( m_helpingHandTargetTimer.HasStarted() && m_helpingHandTargetTimer.GetElapsedTime() < 0.5f ) )
                    SetHelpingHandState( player ? 4 : 2 );
                else
                    SetHelpingHandState( 1 );
                break;
            case 1:
                if ( m_helpingHandTimer.GetElapsedTime() > 0.5f )
                    SetHelpingHandState( player ? 4 : 2 );
                break;
            case 2:
                if ( m_helpingHandTimer.GetElapsedTime() > m_helpingHandExtendDuration )
                    SetHelpingHandState( 3 );
                break;
            case 3:
                if ( player )
                    SetHelpingHandState( 4 );
                break;
            case 4:
                if ( m_helpingHandTimer.GetElapsedTime() > m_helpingHandExtendDuration )
                    SetHelpingHandState( 5 );
                break;
            case 5:
                if ( !player )
                    SetHelpingHandState( 2 );
                break;
            default:
                break;
            }
            if ( m_helpingHandTarget.Get() != target )
                m_helpingHandTarget = target;
            return;
        }
    }
    switch ( m_helpingHandState )
    {
    case 1: SetHelpingHandState( 0 ); break;
    case 2: case 3: SetHelpingHandState( 6 ); break;
    case 4: case 5: SetHelpingHandState( 7 ); break;
    default: break;
    }
}


CBaseEntity *CTerrorWeapon::FindHelpingHandTarget()
{
    CTerrorPlayer *owner = GetTerrorPlayerOwner();
    if ( ( owner->GetTeamNumber() != TEAM_SURVIVOR && owner->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
         owner->IsIncapacitated() || owner->m_reviveTarget.Get() )
        return NULL;
    g_bDebugPlayerUse = true;
    CBaseEntity *target = owner->FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
    g_bDebugPlayerUse = false;
    if ( !target )
        return NULL;
    if ( target->IsPlayer() )
    {
        CTerrorPlayer *survivor = ToTerrorPlayer( target );
        if ( survivor->IsHangingFromLedge() )
            return survivor;
        if ( ( survivor->GetTeamNumber() != TEAM_SURVIVOR && survivor->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
             !survivor->CanBeRevived() || survivor->m_reviveOwner.Get() )
            return NULL;
        Vector eye, forward;
        owner->EyePositionAndVectors( &eye, &forward, NULL, NULL );
        CTraceFilterSimpleListNoGhosts filter( COLLISION_GROUP_PLAYER );
        filter.AddEntityToIgnore( owner );
        trace_t tr;
        UTIL_TraceLine( eye, eye + forward * 96.0f, 0x0200608B, &filter, &tr );
        return tr.fraction > 0.0f && !tr.startsolid && tr.m_pEnt == survivor ? survivor : NULL;
    }
    CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( target );
    if ( weapon )
    {
        const int glow = GetTerrorWeaponGlowType( owner, weapon->GetWeaponID(), weapon->Clip1(),
            weapon->GetExtraPrimaryAmmo(), NULL, weapon->HasAnyUpgrades() );
        return glow == 2 || glow == 3 ? weapon : NULL;
    }
#ifdef CLIENT_DLL
    C_WeaponSpawn *spawn = dynamic_cast< C_WeaponSpawn * >( target );
#else
    CWeaponSpawn *spawn = dynamic_cast< CWeaponSpawn * >( target );
#endif
    if ( spawn )
    {
        const int id = spawn->GetWeaponID();
        int maxReserve = -1;
        const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( id );
        if ( info && GetAmmoDef() )
            maxReserve = GetAmmoDef()->MaxCarry( info->iAmmoType, owner );
        if ( id != 0 || ( target->GetUseType( owner ) != 4 && target->GetUseType( owner ) != 0 ) )
        {
            const int glow = GetTerrorWeaponGlowType( owner, id, -1, maxReserve, NULL, false );
            if ( glow == 2 || glow == 3 )
                return spawn;
        }
        return NULL;
    }
    CPropHealthCabinet *cabinet = dynamic_cast< CPropHealthCabinet * >( target );
    if ( cabinet && ( cabinet->ObjectCaps() & FCAP_IMPULSE_USE ) )
        return cabinet;
    CBaseEntity *redirect = target->GetUseRedirectEntity();
    if ( !redirect || redirect == target )
        return target->ObjectCaps() & 0x0800 ? target : NULL;
    return target;
}

bool CTerrorWeapon::HandlePrimaryAttackUse()
{
    if ( !allow_weapon_fire_to_use.GetBool() )
        return false;
    CTerrorPlayer *owner = GetTerrorPlayerOwner();
    if ( !owner )
    {
        SetHelpingHandState( 0 );
        return false;
    }
    if ( owner->IsBot() )
        return false;
    if ( !m_releasedFireButton )
        return true;
    if ( m_helpingHandState == 0 )
        return owner->m_reviveTarget.Get() != NULL;
    if ( GetTerrorWpnData().m_WeaponType == TERROR_WEAPONTYPE_CARRIED_PROP )
        return false;
    CTerrorPlayer *targetPlayer = ToTerrorPlayer( m_helpingHandTarget.Get() );
    if ( targetPlayer &&
         ( targetPlayer->GetTeamNumber() == TEAM_SURVIVOR || targetPlayer->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
         GetWeaponID() == TERROR_WEAPON_FIRST_AID_KIT && !targetPlayer->IsIncapacitated() &&
         !targetPlayer->IsUseActionTarget( 1 ) && !targetPlayer->IsPerformingSelfUseAction( 1 ) &&
         targetPlayer->GetHealth() < 99 )
        return false;
    if ( !m_isHoldingFireButton )
    {
        m_flNextSecondaryAttack = gpGlobals->curtime + 0.5f;
        m_flNextPrimaryAttack = m_flNextSecondaryAttack;
        m_releasedFireButton = false;
#ifndef CLIENT_DLL
        if ( targetPlayer )
        {
            targetPlayer->OnPlayerUse( owner, owner, USE_ON, 0.0f );
            return true;
        }
        CBaseEntity *target = m_helpingHandTarget.Get();
        if ( target )
        {
            variant_t empty;
            target->AcceptInput( "Use", owner, owner, empty, 3 );
            owner->OnPlayerUsed( target, USE_TOGGLE );
        }
#endif
    }
    return true;
}

int GetTerrorWeaponGlowType( CBaseEntity *pUser, int weaponID, int clip, int maxReserve, int *pOwnedWeaponID, bool forceCanUse )
{
	if ( !pUser || !pUser->IsPlayer() )
	{
		if ( pOwnedWeaponID )
			*pOwnedWeaponID = 0;
		return 2;
	}
	CTerrorPlayer *pPlayer = static_cast< CTerrorPlayer * >( pUser );
 if ( !pPlayer->IsWeaponAllowedForTeam( weaponID ) ) return 0;
 if ( weaponID != TERROR_WEAPON_AMMO )
 {
  const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( weaponID );
  if ( !pInfo ) return 2;
  int result = 2;
  CTerrorWeapon *pOwned = dynamic_cast< CTerrorWeapon * >( pPlayer->Weapon_GetSlot( pInfo->iSlot ) );
  int ownedID = 0;
  if ( pOwned )
  {
   ownedID = pOwned->GetWeaponID();
   if ( weaponID != pOwned->GetWeaponID() )
   {
    if ( pInfo->iSlot == 0 )
    {
     const CTerrorWeaponInfo *pOwnedInfo = GetTerrorWeaponInfo( pOwned->GetWeaponID() );
     if ( pOwnedInfo )
     {
      if ( pOwnedInfo->m_iTier > pInfo->m_iTier ) result = 1;
      else if ( pOwnedInfo->m_iTier < pInfo->m_iTier ) result = 3;
     }
    }
   }
   else if ( !( weaponID == TERROR_WEAPON_PISTOL && !pOwned->IsDualWielding() ) && weaponID != TERROR_WEAPON_MELEE && weaponID != TERROR_WEAPON_CHAINSAW && !forceCanUse )
   {
    const CTerrorWeaponInfo *pOwnedInfo = GetTerrorWeaponInfo( ownedID );
    if ( !pOwnedInfo || pPlayer->GetAmmoCount( pOwnedInfo->iAmmoType ) + pOwned->Clip1() >= maxReserve + clip ) result = 0;
   }
  }
  if ( pOwnedWeaponID ) *pOwnedWeaponID = ownedID;
  return result;
 }
 CTerrorWeapon *pPrimary = dynamic_cast< CTerrorWeapon * >( pPlayer->Weapon_GetSlot( 0 ) );
 if ( pPrimary && pPrimary->UsesPrimaryAmmo() )
 {
  const int primaryID = pPrimary->GetWeaponID();
  if ( primaryID != TERROR_WEAPON_GRENADE_LAUNCHER && primaryID != TERROR_WEAPON_RIFLE_M60 )
  {
   const CTerrorWeaponInfo *pInfo = GetTerrorWeaponInfo( primaryID );
   if ( pInfo )
   {
    const int totalCapacity = GetAmmoDef()->MaxCarry( pInfo->iAmmoType, pPlayer ) + pPrimary->GetMaxClip1();
    int normalClip = pPrimary->Clip1();
    if ( pPrimary->m_nUpgradedPrimaryAmmoLoaded ) normalClip -= pPrimary->m_nUpgradedPrimaryAmmoLoaded;
    if ( pPlayer->GetAmmoCount( pInfo->iAmmoType ) + normalClip < totalCapacity ) return 2;
   }
  }
 }
 return 0;
}

Activity CTerrorWeapon::TranslateViewModelLayerActivity( Activity activity )
{
    switch ( LookupTerrorOriginalActivity( activity ) )
    {
    case TERROR_ORIGINAL_ACT_VM_DEPLOY:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_DEPLOY_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_LAYER );
    case TERROR_ORIGINAL_ACT_VM_THROW:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_THROW_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PULLPIN:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PULLPIN_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_SHOOT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_SECONDARYATTACK:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_SECONDARYATTACK_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PICKUP:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PICKUP_LAYER );
    case TERROR_ORIGINAL_ACT_VM_VOMIT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_VOMIT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_COUGH:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_COUGH_LAYER );
    case TERROR_ORIGINAL_ACT_VM_LUNGE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_LUNGE_PUSH:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_PUSH_LAYER );
    case TERROR_ORIGINAL_ACT_VM_LUNGE_OFFWALL:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_OFFWALL_LAYER );
    case TERROR_ORIGINAL_ACT_VM_LUNGE_POUNCE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_POUNCE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_POUNCE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_POUNCE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_TONGUE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_TONGUE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_BITE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_BITE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_LOOP:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_LOOP_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_RETRACT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_RETRACT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_LOOP:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_LOOP_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_RETRACT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_RETRACT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_MELEE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_MELEE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PICKUP_CLIPIN:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PICKUP_CLIPIN_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PICKUP_CHARGING:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PICKUP_CHARGING_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PICKUP_FASSIST:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_PICKUP_FASSIST_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_CLIPOUT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_CLIPOUT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_CLIPIN:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_CLIPIN_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPOUT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPOUT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPIN:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPIN_LAYER );
    case TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPIN2:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_RELOAD_EMPTY_CLIPIN2_LAYER );
    default:
        return activity;
    }
}

Activity CTerrorWeapon::TranslateViewModelActivity( Activity activity )
{
    switch ( LookupTerrorOriginalActivity( activity ) )
    {
    case TERROR_ORIGINAL_ACT_VM_DEPLOY:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_DEPLOY_LAYER );
    case TERROR_ORIGINAL_ACT_VM_PRIMARYATTACK:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_SHOOT_RIFLE_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_EXTEND_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_LOOP:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_LOOP_LAYER );
    case TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_RETRACT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_ITEMPICKUP_RETRACT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_EXTEND_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_LOOP:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_LOOP_LAYER );
    case TERROR_ORIGINAL_ACT_VM_HELPINGHAND_RETRACT:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_HELPINGHAND_RETRACT_LAYER );
    case TERROR_ORIGINAL_ACT_VM_CANCEL:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_CANCEL_LAYER );
    case TERROR_ORIGINAL_ACT_VM_MELEE:
        return TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_MELEE_LAYER );
    default:
        return activity;
    }
}

int CTerrorWeapon::GetViewModelLayer( Activity activity )
{
    return activity == TranslateViewModelActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_MELEE ) ) ? 3 : 0;
}

#ifndef CLIENT_DLL
CBaseEntity *CTerrorWeapon::GetPreviousOwner() const
{
    if ( !( m_ownerTransferTimer.m_timestamp > 0.0f ) ||
         gpGlobals->curtime >= m_ownerTransferTimer.m_timestamp )
        return NULL;
    return m_hPreviousOwner.Get();
}

CBaseEntity *CTerrorWeapon::GetGiveTarget() const
{
    if ( !( m_ownerTransferTimer.m_timestamp > 0.0f ) ||
         gpGlobals->curtime >= m_ownerTransferTimer.m_timestamp )
        return NULL;
    return m_hGiveTarget.Get();
}

void CTerrorWeapon::GiveToPlayer( CTerrorPlayer *giver, CTerrorPlayer *recipient )
{
    m_hPreviousOwner = giver;
    m_hGiveTarget = recipient;
    m_ownerTransferTimer.Start( 5.0f );
    SetContextThink( &CTerrorWeapon::GiveThink, gpGlobals->curtime + 0.5f, "Weapon_GiveThink" );
}

bool CTerrorWeapon::CanBeGivenTo( CTerrorPlayer *recipient )
{
    if ( !recipient ||
         ( recipient->GetTeamNumber() != TEAM_SURVIVOR && recipient->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
         !recipient->IsAlive() )
        return false;
    if ( GetWeaponID() == TERROR_WEAPON_PISTOL )
    {
        CTerrorWeapon *pistol = NULL;
        for ( int i = 0; i < MAX_WEAPONS; ++i )
        {
            CTerrorWeapon *candidate = dynamic_cast< CTerrorWeapon * >( recipient->GetWeapon( i ) );
            if ( candidate && candidate->GetWeaponID() == TERROR_WEAPON_PISTOL )
            {
                pistol = candidate;
                break;
            }
        }
        if ( !pistol || pistol->IsDualWielding() )
            return false;
    }
    else
    {
        if ( recipient->Weapon_GetSlot( GetSlot() ) && !recipient->IsBot() )
            return false;
        if ( !recipient->Weapon_CanUse( this ) )
            return false;
    }
    CTerrorPlayer *giver = ToTerrorPlayer( GetPreviousOwner() );
    return giver && giver != recipient && m_hGiveTarget.Get() == recipient;
}

void CTerrorWeapon::GiveThink()
{
    CTerrorPlayer *recipient = ToTerrorPlayer( m_hGiveTarget.Get() );
    if ( !recipient || GetOwner() || !CanBeGivenTo( recipient ) )
        return;
    if ( recipient->BumpWeapon( this ) )
    {
        GiveTo( recipient );
        recipient->OnGivenWeapon( this );
    }
    else if ( GetPreviousOwner() )
    {
        CTerrorPlayer *giver = ToTerrorPlayer( GetPreviousOwner() );
        if ( giver->BumpWeapon( this ) )
            GiveTo( GetPreviousOwner() );
    }
}

void CTerrorWeapon::DefaultTouch( CBaseEntity *other )
{
    if ( other && other->IsPlayer() )
    {
        const bool notSolid = IsSolidFlagSet( FSOLID_NOT_SOLID );
        CBaseCombatCharacter *owner = GetOwner();
        if ( notSolid || owner == other )
        {
            DevWarning( "Touching our own weapons\n" );
        }
        else if ( CanBeGivenTo( static_cast< CTerrorPlayer * >( other ) ) )
        {
            // Native103ED730 directly calls1004B4E0, bypassing CS DefaultTouch.
            CBaseCombatWeapon::DefaultTouch( other );
            if ( GetTerrorPlayerOwner() == other )
            {
                CTerrorPlayer *giver = ToTerrorPlayer( GetPreviousOwner() );
                if ( giver && giver != other && m_hGiveTarget.Get() && other == m_hGiveTarget.Get() )
                    static_cast< CTerrorPlayer * >( other )->OnGivenWeapon( this );
            }
        }
    }
}

void CTerrorWeapon::Drop( const Vector &velocity )
{
    CBaseCombatCharacter *owner = GetOwner();
    m_hPreviousOwner = owner && owner->IsPlayer() ? owner : NULL;
    m_hGiveTarget = NULL;
    m_ownerTransferTimer.Start( 5.0f );
    m_DroppedByInfectedGender = 0;
    BaseClass::Drop( velocity );
    SetTouch( &CTerrorWeapon::DefaultTouch );
}

#endif
bool CTerrorWeapon::CanBeUsedBy( CTerrorPlayer *player )
{
    if ( !player ||
         ( player->IsIncapacitated() && GetTerrorWpnData().m_WeaponType != TERROR_WEAPONTYPE_PISTOL ) ||
         ( player->IsIncapacitated() && !player->m_isHangingFromLedge && !player->m_isHangingFromTongue &&
           ( player->m_nButtons & IN_FORWARD ) && survivor_allow_crawling.GetBool() ) ||
         player->m_isHangingFromLedge || player->GetMoveType() == MOVETYPE_LADDER ||
         player->m_isHangingFromTongue ||
         ( player->m_tongueOwner.Get() && !player->m_isHangingFromTongue &&
           static_cast< double >( gpGlobals->curtime ) - static_cast< double >( player->m_tongueVictimTimer.m_timestamp.Get() ) > 1.0 ) ||
         player->GetUseEntity() || player->m_pushEntity.Get() ||
         player->m_pounceAttacker.Get() || player->m_pummelAttacker.Get() ||
         player->m_jockeyAttacker.Get() || player->m_carryAttacker.Get() ||
         ( player->IsKnockedDown() && player->m_knockdownReason == 2 ) )
        return false;
    switch ( LookupTerrorOriginalActivity( player->GetTerrorActivity() ) )
    {
    case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH:
    case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TANKPUNCH:
    case TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND:
    case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_CHARGER:
    case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_CHARGERHIT:
    case TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW:
    case TERROR_ORIGINAL_ACT_TERROR_SLAMMED_WALL:
    case TERROR_ORIGINAL_ACT_TERROR_SLAMMED_GROUND:
    case TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND:
        return false;
    default:
        break;
    }
    return !( GetSlot() == 0 && UsesPrimaryAmmo() && !HasPrimaryAmmo() );
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
// S3EF800: preserve the duration spill, f32 quotient, and wide incap product.
double CTerrorWeapon::GetReloadAnimationScale()
{
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return BaseClass::GetReloadAnimationScale();
	const CTerrorWeaponInfo &data = GetTerrorWpnData();
	const double duration = SequenceDuration();
	const float duration32 = static_cast< float >( duration );
	if ( duration == 0.0 )
		return 1.0;
	float requested = IsDualWielding() ? data.m_flDualReloadDuration : data.m_flReloadDuration;
	if ( requested <= 0.0f )
		requested = duration32;
	const float ratio = requested / duration32;
	if ( static_cast< CTerrorPlayer * >( owner )->IsIncapacitated() )
		return static_cast< double >( survivor_incapacitated_reload_multiplier.GetFloat() ) * ratio;
	return static_cast< double >( ratio );
}

// S3EF900: the two operands spill f32, but the final FDIV is not spilled.
double CTerrorWeapon::GetDeployAnimationScale()
{
	CBaseCombatCharacter *owner = GetOwner();
	if ( !owner || !owner->IsPlayer() )
		return BaseClass::GetDeployAnimationScale();
	const CTerrorWeaponInfo &data = GetTerrorWpnData();
	const double duration = SequenceDuration();
	const float duration32 = static_cast< float >( duration );
	if ( duration == 0.0 )
		return 1.0;
	float requested = IsDualWielding() ? data.m_flDualDeployDuration : data.m_flDeployDuration;
	if ( requested <= 0.0f )
		requested = duration32;
	return static_cast< double >( requested ) / duration32;
}

#pragma float_control( pop )

#ifdef CLIENT_DLL
// C30F1E0: the common model producer precedes Gun's six attachment caches.
CStudioHdr *CTerrorWeapon::OnNewModel()
{
	CStudioHdr *studio = BaseClass::OnNewModel();
	m_iLaserSightAttachment = LookupAttachment( "flashlight" );
	return studio;
}

bool CTerrorWeapon::Simulate()
{
	BaseClass::Simulate(); // Genuine BaseAnimating C03B010, not a Think callback.
	return true; // C30F1D0.
}
#endif

#ifdef CLIENT_DLL
#pragma float_control( precise, on, push )
void CTerrorWeapon::OverrideMouseInput( float *x, float *y )
{
	CTerrorPlayer *owner = GetTerrorPlayerOwner();
	if ( !owner || !owner->IsPlayer() || owner->m_lifeState != LIFE_ALIVE || !owner->m_isIncapacitated )
		return;
	if ( static_cast< double >( gpGlobals->curtime ) >= m_incapacitatedDizzyTimer.m_timestamp.Get() )
	{
		const float duration = survivor_incapacitated_dizzy_timer.GetFloat();
		const double deadline = static_cast< double >( gpGlobals->curtime ) + static_cast< double >( duration );
		const float storedDeadline = static_cast< float >( deadline );
		if ( static_cast< double >( m_incapacitatedDizzyTimer.m_timestamp.Get() ) != deadline )
			m_incapacitatedDizzyTimer.m_timestamp.GetForModify() = storedDeadline;
		if ( m_incapacitatedDizzyTimer.m_duration.Get() != duration )
			m_incapacitatedDizzyTimer.m_duration.GetForModify() = duration;
		float severity = survivor_incapacitated_dizzy_severity.GetFloat();
		m_incapacitatedDizzyMouseX = RandomFloat( severity * -1.0f, severity );
		severity = survivor_incapacitated_dizzy_severity.GetFloat();
		m_incapacitatedDizzyMouseY = RandomFloat( severity * -1.0f, severity );
	}
	*x = m_incapacitatedDizzyMouseX + *x;
	*y = m_incapacitatedDizzyMouseY + *y;
}
#pragma float_control( pop )
#endif

void CTerrorWeapon::OnOwnerStunned( float duration )
{
	CancelSwing();
}

#ifdef CLIENT_DLL
bool CTerrorWeapon::IsAllowedToSwitch()
{
	CBaseCombatCharacter *owner = GetOwner();
	CTerrorPlayer *player = owner && owner->IsPlayer() ? static_cast< CTerrorPlayer * >( owner ) : NULL;
	return CanBeUsedBy( player ) && BaseClass::IsAllowedToSwitch();
}
#endif

// S3F1EC0/C312020: native ADD wraps before the signed [0,10] clamp.
void CTerrorWeapon::MakeWeaponBloody()
{
	const uint32 value = static_cast< uint32 >( m_iBloodyWeaponLevel.Get() ) + 2U;
	m_iBloodyWeaponLevel = ( value & 0x80000000U ) ? 0 : ( value > 10U ? 10 : static_cast< int >( value ) );
}

#ifdef CLIENT_DLL
double CTerrorWeapon::GetBloodyWeaponBlend() const
{
	return UTIL_IsLowViolence() ? 0.0 : static_cast< double >( m_iBloodyWeaponLevel.Get() ) * static_cast< double >( 0.1f );
}
#endif

const char *CTerrorWeapon::GetResponseRulesName() const
{
	return GetCSWpnData().m_szResponseRulesName;
}
