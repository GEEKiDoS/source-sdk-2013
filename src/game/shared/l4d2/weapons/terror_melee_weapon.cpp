//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#include "c_te_effect_dispatch.h"
#include "prediction.h"
#include "particles_new.h"
#include "particle_property.h"
#include "baseviewmodel_shared.h"
#include "c_rumble.h"
#include "datacache/imdlcache.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_lagcompensation.h"
#include "ilagcompensationmanager.h"
#include "l4d2/infected/common/infected.h"
#include "NextBot/NextBot.h"
#include "NextBot/NextBotManager.h"
#include "usermessages.h"
#include "te_effect_dispatch.h"
#include "networkstringtable_gamedll.h"
#include "l4d2/infected/special/ability_tongue.h"
#endif
#include "l4d2/weapons/terror_melee_weapon.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "activitylist.h"
#include "effect_dispatch_data.h"
#include "particle_parse.h"
#include "debugoverlay_shared.h"
#include "rumble_shared.h"
#include <float.h>
#ifdef L4D2
#include "precache_register.h"
#endif
#include "l4d2/terror_trace_filter.h"
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( TerrorMeleeWeapon, DT_TerrorMeleeWeapon )
BEGIN_NETWORK_TABLE( CTerrorMeleeWeapon, DT_TerrorMeleeWeapon )
#ifdef CLIENT_DLL
	RecvPropFloat( RECVINFO( m_flLastAttackTime ) ),
	RecvPropInt( RECVINFO( m_hMeleeWeaponInfo ) ),
	RecvPropInt( RECVINFO( m_iNextPrimaryAttackIndex ) ),
	RecvPropInt( RECVINFO( m_iNextStrongAttackIndex ) ),
	RecvPropInt( RECVINFO( m_iNextSecondaryAttackIndex ) ),
	RecvPropBool( RECVINFO( m_bInMeleeSwing ) ),
	RecvPropDataTable( RECVINFO_DT( m_meleeSwingTimer ), 0, &REFERENCE_RECV_TABLE( DT_CountdownTimer ) ),
#else
	SendPropFloat( SENDINFO( m_flLastAttackTime ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_hMeleeWeaponInfo ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iNextPrimaryAttackIndex ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iNextStrongAttackIndex ), 4, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_iNextSecondaryAttackIndex ), 4, SPROP_UNSIGNED ),
	SendPropBool( SENDINFO( m_bInMeleeSwing ) ),
	SendPropDataTable( SENDINFO_DT( m_meleeSwingTimer ), &REFERENCE_SEND_TABLE( DT_CountdownTimer ) ),
#endif
END_NETWORK_TABLE()
BEGIN_PREDICTION_DATA( CTerrorMeleeWeapon )
#ifdef CLIENT_DLL
	DEFINE_PRED_FIELD( m_flLastAttackTime, FIELD_FLOAT, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iNextPrimaryAttackIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iNextStrongAttackIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_iNextSecondaryAttackIndex, FIELD_INTEGER, FTYPEDESC_INSENDTABLE ),
	DEFINE_PRED_FIELD( m_bInMeleeSwing, FIELD_BOOLEAN, FTYPEDESC_INSENDTABLE ),
	// Native6F43E8 flags6; the generic SDK embedded macro supplies SAVE2 only.
#ifndef NO_ENTITY_PREDICTION
	{ FIELD_EMBEDDED, "m_meleeSwingTimer", { (int)_hacky_datamap_offsetof( classNameTypedef, m_meleeSwingTimer ), 0 },
		1, FTYPEDESC_SAVE | FTYPEDESC_KEY, NULL, NULL, NULL, &CountdownTimer::m_PredMap },
#else
	DEFINE_FIELD_NULL,
#endif // NO_ENTITY_PREDICTION
#endif
END_PREDICTION_DATA()
#ifndef CLIENT_DLL
BEGIN_DATADESC( CTerrorMeleeWeapon )
	DEFINE_KEYFIELD( m_strMapSetScriptName, FIELD_STRING, "melee_script_name" ),
END_DATADESC()
#endif
LINK_ENTITY_TO_CLASS( weapon_melee, CTerrorMeleeWeapon );
#ifndef L4D2
PRECACHE_WEAPON_REGISTER( weapon_melee );
#endif

// Original registrations S:0x5A6570..0x5A6700. Reach is not a script key.
static ConVar melee_show_swing( "melee_show_swing", "0", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar melee_range( "melee_range", "70", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar melee_combo_reset_time( "melee_combo_reset_time", "1.2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
, "Seconds after a swing until we reset the combo activities" );
#ifndef CLIENT_DLL
static ConVar melee_force_scalar( "melee_force_scalar", "20", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar melee_force_scalar_combat_character( "melee_force_scalar_combat_character", "5", 
#if defined( GAME_DLL )
	0x00006004
#else
	FCVAR_CHEAT | FCVAR_REPLICATED
#endif
 );
static ConVar melee_collateral_shove_count( "melee_collateral_shove_count", "16", 
#if defined( GAME_DLL )
	0x00004004
#else
	FCVAR_CHEAT
#endif
 );
#endif

static Activity MeleeActivity( const char *pszName )
{
	return (Activity)ActivityList_IndexForName( pszName );
}

CTerrorMeleeWeapon::CTerrorMeleeWeapon()
{
	m_hMeleeWeaponInfo = INVALID_MELEE_WEAPON_INFO_HANDLE;
	m_flLastAttackTime = 0.0f;
#ifndef CLIENT_DLL
	m_strMapSetScriptName = NULL_STRING;
#endif
}

void CTerrorMeleeWeapon::SetMeleeWeaponInfo( MeleeWeaponInfoHandle_t hInfo )
{
	m_hMeleeWeaponInfo = hInfo;
}

#ifndef CLIENT_DLL
void CTerrorMeleeWeapon::SetMeleeScriptName( const char *pszName )
{
	m_strMapSetScriptName = AllocPooledString( pszName );
}
#endif

const CMeleeWeaponInfo *CTerrorMeleeWeapon::GetMeleeInfo() const
{
	return MeleeWeaponInfoStore()->GetMeleeWeaponInfo( m_hMeleeWeaponInfo );
}

const MeleeAttackInfo_t *CTerrorMeleeWeapon::GetCurrentMeleeAttack() const
{
	// Explicit reload frees the old arrays without resetting held weapon state.
	return MeleeWeaponInfoStore()->IsValidAttackInfo( m_pCurrentAttack ) ? m_pCurrentAttack : NULL;
}

const char *CTerrorMeleeWeapon::GetMeleeWeaponScriptName() const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? pInfo->m_szName : "";
}

const char *CTerrorMeleeWeapon::GetResponseRulesName() const
{
	return GetMeleeWeaponScriptName();
}

bool CTerrorMeleeWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#ifndef CLIENT_DLL
	// S:0x3EA450 intentionally calls common Holster twice on a ladder
	// for a slot-five melee script. Do not turn that into one callback.
	if ( GetSlot() == 5 )
	{
		CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
		if ( pPlayer && pPlayer->GetMoveType() == MOVETYPE_LADDER )
		{
			BaseClass::Holster( pSwitchingTo );
			return BaseClass::Holster( pSwitchingTo );
		}
		if ( pPlayer )
			pPlayer->DropWeapon( this, true, NULL );
	}
#endif
	return BaseClass::Holster( pSwitchingTo );
}

void CTerrorMeleeWeapon::Precache()
{
	BaseClass::Precache();
#ifdef CLIENT_DLL
	PrecacheEffect( "bloodslash" );
#endif
#ifndef CLIENT_DLL
	g_pStringTableEffectDispatch->AddString( true, "bloodslash" );
#endif
}

void CTerrorMeleeWeapon::Spawn()
{
	Precache();
	m_iClip1 = WEAPON_NOCLIP;
#ifndef CLIENT_DLL
	if ( m_strMapSetScriptName != NULL_STRING )
	{
		const MeleeWeaponInfoHandle_t hInfo = MeleeWeaponInfoStore()->GetMeleeWeaponInfoIndex( STRING( m_strMapSetScriptName ) );
		if ( hInfo != INVALID_MELEE_WEAPON_INFO_HANDLE )
			SetMeleeWeaponInfo( hInfo );
	}
#endif
	m_iNextPrimaryAttackIndex = 0;
	m_iNextStrongAttackIndex = 0;
	m_iNextSecondaryAttackIndex = 0;
	m_pCurrentAttack = NULL;
	m_flMeleeSwingProgress = 0.0f;
	BaseClass::Spawn();
}

const char *CTerrorMeleeWeapon::GetViewModel( int viewmodelindex ) const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? pInfo->m_szViewModel : BaseClass::GetViewModel( 0 );
}

const char *CTerrorMeleeWeapon::GetWorldModel() const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? pInfo->m_szPlayerModel : BaseClass::GetViewModel( 0 );
}


const char *CTerrorMeleeWeapon::GetShootSound( int iIndex ) const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	if ( !pInfo )
		return "";
	return pInfo->m_szShootSounds[iIndex][0] ? pInfo->m_szShootSounds[iIndex] : BaseClass::GetShootSound( iIndex );
}

Activity CTerrorMeleeWeapon::GetDeployActivity()
{
	return ACT_VM_DRAW;
}

Activity CTerrorMeleeWeapon::TranslatePlayerActivity( Activity baseAct )
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	if ( !pInfo )
		return baseAct;
	static const char *s_pActivities[] =
	{
		"ACT_IDLE", "ACT_WALK", "ACT_RUN", "ACT_CROUCHIDLE", "ACT_WALK_CROUCH", "ACT_RUN_CROUCH",
		"ACT_IDLE_INJURED", "ACT_WALK_INJURED", "ACT_RUN_INJURED", "ACT_IDLE_CALM", "ACT_WALK_CALM", "ACT_RUN_CALM", "ACT_JUMP"
	};
	for ( int i = 0; i < ARRAYSIZE( s_pActivities ); ++i )
	{
		if ( baseAct == MeleeActivity( s_pActivities[i] ) )
		{
			return MeleeActivity( pInfo->m_szActivity[i] );
		}
	}
	return baseAct;
}

Activity CTerrorMeleeWeapon::GetPlayerAttackActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const bool bMoving = mainActivity != ACT_IDLE;
	if ( !m_pCurrentAttack )
		return BaseClass::GetPlayerAttackActivity( event, mainActivity );
	const MeleeAttackInfo_t *pAttack = GetCurrentMeleeAttack();
	if ( !pAttack )
		return ACT_INVALID;
	if ( !bMoving && pAttack->m_PlayerIdleActivity != ACT_INVALID )
		return pAttack->m_PlayerIdleActivity;
	return pAttack->m_PlayerActivity;
}

Activity CTerrorMeleeWeapon::GetPlayerReloadActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	return MeleeActivity( "ACT_RELOAD_RIFLE" );
}

Activity CTerrorMeleeWeapon::GetPlayerDeployActivity( TerrorPlayerAnimEvent event, Activity mainActivity )
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? MeleeActivity( pInfo->m_szActivity[MELEE_ACTIVITY_DEPLOY] ) : MeleeActivity( "ACT_DEPLOY_RIFLE" );
}

Activity CTerrorMeleeWeapon::GetMeleeShoveActivity() const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? MeleeActivity( pInfo->m_szActivity[MELEE_ACTIVITY_SHOVE] ) : ACT_INVALID;
}

#ifdef CLIENT_DLL
const CHudTexture *CTerrorMeleeWeapon::GetSpriteActive() const
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	return pInfo ? pInfo->m_pSpriteActive : NULL;
}
#endif

// S:0x3EAF30 / 0x3EB050. Strong indices are networked, but the original
// parser/selector never consumes the shipped strongattacks blocks.
Activity CTerrorMeleeWeapon::SelectMeleeAttack( bool bPrimary )
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	if ( !pInfo )
		return bPrimary ? ACT_VM_PRIMARYATTACK : ACT_VM_SECONDARYATTACK;
	const CUtlVector<MeleeAttackInfo_t> &attacks = bPrimary ? pInfo->m_PrimaryAttacks : pInfo->m_SecondaryAttacks;
	if ( attacks.Count() <= 0 )
		return bPrimary ? ACT_VM_PRIMARYATTACK : ACT_VM_SECONDARYATTACK;
	int nIndex = bPrimary ? m_iNextPrimaryAttackIndex.Get() : m_iNextSecondaryAttackIndex.Get();
	if ( bPrimary && melee_combo_reset_time.GetFloat() > 0.0f && gpGlobals->curtime - m_flLastAttackTime > melee_combo_reset_time.GetFloat() )
		nIndex = 0;
	if ( nIndex > attacks.Count() - 1 || nIndex > 7 )
		nIndex = 0;
	if ( nIndex < 0 )
		return ACT_INVALID; // Only a scripted out-of-array selection is skipped.
	m_pCurrentAttack = &attacks[nIndex];
	if ( bPrimary )
		m_iNextPrimaryAttackIndex = nIndex + 1;
	else
		m_iNextSecondaryAttackIndex = nIndex + 1;
	return attacks[nIndex].m_Activity;
}

void CTerrorMeleeWeapon::PrimaryAttack()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || pPlayer->m_tongueOwner.Get() )
		return;
	if ( !pPlayer->CanAttack() )
	{
		DevMsg( "melee attack failed - !player->CanAttack()\n" );
		return;
	}
	if ( !IsWeaponVisible() )
	{
		DevMsg( "melee attack failed - !IsWeaponVisible()\n" );
		return;
	}
	if ( HandlePrimaryAttackUse() )
		return;
	StartMeleeAttack( pPlayer, true );
	m_flLastAttackTime = gpGlobals->curtime;
}

bool CTerrorMeleeWeapon::CanUseSecondaryShove() const
{
	const CMeleeWeaponInfo *info = GetMeleeInfo();
	return info && info->m_SecondaryAttacks.Count() > 0;
}
void CTerrorMeleeWeapon::SecondaryAttack()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer || !CanUseSecondaryShove() || pPlayer->m_tongueOwner.Get() ||
		pPlayer->IsIncapacitated() || !pPlayer->CanAttack() )
		return;
	SetHelpingHandState( 0 );
	if ( !IsWeaponVisible() || !GetMeleeInfo() || !pPlayer->IsShoveAvailable() )
		return;
	SendWeaponAnim( SelectMeleeAttack( false ) );
	static ConVarRef z_gun_swing_interval( "z_gun_swing_interval" );
	static ConVarRef z_gun_swing_duration( "z_gun_swing_duration" );
	static ConVarRef z_gun_range( "z_gun_range" );
	Swing( z_gun_swing_interval.GetFloat(), z_gun_swing_duration.GetFloat(), z_gun_range.GetFloat() );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
void CTerrorMeleeWeapon::StartMeleeAttack( CTerrorPlayer *pPlayer, bool bPrimary )
{
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	if ( !pInfo )
		return;
	SendWeaponAnim( SelectMeleeAttack( bPrimary ) );
	pPlayer->DoTerrorAnimationEvent( bPrimary ? TERROR_ANIMEVENT_FIRE_PRIMARY : TERROR_ANIMEVENT_FIRE_PRIMARY_ALT, 0 );
	const double flSequenceDuration = GetViewModelSequenceDuration();
	if ( bPrimary )
	{
		const double primary = static_cast< double >( gpGlobals->curtime ) + flSequenceDuration;
		if ( static_cast< double >( m_flNextPrimaryAttack.Get() ) != primary )
			m_flNextPrimaryAttack.SetDirect( static_cast< float >( primary ) );
		const double secondary = static_cast< double >( gpGlobals->curtime ) + 0.7;
		if ( static_cast< double >( m_flNextSecondaryAttack.Get() ) != secondary )
			m_flNextSecondaryAttack.SetDirect( static_cast< float >( secondary ) );
	}
	else
	{
		const double secondary = static_cast< double >( gpGlobals->curtime ) + flSequenceDuration;
		if ( static_cast< double >( m_flNextSecondaryAttack.Get() ) != secondary )
			m_flNextSecondaryAttack.SetDirect( static_cast< float >( secondary ) );
		const float primary = m_flNextSecondaryAttack.Get();
		if ( m_flNextPrimaryAttack.Get() != primary )
			m_flNextPrimaryAttack.SetDirect( primary );
	}
	SetWeaponIdleTime( pInfo->m_flWeaponIdleTime );
	WeaponSound( MELEE_MISS );
	SuppressHelpingHand();
	const MeleeAttackInfo_t *pAttack = GetCurrentMeleeAttack();
	if ( pAttack )
		m_meleeSwingTimer.Start( pAttack->m_flDuration );
	m_bInMeleeSwing = true;
	m_meleeAffectedEntities.RemoveAll();
	m_vecLastMeleeDirection = vec3_origin;
	m_flMeleeSwingProgress = 0.0f;
	UpdateMeleeSwing();
#ifndef CLIENT_DLL
	pPlayer->SetNoiseLevel( GetTerrorWpnData().m_flNoiseFactor );
	pPlayer->DelayCalm( 2.0f );
	pPlayer->NoteTerrorWeaponFired();
#else
	RumbleEffect( RUMBLE_CROWBAR_SWING, 0, RUMBLE_FLAG_RESTART );
#endif
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#pragma float_control( precise, on, push )
void CTerrorMeleeWeapon::ItemPostFrame()
{
	BaseClass::ItemPostFrame();
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	if ( !pPlayer )
		return;
	if ( !( gpGlobals->curtime >= pPlayer->m_stunTimer.m_timestamp ) || pPlayer->m_pounceAttacker.Get() || pPlayer->m_pummelAttacker.Get() || pPlayer->m_jockeyAttacker.Get() )
	{
		m_bInMeleeSwing = false;
		return;
	}
	if ( m_bInMeleeSwing )
		UpdateMeleeSwing();
	if ( m_meleeSwingTimer.IsElapsed() )
		m_bInMeleeSwing = false;
}
#pragma float_control( pop )

static float MeleeDirectionYaw( MeleeAttackDirection_t direction )
{
	switch ( direction )
	{
	case MELEE_DIRECTION_E: case MELEE_DIRECTION_NE: case MELEE_DIRECTION_SE: return -45.0f;
	case MELEE_DIRECTION_W: case MELEE_DIRECTION_SW: case MELEE_DIRECTION_NW: return 45.0f;
	default: return 0.0f;
	}
}

static float MeleeDirectionPitch( MeleeAttackDirection_t direction )
{
	switch ( direction )
	{
	case MELEE_DIRECTION_N: case MELEE_DIRECTION_NE: case MELEE_DIRECTION_NW: return -45.0f;
	case MELEE_DIRECTION_S: case MELEE_DIRECTION_SE: case MELEE_DIRECTION_SW: return 45.0f;
	default: return 0.0f;
	}
}

// S:0x3EBDC0 / C:0x30E310. Both window endpoints are inclusive. There is
// no catch-up hit if a command steps completely over the contact window.
#pragma float_control( precise, on, push )
void CTerrorMeleeWeapon::UpdateMeleeSwing()
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	const MeleeAttackInfo_t *pAttack = GetCurrentMeleeAttack();
	if ( !pPlayer || !pAttack )
		return;
	const double elapsed = static_cast< double >( gpGlobals->curtime ) -
		static_cast< double >( m_meleeSwingTimer.m_timestamp.Get() ) +
		static_cast< double >( m_meleeSwingTimer.m_duration.Get() );
	const float flElapsed = static_cast< float >( elapsed );
	if ( static_cast< double >( pAttack->m_flStartTime ) > elapsed || flElapsed > pAttack->m_flEndTime )
		return;
#ifndef CLIENT_DLL
	StartTerrorLagCompensation( pPlayer, TERROR_LAG_COMPENSATE_BOUNDS, vec3_origin, vec3_angle, 0.0f );
#endif
	Vector forward = GetSwingDirection();
	QAngle angles;
	VectorAngles( forward, angles );
	m_flMeleeSwingProgress = clamp( ( flElapsed - pAttack->m_flStartTime ) / ( pAttack->m_flEndTime - pAttack->m_flStartTime ), 0.0f, 1.0f );
	const float flRemaining = 1.0f - m_flMeleeSwingProgress;
	angles.y = static_cast< float >( static_cast< double >( MeleeDirectionYaw( pAttack->m_StartDirection ) ) * flRemaining + angles.y );
	angles.y = static_cast< float >( static_cast< double >( MeleeDirectionYaw( pAttack->m_EndDirection ) ) * m_flMeleeSwingProgress + angles.y );
	angles.x = static_cast< float >( static_cast< double >( MeleeDirectionPitch( pAttack->m_StartDirection ) ) * flRemaining + angles.x );
	angles.x = static_cast< float >( static_cast< double >( MeleeDirectionPitch( pAttack->m_EndDirection ) ) * m_flMeleeSwingProgress + angles.x );
	AngleVectors( angles, &forward );
	bool bHit = false;
	if ( m_vecLastMeleeDirection != vec3_origin )
	{
		for ( int i = 0; i <= 3; ++i )
		{
			const float flFraction = (float)i * 0.33333334f;
			Vector direction = m_vecLastMeleeDirection + ( forward - m_vecLastMeleeDirection ) * flFraction;
			VectorNormalize( direction );
			if ( TraceMeleeSwing( direction ) )
				bHit = true;
		}
	}
	m_vecLastMeleeDirection = forward;
#ifndef CLIENT_DLL
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	if ( bHit && pInfo && pInfo->m_iRumbleEffect >= 0 )
		pPlayer->RumbleEffect( pInfo->m_iRumbleEffect, 0, RUMBLE_FLAG_RESTART );
	lagcompensation->FinishLagCompensation( pPlayer );
#endif
}
#pragma float_control( pop )

bool CTerrorMeleeWeapon::HasHitMeleeTarget( CBaseEntity *pTarget ) const
{
	return m_meleeAffectedEntities.Find( pTarget ) != m_meleeAffectedEntities.InvalidIndex();
}

bool CTerrorMeleeWeapon::CanHitMeleeTarget( CBaseEntity *pTarget ) const
{
	if ( pTarget->GetCollisionGroup() == COLLISION_GROUP_WEAPON )
		return false;
	if ( pTarget->IsPlayer() && ( pTarget->GetTeamNumber() == TEAM_SURVIVOR || pTarget->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		return m_flMeleeSwingProgress >= 0.40000001f && m_flMeleeSwingProgress <= 0.60000002f;
	return true;
}


#pragma float_control( precise, on, push )
bool CTerrorMeleeWeapon::TraceMeleeSwing( const Vector &direction )
{
	CTerrorPlayer *pPlayer = GetTerrorPlayerOwner();
	const CMeleeWeaponInfo *pInfo = GetMeleeInfo();
	const MeleeAttackInfo_t *pAttack = GetCurrentMeleeAttack();
	if ( !pPlayer || !pInfo || !pAttack )
		return false;
	const Vector start = pPlayer->Weapon_ShootPosition();
	Vector forward, right, up;
	pPlayer->EyeVectors( &forward, &right, &up );
	Vector normalizedDirection = direction;
	VectorNormalize( normalizedDirection );
	const Vector end = start + direction * ( DotProduct( normalizedDirection, forward ) * melee_range.GetFloat() );
	float flHull = 4.0f;
	const int startDirection = pAttack->m_StartDirection;
	const int endDirection = pAttack->m_EndDirection;
	if ( ( startDirection >= MELEE_DIRECTION_N && startDirection <= MELEE_DIRECTION_S ) ||
		( startDirection >= MELEE_DIRECTION_NE && startDirection <= MELEE_DIRECTION_NW ) ||
		( endDirection >= MELEE_DIRECTION_N && endDirection <= MELEE_DIRECTION_S ) ||
		( endDirection >= MELEE_DIRECTION_NE && endDirection <= MELEE_DIRECTION_NW ) )
		flHull = 16.0f;
	CTraceFilterSimpleListNoGhosts filter( COLLISION_GROUP_NONE );
	filter.AddEntityToIgnore( pPlayer );
#ifdef CLIENT_DLL
	filter.AddEntityToIgnore( C_BasePlayer::GetLocalPlayer() );
	MDLCACHE_CRITICAL_SECTION();
	CEffectData ragdoll;
	ragdoll.m_vStart = start;
	ragdoll.m_vOrigin = end;
	ragdoll.m_nDamageType = DMG_CLUB;
	DispatchEffect( "RagdollImpact", ragdoll );
#endif
	trace_t trace;
	pAttack = GetCurrentMeleeAttack();
	if ( !pAttack )
		return false;
	if ( pAttack->m_bPrimaryAttack )
		UTIL_TraceLine( start, end, MASK_SHOT | CONTENTS_GRATE, &filter, &trace );
	else
		UTIL_TraceHull( start, end, Vector( -flHull, -flHull, -flHull ), Vector( flHull, flHull, flHull ), MASK_SHOT | CONTENTS_GRATE, &filter, &trace );
#ifndef CLIENT_DLL
	if ( melee_show_swing.GetInt() )
	{
		const float flDebugHull = melee_show_swing.GetInt() <= 1 ? 0.5f : flHull;
		NDebugOverlay::SweptBox( start, end, Vector( -flDebugHull, -flDebugHull, -flDebugHull ), Vector( flDebugHull, flDebugHull, flDebugHull ), vec3_angle,
			255, trace.DidHit() ? 0 : 255, trace.DidHit() ? 0 : 255, 255, 3.0f );
	}
	if ( pInfo->m_iDamageFlags & DMG_SLASH )
	{
		// S:0x3EB2E0 passes this normalized world-position subtraction,
		// not the swing direction or the damage-force direction.
		Vector tongueDirection = direction - start;
		VectorNormalize( tongueDirection );
		TraceTerrorMeleeTongue( pPlayer, trace.startpos, trace.endpos, DMG_SLASH, tongueDirection );
	}
#endif
	CBaseEntity *pTarget = trace.m_pEnt;
	if ( !trace.DidHit() || !pTarget || !CanHitMeleeTarget( pTarget ) )
		return false;
	const bool bAlreadyHit = HasHitMeleeTarget( pTarget );
	pAttack = GetCurrentMeleeAttack();
	if ( !pAttack || pInfo != GetMeleeInfo() )
	{
		if ( !bAlreadyHit )
			m_meleeAffectedEntities.AddToTail( pTarget );
		return true;
	}
#ifndef CLIENT_DLL
	if ( !pAttack->m_bPrimaryAttack )
	{
		if ( !bAlreadyHit )
			OnSwingHit( trace, direction, true );
	}
	else
	{
		CTakeDamageInfo damage( this, pPlayer, this, vec3_origin, start, GetMeleeDamage( pTarget ), pInfo->m_iDamageFlags );
		if ( bAlreadyHit )
			damage.SetDamage( 0.0f );
		CBaseAnimating *pAnimating = pTarget->GetBaseAnimating();
		if ( pAnimating && pTarget->GetTeamNumber() == TEAM_INFECTED )
		{
			Ray_t ray;
			ray.Init( start, end );
			pAnimating->TestHitboxes( ray, MASK_SHOT | CONTENTS_GRATE, trace );
			if ( pInfo->m_iDamageFlags & ( DMG_SLASH | DMG_CLUB ) )
			{
				if ( pInfo->m_iDamageFlags & DMG_SLASH )
					damage.AddDamageType( (int)0x80000000u );
				if ( trace.hitgroup != 0 )
				{
					CSingleUserRecipientFilter splatter( pPlayer );
					splatter.MakeReliable();
					UserMessageBegin( splatter, pInfo->m_iDamageFlags & DMG_SLASH ? "MeleeSlashSplatter" : "MeleeClubSplatter" );
					WRITE_BYTE( 1 );
					MessageEnd();
				}
			}
		}
		pAttack = GetCurrentMeleeAttack();
		if ( pAttack )
		{
			const Vector &forceDirection = pAttack->m_vecForceDirection;
			Vector force = ( forward * forceDirection.x + right * forceDirection.y + up * forceDirection.z ) * melee_force_scalar.GetFloat();
			const Vector attackDirection = right * forceDirection.y;
			damage.SetDamageDirection( attackDirection );
			if ( pTarget->MyCombatCharacterPointer() )
				force *= melee_force_scalar_combat_character.GetFloat();
			if ( pTarget->GetNonShadowMass() > 400.0f || ( !TerrorGameRules()->IsRoundInProgress() && TerrorGameRules()->IsSurvivalMode() ) )
			{
				damage.AddDamageType( 0x800 );
				force *= 0.0f;
			}
			damage.SetDamageForce( force );
			ClearMultiDamage();
			pTarget->DispatchTraceAttack( damage, attackDirection, &trace );
			ApplyMultiDamage();
		}
	}
#else
	if ( !bAlreadyHit && pAttack->m_bPrimaryAttack )
		pTarget->ImpactTrace( &trace, pInfo->m_iDamageFlags, NULL );
#endif
	if ( !bAlreadyHit && pInfo == GetMeleeInfo() && ( pAttack = GetCurrentMeleeAttack() ) != NULL && pAttack->m_bPrimaryAttack )
	{
		if ( pTarget->MyCombatCharacterPointer() )
		{
			WeaponSound( MELEE_HIT );
#ifndef CLIENT_DLL
			if ( pInfo->m_iDamageFlags & DMG_SLASH )
			{
				CEffectData slash;
				slash.m_vOrigin = trace.endpos;
				slash.m_nEntIndex = entindex();
				DispatchEffect( "bloodslash", slash );
			}
			if ( pInfo->m_iDamageFlags & ( DMG_SLASH | DMG_CLUB ) )
			{
				if ( pTarget->GetTeamNumber() == TEAM_INFECTED )
				{
					pPlayer->AddTerrorSplatter( 1, 0.0f, ( pInfo->m_iDamageFlags & DMG_SLASH ) ? 1 : 2 );
					Infected *pCommon = dynamic_cast<Infected *>( pTarget );
					if ( pCommon && pCommon->GetGender() == 15 )
						pCommon->OnShoved( pPlayer );
				}
				ShoveNearbyCommonInfected( pTarget );
			}
			MakeWeaponBloody();
#else
			if ( pInfo->m_iDamageFlags & DMG_SLASH )
			{
				C_BaseViewModel *pViewModel = pPlayer->GetViewModel();
				if ( pViewModel )
				{
					CNewParticleEffect *pBlood = pViewModel->ParticleProp()->Create( "blood_melee_slash_FP", PATTACH_POINT_FOLLOW, "attach_blade" );
					if ( pBlood )
						pBlood->SetControlPoint( 1, trace.endpos );
				}
			}
			MakeWeaponBloody();
#endif
		}
		else
		{
			WeaponSound( MELEE_HIT_WORLD );
		}
	}
	if ( !bAlreadyHit )
		m_meleeAffectedEntities.AddToTail( pTarget );
	return true;
}
#pragma float_control( pop )

#ifndef CLIENT_DLL
#pragma float_control( precise, on, push )
double CTerrorMeleeWeapon::GetMeleeDamage( CBaseEntity *pTarget ) const
{
	CBaseCombatCharacter *pCharacter = pTarget->MyCombatCharacterPointer();
	if ( !pCharacter )
		return 175.0f;
	// Original CBaseCombatCharacter returns9 (S:0x043990), common returns0
	// (0x2993E0), and the real player getter reads m_zombieClass (0x325530).
	const int nZombieClass = pCharacter->GetZombieClass();
	switch ( nZombieClass )
	{
	case 0: case 1: case 2: case 3: case 4: case 5:
		return static_cast< double >( pCharacter->GetHealth() );
	case 6:
		return static_cast< double >( pCharacter->GetMaxHealth() ) * static_cast< double >( 0.64999998f );
	case 7:
		return static_cast< double >( pCharacter->GetMaxHealth() ) * 0.25;
	case 8:
		return static_cast< double >( pCharacter->GetMaxHealth() ) * static_cast< double >( 0.050000001f );
	default:
		return GetMeleeInfo()->m_flDamage;
	}
}
#pragma float_control( pop )

class CollectMeleeCollateralCommons
{
public:
	CollectMeleeCollateralCommons( CBaseEntity *pVictim, int nLimit ) : m_pVictim( pVictim ), m_nLimit( nLimit ), m_vecOrigin( pVictim->GetAbsOrigin() ) {}
	bool operator()( INextBot *pBot )
	{
		Infected *pCommon = dynamic_cast<Infected *>( pBot->GetEntity() );
		if ( pCommon && pCommon->IsAlive() && pCommon->GetTeamNumber() == TEAM_INFECTED && pCommon->GetZombieClass() == 0 && pCommon != m_pVictim &&
			( pCommon->GetAbsOrigin() - m_vecOrigin ).LengthSqr() <= 72.0f * 72.0f && m_Collected.Count() < m_nLimit )
			m_Collected.AddToTail( pCommon );
		return true;
	}
	CUtlVectorFixedGrowable< Infected *, 16 > m_Collected;
private:
	CBaseEntity *m_pVictim;
	int m_nLimit;
	Vector m_vecOrigin;
};

void CTerrorMeleeWeapon::ShoveNearbyCommonInfected( CBaseEntity *pVictim )
{
	// S:0x3EA7F0 retains the first N in NextBot registration order. Once
	// full, its attempted closest-distance replacement does nothing.
	CollectMeleeCollateralCommons collect( pVictim, melee_collateral_shove_count.GetInt() );
	TheNextBots().ForEachBot( collect );
	FOR_EACH_VEC( collect.m_Collected, i )
		collect.m_Collected[i]->OnShoved( GetTerrorPlayerOwner() );
}

// S3C46A0/3C4700/3C4A40/3C5510: the sole segment collector, shared by
// bullet finalization, melee slashes and continuous chainsaw contacts.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
class CCollectMeleeTongueSegments
{
public:
	CCollectMeleeTongueSegments( const Vector &start, const Vector &end )
		: m_Start( start ), m_End( end ), m_pTongue( NULL ), m_flDistance( FLT_MAX ), m_vecHit( vec3_origin )
	{
	}

	bool operator()( CBasePlayer *pBasePlayer )
	{
		CTerrorPlayer *pSmoker = ToTerrorPlayer( pBasePlayer );
		if ( !pSmoker || !pSmoker->IsAlive() || pSmoker->GetTeamNumber() != TEAM_INFECTED || pSmoker->GetZombieClass() != 1 )
			return true;
		CTongue *pTongue = dynamic_cast<CTongue *>( pSmoker->GetCustomAbility() );
		if ( !pTongue || !pSmoker->m_tongueVictim.Get() )
			return true;

		const int nSegments = pTongue->GetSegmentCount();
		for ( int i = 0; i < nSegments; ++i )
		{
			const Vector segmentStart = pTongue->GetSegmentStart( i );
			const Vector segmentEnd = pTongue->GetSegmentEnd( i );
			Vector attackPoint, tonguePoint;
			float attackFraction, tongueFraction;
			if ( CalcLineToLineIntersectionSegment( m_Start, m_End, segmentStart, segmentEnd,
				&attackPoint, &tonguePoint, &attackFraction, &tongueFraction ) &&
				attackFraction >= 0.0f && attackFraction <= 1.0f && tongueFraction >= 0.0f && tongueFraction <= 1.0f )
			{
				const Vector delta = attackPoint - tonguePoint;
				const float flDistance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
				if ( !( flDistance > m_flDistance ) )
				{
					m_pTongue = pTongue;
					m_flDistance = flDistance;
					m_vecHit = tonguePoint;
				}
			}
		}
		return true;
	}

	const Vector &m_Start;
	const Vector &m_End;
	CTongue *m_pTongue;
	float m_flDistance;
	Vector m_vecHit;
};
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

CTongue *CollectTerrorTongueSegments( const Vector &start, const Vector &end, Vector &hitPoint, float &distance )
{
	CCollectMeleeTongueSegments collect( start, end );
	ForEachPlayer( collect );
	hitPoint = collect.m_vecHit;
	distance = collect.m_flDistance;
	return collect.m_pTongue;
}

void TraceTerrorMeleeTongue( CTerrorPlayer *pPlayer, const Vector &start, const Vector &end, int damageType, const Vector &damageDirection )
{
	Vector hitPoint;
	float distance;
	CTongue *pTongue = CollectTerrorTongueSegments( start, end, hitPoint, distance );
	static ConVarRef tongue_bullet_radius( "tongue_bullet_radius" );
	if ( pTongue && distance < tongue_bullet_radius.GetFloat() )
		pTongue->TakeDamage( pPlayer, 100.0f, damageType, hitPoint, damageDirection );
}
#endif

#ifdef CLIENT_DLL
// C:0x30D670. A null effect entity and a dormant weapon do not create blood.
static void TerrorMeleeBloodSlash( const CEffectData &data )
{
	C_BaseEntity *pEntity = data.GetEntity();
	C_BaseAnimating *pAnimating = pEntity ? pEntity->GetBaseAnimating() : NULL;
	if ( !pAnimating || pAnimating->IsDormant() )
		return;
	CNewParticleEffect *pBlood;
	const int nBlade = pAnimating->LookupAttachment( "attach_blade" );
	if ( nBlade > 0 )
		pBlood = pAnimating->ParticleProp()->Create( "blood_melee_slash_TP", PATTACH_POINT_FOLLOW, nBlade );
	else
		pBlood = pAnimating->ParticleProp()->Create( "blood_melee_slash_TP", PATTACH_CUSTOMORIGIN_FOLLOW, -1, data.m_vOrigin + Vector( 0, 0, 10 ) );
	if ( pBlood )
		pBlood->SetControlPoint( 1, data.m_vOrigin );
}
DECLARE_CLIENT_EFFECT( "bloodslash", TerrorMeleeBloodSlash );
#endif

// Native server 0x3e9dd0; 0x5c51d4 is its original .CRT pointer RVA.
#if defined( L4D2 ) && !defined( CLIENT_DLL )
namespace weapon_meleePrecache
{
class CResourcePrecacher : public CBaseResourcePrecacher
{
public:
	CResourcePrecacher() : CBaseResourcePrecacher( 1, "weapon_melee", 0x5c51d4 ) {}
	virtual void Precache( IPrecacheHandler *handler, bool precache, int resourceList, bool ignoreConditionals ) OVERRIDE
	{
		handler->Precache( RESOURCETYPE_ENTITY, "weapon_melee", precache, resourceList );
	}
};
static CResourcePrecacher s_ResourcePrecacher;
}
#endif
