//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/base_backpack_item.h"
#include "collisionutils.h"
#include "ndebugoverlay.h"
#include "l4d2/weapons/base_mounted_weapon.h"
#include "l4d2/weapons/prop_mounted_gun.h"
#include "in_buttons.h"
#include "igameevents.h"
#include "trains.h"
#include "ai_speech.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "AI_Criteria.h"
#include "EventLog.h"
#include "tier1/fmtstr.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/player/terror_saved_player_states.h"
#include "particle_parse.h"
#include "world.h"
#include <float.h>
#include "tier0/memdbgon.h"

ConVar survivor_revive_duration( "survivor_revive_duration", "5", 0x00004004 );
ConVar survivor_revive_continuous_fire( "survivor_revive_continuous_fire", "1", 0x00002006, "Players need to hold +attack to revive" );
extern ConVar z_round_start_attack_prevention;
ConVar adrenaline_revive_speedup( "adrenaline_revive_speedup", "0.5", 0x00004004,
	"Percent of normal revive duration to use when affected by Adrenaline", true, 0.01f, true, 1.0f );
ConVar sb_revive_friend_distance( "sb_revive_friend_distance", "125.0", 0x00004004 );
extern ConVar player_use_radius;
extern ConVar player_incap_use_radius;
extern ConVar z_use_tolerance;
extern ConVar sv_debug_player_use;
extern ConVar sv_turbophysics;
extern int TrainSpeed( int iSpeed, int iMax );
bool g_bDebugPlayerUse = true;
ConVar action_progress_reset_interval( "action_progress_reset_interval", "1.0", 0x00004004 );
ConVar survivor_push( "survivor_push", "0", 0x00004004 );
ConVar survivor_revive_after_drag( "survivor_revive_after_drag", "0", 0x00002006,
	"Standing still after dragging someone will start reviving him" );
ConVar survivor_drag_continuous_fire( "survivor_drag_continuous_fire", "1", 0x00002006,
	"Survivors need to hold +attack2 to drag incapacitated survivors" );

extern ConVar survivor_incap_decay_rate;
extern ConVar survivor_incap_hopeless_decay_rate;
extern ConVar survivor_incap_tongued_decay_rate;

void CTerrorPlayer::ResetForSpawn()
{
	// S2CB580, virtual558/+2232. The punch/view-roll/last-shove writes belong
	// only to the client ResetForSpawn; do not copy them into this body.
	m_roundStartTime[ GetTeamNumber() % 2 ] = gpGlobals->curtime;
	GetTerrorAnimState()->ClearAnimationState();
	if ( m_jumpSupressedUntil != -1.0f )
		m_jumpSupressedUntil = -1.0f;
	if ( m_TimeForceExternalView != -1.0f )
		m_TimeForceExternalView = -1.0f;
	if ( m_iShovePenalty != 0 )
		m_iShovePenalty = 0;
	if ( m_flNextShoveTime != 0.0f )
		m_flNextShoveTime = 0.0f;
	UnforceButtons( IN_SPEED );
	UnforceButtons( IN_DUCK );
	EnableButtons( IN_JUMP );
}

// S3051C0. Both identities are formatted, including an unused NULL second.
void LogIncapEvent( const char *format, CBaseEntity *first, CBaseEntity *second )
{
	CEventLog *eventLog = static_cast< CEventLog * >( GameLogSystem() );
	char firstIdentity[256];
	char secondIdentity[256];
	eventLog->GetEntityIdentity( first, firstIdentity, sizeof( firstIdentity ) );
	eventLog->GetEntityIdentity( second, secondIdentity, sizeof( secondIdentity ) );
	CFmtStrN< 256 > message( format, firstIdentity, secondIdentity );
	UTIL_LogPrintf( "(INCAP) %s\n", message.Access() );
	DevMsg( 2, "(INCAP) %s\n", message.Access() );
}


// S:0x246AE0 uses the entity's simulation clock, not the command or think clock.
void CCSPlayer::SetProgressBarTime( float flDuration )
{
	m_flProgressBarDuration = flDuration;
	m_flProgressBarStartTime = GetSimulationTime();
}

void CCSPlayer::SetProgressBarStartTime( float flStartTime )
{
	m_flProgressBarStartTime = flStartTime;
}


bool CTerrorPlayer::IsDominated() const
{
	// S:0x2CB240. Incapacitation and ledge hanging are separate predicates.
	return IsPummelVictim() || IsBeingPounced() || IsTongueVictim() ||
		IsJockeyVictim() || IsBeingCarried();
}

CBaseMountedWeapon *CTerrorPlayer::GetMountedWeapon()
{
	// S2CA070: mounted-weapon byte, same raw use handle, actual RTTI family.
	return m_usingMountedWeapon ? dynamic_cast< CBaseMountedWeapon * >( GetUseEntity() ) : NULL;
}

bool CTerrorPlayer::IsPerformingOtherUseAction( int nAction ) const
{
	// S2C7F30 differs from the self-permitted S2C7E30 predicate.
	if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
		return false;
	if ( m_useActionOwner.Get() != this )
		return false;
	return ToBasePlayer( m_useActionTarget.Get() ) != this;
}

bool CTerrorPlayer::IsPerformingUseAction( int nAction ) const
{
	// S:0x2C7E30: a zero argument means any nonzero current action.
	if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
		return false;
	return m_useActionOwner.Get() == this;
}

bool CTerrorPlayer::IsUseActionTarget( int nAction ) const
{
	// S:0x2C7DA0 deliberately distinguishes a self action from its recipient.
	if ( nAction ? m_iCurrentUseAction != nAction : m_iCurrentUseAction == 0 )
		return false;
	return m_useActionOwner.Get() != this && m_useActionTarget.Get() == this;
}

void CTerrorPlayer::DispatchUseActionAnimation( int nAction, CBaseEntity *pTarget )
{
	// S:0x2FC210. These are game-owned event names, not retail enum casts.
	if ( pTarget == this )
	{
		if ( nAction == 5 )
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_RETURN_TO_LIFE );
		else if ( nAction == 6 || nAction == 7 )
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_DEPLOY_UPGRADE );
		else
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_HEAL_SELF );
	}
	else
	{
		switch ( nAction )
		{
		case 4:
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_DEFIBRILLATE );
			break;
		case 8:
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUR_GAS );
			break;
		case 9:
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUR_COLA );
			break;
		default:
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_HEAL_OTHER );
			break;
		}
	}
}

void CTerrorPlayer::SetUseAction( int nAction, CBaseEntity *pTarget, float flDuration, bool bShowProgress )
{
	// S:0x315900. Acquisition is rejected, not a cancellation/retry transaction.
	if ( !pTarget || IsDominated() )
		return;
	CTerrorPlayer *pPlayerTarget = ToTerrorPlayer( pTarget );
	if ( ( pPlayerTarget && pPlayerTarget->m_iCurrentUseAction != 0 ) ||
		IsPerformingUseAction() || IsUseActionTarget() )
		return;

	if ( GetMoveParent() )
		SetParent( NULL );
	if ( bShowProgress )
		SetProgressBarTime( flDuration );

	m_useActionOwner = this;
	m_useActionTarget = pTarget;
	m_iCurrentUseAction = nAction;
	DispatchUseActionAnimation( nAction, pTarget );

	if ( pPlayerTarget && pPlayerTarget != this )
	{
		pPlayerTarget->SetProgressBarTime( flDuration );
		pPlayerTarget->m_useActionOwner = this;
		pPlayerTarget->m_useActionTarget = pTarget;
		pPlayerTarget->m_iCurrentUseAction = nAction;
	}
}

void CTerrorPlayer::StopUseAction()
{
	// S:0x315CD0 captures both participants before clearing either handle.
	CTerrorPlayer *pOwner = ToTerrorPlayer( m_useActionOwner );
	CTerrorPlayer *pTarget = ToTerrorPlayer( m_useActionTarget );
	if ( pOwner )
	{
		CTerrorWeapon *pWeapon = static_cast< CTerrorWeapon * >( pOwner->GetActiveWeapon() );
		if ( pWeapon )
			pWeapon->SuppressHelpingHand( 0.0f );
		pOwner->SetProgressBarTime( 0.0f );
		pOwner->m_useActionTarget = NULL;
		pOwner->m_useActionOwner = NULL;
		pOwner->m_iCurrentUseAction = 0;
	}
	if ( pTarget && pTarget != pOwner )
	{
		pTarget->SetProgressBarTime( 0.0f );
		pTarget->m_useActionTarget = NULL;
		pTarget->m_useActionOwner = NULL;
		pTarget->m_iCurrentUseAction = 0;
	}
}

void CTerrorPlayer::UpdateUseActionProgress( float flElapsed, float flDuration )
{
	// S:0x3023A0 updates both actual participants, including the self/self case.
	CTerrorPlayer *pOwner = ToTerrorPlayer( m_useActionOwner );
	CTerrorPlayer *pTarget = ToTerrorPlayer( m_useActionTarget );
	if ( pOwner )
	{
		pOwner->SetProgressBarTime( flDuration );
		pOwner->SetProgressBarStartTime( pOwner->GetSimulationTime() - flElapsed );
	}
	if ( pTarget )
	{
		pTarget->SetProgressBarTime( flDuration );
		pTarget->SetProgressBarStartTime( pTarget->GetSimulationTime() - flElapsed );
	}
}

void CTerrorPlayer::OnUseEntityChanged()
{
	// S:0x317BF0; the timer's stripped name is not claimed as an original symbol.
	m_useEntityChangedTimer.Start();
	m_usingMountedGun = dynamic_cast< CPropMountedGun * >( GetUseEntity() ) != NULL;
	m_usingMountedWeapon = dynamic_cast< CBaseMountedWeapon * >( GetUseEntity() ) != NULL;
}

bool CTerrorPlayer::IsPerformingSelfUseAction( int nAction ) const
{
	// S:0x2C7EA0 is stricter than IsPerformingUseAction.
	return IsPerformingUseAction( nAction ) && m_useActionTarget.Get() == this;
}

float CTerrorPlayer::GetReviveRemainingTime() const
{
	return m_reviveTimer.GetRemainingTime();
}

float CTerrorPlayer::GetReviveDuration() const
{
	return m_reviveTimer.GetCountdownDuration();
}

void CTerrorPlayer::EndReviving( bool bInterrupted )
{
	// S:0x31F180. Even an uninterrupted EndReviving sends revive_end.
	SetProgressBarTime( 0.0f );
	CTerrorPlayer *pTarget = ToTerrorPlayer( m_reviveTarget.Get() );
	if ( pTarget )
	{
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_USE );
		pTarget->DoTerrorAnimationEvent( TERROR_ANIMEVENT_REVIVE_TARGET_END );
		pTarget->m_reviveOwner = NULL;
		pTarget->SetProgressBarTime( 0.0f );
		if ( bInterrupted )
			EmitSound( "Player.StopVoice" );

		IGameEvent *pEvent = gameeventmanager->CreateEvent( "revive_end" );
		if ( pEvent )
		{
			pEvent->SetInt( "userid", GetUserID() );
			pEvent->SetInt( "subject", pTarget->GetUserID() );
			pEvent->SetBool( "ledge_hang", pTarget->IsHangingFromLedge() );
			gameeventmanager->FireEvent( pEvent );
		}
		if ( bInterrupted )
			pTarget->SpeakReviveInterrupted( this, pTarget->IsHangingFromLedge(), gpGlobals->curtime + 0.2f );
	}
	m_reviveTarget = NULL;
	StopUseAction();
}

void CTerrorPlayer::StopBeingRevived( bool bInterrupted )
{
	// S:0x31EFE0 is not EndReviving: it deliberately sends no revive_end.
	SetProgressBarTime( 0.0f );
	CTerrorPlayer *pOwner = ToTerrorPlayer( m_reviveOwner.Get() );
	if ( pOwner )
	{
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_REVIVE_TARGET_END );
		pOwner->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_USE );
		pOwner->m_reviveTarget = NULL;
		pOwner->SetProgressBarTime( 0.0f );
		if ( bInterrupted )
			pOwner->EmitSound( "Player.StopVoice" );
	}
	m_reviveOwner = NULL;
	StopUseAction();
}

bool CTerrorPlayer::CanAttack() const
{
	// S:0x2C8EC0. The two team slots are written by the real round-start path.
	if ( ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		m_roundStartTime[0] + z_round_start_attack_prevention.GetFloat() > gpGlobals->curtime )
		return false;
	if ( GetTeamNumber() == TEAM_INFECTED && m_zombieClass == 3 && !( GetFlags() & FL_ONGROUND ) )
		return false;
	if ( m_isHangingFromTongue || ( IsKnockedDown() && m_knockdownReason == 2 ) || IsStaggering() ||
		IsBeingPounced() || IsJockeyVictim() || IsPummelVictim() )
		return false;
	const Activity activity = GetTerrorActivity();
	if ( IsAttackBlockingActivity( activity ) || IsPerformingUseAction( 10 ) || IsPerformingUseAction( 11 ) )
		return false;
	switch ( LookupTerrorOriginalActivity( activity ) )
	{
	case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH:
	case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TANKPUNCH:
	case TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND:
	case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_CHARGER:
	case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_CHARGERHIT:
	case TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW:
	case TERROR_ORIGINAL_ACT_TERROR_POUNCED_TO_STAND:
		return false;
	default:
		return true;
	}
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
bool CTerrorPlayer::IsRecentlyAttacking() const
{
	// S:0x3021E0 has no same-tick previous-shot fallback.
	return m_terrorFireInterval.HasStarted() && m_terrorFireInterval.GetElapsedTime() < 0.5f;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
bool CTerrorPlayer::IsRecentlyShoving() const
{
	return m_terrorShoveInterval.HasStarted() && m_terrorShoveInterval.GetElapsedTime() < 1.0f;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif


void CTerrorPlayer::OnPlayerUse( CBaseEntity *pActivator, CBaseEntity *pCaller, USE_TYPE useType, float flValue )
{
	// S320070 is NONvirtual; inherited Use dispatches the actual Spawn-installed callback.
	CTerrorPlayer *pUser = ToTerrorPlayer( pActivator );
	if ( !pUser || !IsAlive() || !IsIncapacitated() ||
		( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		( m_reviveOwner.Get() && m_reviveOwner.Get() != pUser ) )
		return;
	if ( ( pUser->GetTeamNumber() != TEAM_SURVIVOR && pUser->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		pUser->IsIncapacitated() || pUser->IsRecentlyAttacking() || pUser->IsRecentlyShoving() ||
		pUser->IsTongueVictim() || pUser->IsBeingPounced() || pUser->IsJockeyVictim() ||
		pUser->IsPummelVictim() || pUser->m_reviveTarget.Get() )
		return;
	if ( pUser->m_dragTarget.Get() )
	{
		if ( pUser->m_dragTarget.Get() != this || !( pUser->m_afButtonPressed & IN_USE ) )
			return;
	}
	else if ( m_reviveOwner.Get() )
	{
		return;
	}
	pUser->BeginReviving( this );
}

void CTerrorPlayer::OnPlayerUsed( CBaseEntity *pTarget, USE_TYPE useType )
{
	// S:0x301D80. The original stat singleton's reached notification is empty.
	if ( !pTarget || ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
		pTarget->IsPlayer() )
		return;
	CBaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( ( pWeapon && pWeapon->m_bInReload ) || m_usingMountedWeapon )
		return;
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_ITEM_PICKUP );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "player_use" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		pEvent->SetInt( "targetid", pTarget->entindex() );
		gameeventmanager->FireEvent( pEvent );
	}
}

void CTerrorPlayer::StopDragging()
{
	// S:0x31E920 preserves the event's subject before releasing the path.
	CTerrorPlayer *pTarget = ToTerrorPlayer( m_dragTarget.Get() );
	if ( !pTarget )
		return;
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "drag_end" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		pEvent->SetInt( "subject", pTarget->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
	pTarget->StopBeingDragged();
	m_dragTarget = NULL;
}

void CTerrorPlayer::StopBeingDragged()
{
	// S:0x3157C0. The path is a linked list of vectors, not another action.
	m_dragOwner = NULL;
	m_dragPath.Purge();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTerrorPlayer::BeginReviving( CTerrorPlayer *pTarget )
{
	// S:0x31EAC0 rejects competition. It does not steal another reviver's target.
	if ( !pTarget || pTarget->m_reviveOwner.Get() || !pTarget->CanBeRevived() ||
		pTarget->IsBeingPounced() || m_reviveTarget.Get() )
		return;
	if ( m_lastDamageTime + 2.0f > gpGlobals->curtime )
	{
		CBaseEntity *pAttacker = m_lastDamageInfo.GetAttacker();
		CBaseCombatCharacter *pCombatAttacker = pAttacker ? pAttacker->MyCombatCharacterPointer() : NULL;
		// S:0x25BB80 is a revive-source policy, not the strict IsZombieClass query.
		if ( pCombatAttacker &&
			( pCombatAttacker->GetTeamNumber() != TEAM_INFECTED || pCombatAttacker->GetZombieClass() == 7 ) &&
			pAttacker->IsAlive() )
			return;
	}
	if ( pTarget->GetTongueOwner() )
	{
		DebugAbilityVictim( pTarget->GetTongueOwner(), pTarget, "Tongue letting because victim is being revived" );
		if ( CTerrorPlayer *owner = pTarget->GetTongueOwner() )
			owner->ReleaseTongue( false );
	}

	m_reviveStartButtons = pTarget == this ? 0 : m_nButtons & ( IN_ATTACK | IN_USE );
	StopDragging();
	pTarget->StopBeingDragged();
	LogIncapEvent( "%s starts reviving %s", this, pTarget );
	m_reviveTarget = pTarget;
	pTarget->m_reviveOwner = this;

	IGameEvent *pEvent = gameeventmanager->CreateEvent( "revive_begin" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		pEvent->SetInt( "subject", pTarget->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
	DoTerrorAnimationEvent( pTarget->GetAbsOrigin().z <= WorldSpaceCenter().z ?
		TERROR_ANIMEVENT_REVIVE_START_LOW : TERROR_ANIMEVENT_REVIVE_START_HIGH );
	pTarget->DoTerrorAnimationEvent( TERROR_ANIMEVENT_REVIVE_TARGET_START );
	if ( GetActiveWeapon() )
		GetActiveWeapon()->AbortReload();

	float flDuration = survivor_revive_duration.GetFloat();
	if ( m_bAdrenalineActive )
		flDuration *= adrenaline_revive_speedup.GetFloat();
	SetProgressBarTime( flDuration );
	pTarget->SetProgressBarTime( flDuration );

	char modifiers[256];
	Q_snprintf( modifiers, sizeof( modifiers ), "subject:%s", pTarget->GetResponseCharacterName() );
	const char *pConcept;
	if ( pTarget == this )
		pConcept = "PlayerHeal";
	else if ( pTarget->m_currentReviveCount >= TerrorGameRules()->GetSurvivorMaxIncapacitatedCount() - 1 )
		pConcept = pTarget->IsHangingFromLedge() ? "PlayerLedgeSaveCritical" : "PlayerReviveFriendCritical";
	else
		pConcept = pTarget->IsHangingFromLedge() ? "PlayerLedgeSave" : "PlayerReviveFriend";
	SpeakIfAllowed( pConcept, 3, pTarget == this ? NULL : modifiers, NULL, 0, NULL );

	m_reviveTimer.Start( flDuration );
	EyeVectors( &m_reviveStartDirection );
	m_reviveStartPosition = GetAbsOrigin();
	SetAbsVelocity( vec3_origin );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTerrorPlayer::UpdateReviving()
{
	if ( !IsIncapacitated() )
	{
		// ---- Reviver side (34A857..34ACC4) ----
		CTerrorPlayer *target = ToTerrorPlayer( m_reviveTarget.Get() );
		if ( !target )
			return;
		// The deadline is tested before any interruption condition.
		if ( m_reviveTimer.IsElapsed() )
		{
			target->OnRevived();
			return;
		}

		Vector forward;
		EyeVectors( &forward ); // 7A660(this, &forward, NULL, NULL); result unused natively
		const float rangeSqr = sb_revive_friend_distance.GetFloat() * sb_revive_friend_distance.GetFloat();

		bool inRange;
		if ( m_hElevator.Get() )
		{
			// Native recomputes this player's dirty abs transform before the target's.
			const Vector &origin = GetAbsOrigin();
			const Vector &targetOrigin = target->GetAbsOrigin();
			const float dy = targetOrigin.y - origin.y;
			const float dx = targetOrigin.x - origin.x;
			inRange = !( dy * dy + dx * dx > rangeSqr ) || ( GetFlags() & FL_FROZEN );
		}
		else
		{
			Vector targetPosition;
			if ( !target->m_isHangingFromLedge )
			{
				targetPosition = target->WorldSpaceCenter();
			}
			else
			{
				const Vector &view = g_pGameRules->GetViewVectors()->m_vView;
				targetPosition.x = ( target->m_hangPos.Get().x + view.x ) - target->m_hangNormal.Get().x * 32.0f;
				targetPosition.y = ( target->m_hangPos.Get().y + view.y ) - target->m_hangNormal.Get().y * 32.0f;
				targetPosition.z = target->m_hangPos.Get().z + view.z;
			}
			inRange = !( ( GetAbsOrigin() - targetPosition ).LengthSqr() > rangeSqr ) ||
				!( ( EyePosition() - targetPosition ).LengthSqr() > rangeSqr ) ||
				( GetFlags() & FL_FROZEN );
		}

		if ( !inRange )
		{
			LogIncapEvent( "%s was moved and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( IsIncapacitated() )
		{
			LogIncapEvent( "%s was incapacitated and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( IsRecentlyAttacking() )
		{
			LogIncapEvent( "%s attacked and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( GetActiveWeapon() && GetActiveWeapon()->m_bInReload )
		{
			LogIncapEvent( "%s reloads and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( IsRecentlyShoving() )
		{
			LogIncapEvent( "%s shoved and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( IsTongueVictim() )
		{
			LogIncapEvent( "%s got pulled and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else if ( IsBeingPounced() )
		{
			LogIncapEvent( "%s was pounced and stops reviving %s", this, m_reviveTarget.Get() );
		}
		else
		{
			if ( !survivor_revive_continuous_fire.GetInt() || ( m_nButtons & ( IN_ATTACK | IN_USE ) ) == m_reviveStartButtons )
				return;
			LogIncapEvent( "%s stopped holding revive key and stops reviving %s", this, m_reviveTarget.Get() );
		}
		EndReviving( true );
		return;
	}

	// ---- Incapacitated side (34A2BA..34A856) ----
	if ( m_helpTimer.IsElapsed() && !m_reviveOwner.Get() )
	{
		m_helpTimer.Start( RandomFloat( 5.0f, 10.0f ) );
		SpeakIfAllowed( "PlayerHelp", 1 );
	}

	// Networked CountdownTimer at 15944: started and elapsed (>=), then invalidated with a change notification.
	if ( m_downAndBeatenTimer.HasStarted() && m_downAndBeatenTimer.IsElapsed() )
	{
		m_downAndBeatenTimer.Invalidate();
		GetMusic()->OnDownAndBeatenEnded();
	}

	if ( m_incapacitatedTimer.IsElapsed() && !TheDirector->IsFinaleWon() && m_reviveOwner == NULL && !IsBeingPounced() )
	{
		float damage = survivor_incap_decay_rate.GetFloat();
		// S:25B50/25170 flags 1, counting every admitted player in callback order: the existing owner.
		if ( CountLivingSurvivors( 1 ) == 0 )
			damage = survivor_incap_hopeless_decay_rate.GetFloat();
		else if ( IsTongueVictim() )
			damage = survivor_incap_tongued_decay_rate.GetFloat();

		int players, restoring;
		TerrorGameRules()->GetRestoringPlayerCounts( TEAM_SURVIVOR, &players, &restoring );
		if ( players - restoring <= 0 )
		{
			CBaseEntity *world = GetWorldEntity();
			CTakeDamageInfo info( world, world, damage, 0x00020000u, 0 );
			TakeDamage( info );
		}
		// Recorded even when the restoring-player gate suppressed the damage.
		if ( IsTongueVictim() )
			GetTongueOwner()->RecordDamageToSurvivor( (int)damage, false, GetSurvivorCharacter() );
		m_incapacitatedTimer.Start( 1.0f );
		m_survivorIntensity.Hold( 1.0f );
	}

	if ( IsAlive() && !m_isBleedingOut && survivor_incap_decay_rate.GetFloat() * 10.0f > (float)GetHealth() )
	{
		m_isBleedingOut = true;
		if ( !m_isHangingFromLedge )
			GetMusic()->OnBleedingOut();        // Music +312
		else
			GetMusic()->OnLedgeHangBleedingOut(); // Music +280
	}

	// Self revive: this player is its own revive target.
	if ( ToTerrorPlayer( m_reviveTarget.Get() ) == this && m_reviveTimer.IsElapsed() )
		OnRevived();

	// Incap blood pool, once per incapacitation (flag reset in OnIncapacitated 32EB40).
	// Native COMISS/JBE: an invalid or unordered (NaN) timestamp counts as not started.
	const float incapTimestamp = m_incapacitatedTimestamp.GetStartTime();
	if ( !m_incapBloodPoolPlaced && ( !( incapTimestamp > 0.0f ) || gpGlobals->curtime - incapTimestamp > 1.5f ) )
	{
		if ( GetAbsVelocity().LengthSqr() < 100.0f )
		{
			m_incapBloodPoolPlaced = true;
			const Vector origin = GetAbsOrigin();
			if ( UTIL_IsGroundFlat( 48.0f, origin, VEC_DUCK_HULL_MAX.z, 0x400Bu, this, false ) )
			{
				trace_t trace;
				UTIL_TraceLine( origin, Vector( origin.x, origin.y, origin.z - 75.0f ), 0x400Bu,
					this, COLLISION_GROUP_DEBRIS, &trace );
				if ( trace.fraction < 1.0f && !trace.allsolid && trace.m_pEnt && !trace.m_pEnt->ClassMatches( "prop_*" ) )
				{
					QAngle angles;
					VectorAngles( Vector( trace.plane.normal.x, trace.plane.normal.z, -trace.plane.normal.y ), angles );
					CDisablePredictionFiltering filtering;
					DispatchParticleEffect( "blood_incapacitated", trace.endpos, angles, this );
				}
			}
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

int CTerrorPlayer::ObjectCaps()
{
	// S:0x34E0A0 retains the base entity's real parent/model capabilities.
	int caps = CBaseEntity::ObjectCaps() & ~FCAP_ACROSS_TRANSITION;
	const Activity activity = GetTerrorActivity();
	if ( LookupTerrorOriginalActivity( activity ) == TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND ||
		LookupTerrorOriginalActivity( activity ) == TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING ||
		!IsAlive() || ( GetTeamNumber() != TEAM_SURVIVOR && GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
		return caps;
	if ( CanBeRevived() )
		return caps | FCAP_CONTINUOUS_USE;
	return caps | FCAP_IMPULSE_USE;
}

int CTerrorPlayer::GetUsePriority( CBaseEntity *pUser )
{
	// S:0x34E040. A healthy survivor has priority zero, not one.
	if ( IsAlive() && CanBeRevived() )
		return 2;
	if ( IsAlive() && ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		return 0;
	return 1;
}

bool CTerrorPlayer::IsUseableEntity( CBaseEntity *pEntity, unsigned int requiredCaps )
{
	// S:0x2FFFB0. Script models are accepted independently of their model caps.
	if ( GetTeamNumber() == TEAM_INFECTED )
		return !IsGhost() && CBasePlayer::IsUseableEntity( pEntity, requiredCaps | FCAP_USE_BY_INFECTED );
	if ( !IsAlive() )
		return false;
	if ( pEntity && pEntity->m_hScriptUseTarget.Get() )
		return true;
	return CBasePlayer::IsUseableEntity( pEntity, requiredCaps );
}

typedef CUtlVectorFixedGrowable< CBaseEntity *, 256 > UseCandidateVector;

class CUseTraceFilter : public CTraceFilterSimple
{
public:
	CUseTraceFilter( CTerrorPlayer *pUser, const UseCandidateVector *pIgnored )
		: CTraceFilterSimple( NULL, COLLISION_GROUP_INTERACTIVE ), m_pUser( pUser ),
		m_pWeapon( pUser ? static_cast< CTerrorWeapon * >( pUser->GetActiveWeapon() ) : NULL ), m_pIgnored( pIgnored )
	{
	}

	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		if ( pHandleEntity == m_pUser )
			return false;
		if ( m_pIgnored )
		{
			for ( int i = 0; i < m_pIgnored->Count(); ++i )
				if ( pHandleEntity == ( *m_pIgnored )[i] )
					return false;
		}
		if ( !CTraceFilterSimple::ShouldHitEntity( pHandleEntity, contentsMask ) )
			return false;
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		if ( !pEntity || !pEntity->IsPlayer() )
			return true;
		CTerrorPlayer *pPlayer = ToTerrorPlayer( pEntity );
		if ( pPlayer->IsGhost() || !pPlayer->IsAlive() || !m_pUser ||
			( m_pUser->GetTeamNumber() != TEAM_SURVIVOR && m_pUser->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) ||
			( pPlayer->GetTeamNumber() != TEAM_SURVIVOR && pPlayer->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
			return false;
		if ( !pPlayer->CanBeRevived() )
			return m_pWeapon && ( m_pWeapon->GetWeaponID() == TERROR_WEAPON_PAIN_PILLS ||
				m_pWeapon->GetWeaponID() == TERROR_WEAPON_FIRST_AID_KIT ||
				m_pWeapon->GetWeaponID() == TERROR_WEAPON_ADRENALINE );
		return !pPlayer->m_reviveOwner.Get() || pPlayer->m_reviveOwner.Get() == m_pUser;
	}

private:
	CTerrorPlayer *m_pUser;
	CTerrorWeapon *m_pWeapon;
	const UseCandidateVector *m_pIgnored;
};

static int UseCandidateSort( CBaseEntity *const *pLeft, CBaseEntity *const *pRight )
{
	// S:0x34E110 returns only 0 or 1. Keep the original malformed comparator.
	return ( *pLeft )->entindex() < ( *pRight )->entindex();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S4EA090. Selector consumers spill the returned length before subtraction.
static float NormalizeUseDirection( Vector &direction )
{
	const double length = sqrt( ( static_cast< double >( direction.x ) * direction.x +
		static_cast< double >( direction.y ) * direction.y ) +
		static_cast< double >( direction.z ) * direction.z );
	const float roundedLength = static_cast< float >( length );
	const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
	direction *= inverseLength;
	return roundedLength;
}

static float UseCollisionDistanceSqr( CBaseEntity *pEntity, const Vector &position )
{
	Vector local, nearest;
	CCollisionProperty *pCollision = pEntity->CollisionProp();
	pCollision->WorldToCollisionSpace( position, &local );
	CalcClosestPointOnAABB( pCollision->OBBMins(), pCollision->OBBMaxs(), local, nearest );
	return ( local - nearest ).LengthSqr();
}

CBaseEntity *CTerrorPlayer::FindUseEntity()
{
	return FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
}

CBaseEntity *CTerrorPlayer::FindUseEntity( float flRange, float flDirectRange, float flMinimumDot,
	bool *pOutInRange, bool bPrioritizePlayers )
{
	// S:0x34E790. Keep trace, category ordering, collision points and fallback separate.
	if ( flDirectRange <= 0.0f || flDirectRange > flRange )
		flDirectRange = flRange;
	if ( flMinimumDot <= 0.0f )
	{
		// S:0x34E790 uses original weapon v419, not backpack ancestry.
		CTerrorWeapon *pWeapon = static_cast< CTerrorWeapon * >( GetActiveWeapon() );
		flMinimumDot = pWeapon && pWeapon->IsCarriedProp() ? 0.98f : z_use_tolerance.GetFloat();
	}
	if ( pOutInRange )
		*pOutInRange = false;
	if ( IsPerformingUseAction() || GetMoveType() == MOVETYPE_LADDER || IsDominated() ||
		IsKnockedDown() || GetTerrorAnimState()->IsUseActionAnimationBlocked() )
		return NULL;
	if ( m_reviveTarget.Get() || m_reviveOwner.Get() )
	{
		if ( pOutInRange )
			*pOutInRange = m_reviveTarget.Get() != NULL;
		return m_reviveTarget.Get();
	}
	if ( m_dragTarget.Get() )
	{
		if ( pOutInRange )
			*pOutInRange = true;
		return m_dragTarget.Get();
	}

	Vector forward;
	EyeVectors( &forward );
	const Vector eye = EyePosition();
	Vector useOrigin = eye;
	useOrigin.x = GetAbsOrigin().x;
	useOrigin.y = GetAbsOrigin().y;
	const Vector directDelta = forward * flDirectRange;
	const Vector rangeDelta = forward * flRange;
	const float flRangeSqr = flRange * flRange;
	const float flDirectRangeSqr = flDirectRange * flDirectRange;
	const bool bDebug = g_bDebugPlayerUse && sv_debug_player_use.GetInt() == entindex();
	if ( bDebug )
		Msg( "\n" );
	UseCandidateVector candidates, ignored;
	CUseTraceFilter filter( this, &ignored );
	trace_t directTrace;
	UTIL_TraceLine( eye, eye + rangeDelta, 0x0200608B, &filter, &directTrace );
	CBaseEntity *pBest = NULL;
	CBaseEntity *pDirect = directTrace.m_pEnt;
	float flBestDot = 0.0f;
	int nBestPriority = 0;
	bool bBestInRange = false;
	if ( pDirect && IsUseableEntity( pDirect, 0 ) )
	{
		const float flDistanceSqr = UseCollisionDistanceSqr( pDirect, useOrigin );
		if ( flRangeSqr > flDistanceSqr )
		{
			CTerrorPlayer *pPlayer = ToTerrorPlayer( pDirect );
			bool bAccept = true;
			float flNearSqr = flDirectRangeSqr;
			if ( pPlayer && !pPlayer->CanBeRevived() )
			{
				if ( flRange == player_use_radius.GetFloat() && flDistanceSqr > 9216.0f )
					bAccept = false;
				flNearSqr = 9216.0f;
			}
			if ( bAccept )
			{
				pBest = pDirect;
				flBestDot = 1.0f;
				nBestPriority = pPlayer && bPrioritizePlayers ? 2 : pDirect->GetUsePriority( this );
				bBestInRange = flDirectRange == flRange || flNearSqr >= flDistanceSqr;
				if ( bDebug )
				{
					NDebugOverlay::Line( useOrigin, directTrace.endpos, 0, 255, 0, true, 0.5f );
					NDebugOverlay::Cross3D( directTrace.endpos, 16.0f, 0, 255, 0, true, 0.5f );
					Msg( "Trace using: %s\n", pDirect->GetDebugName() );
				}
			}
		}
	}
	for ( CEntitySphereQuery sphere( useOrigin, flRange + 32.0f ); sphere.GetCurrentEntity(); sphere.NextEntity() )
	{
		CBaseEntity *pEntity = sphere.GetCurrentEntity();
		if ( pEntity == this || pEntity == pBest || !IsUseableEntity( pEntity, 0 ) )
			continue;
		candidates.AddToTail( pEntity );
		if ( pEntity->GetUsePriority( this ) != 2 )
			ignored.AddToTail( pEntity );
	}
	candidates.Sort( UseCandidateSort );
	ignored.Sort( UseCandidateSort );
	for ( int i = 0; i < candidates.Count(); ++i )
	{
		CBaseEntity *pCandidate = candidates[i];
		if ( bDebug )
			Msg( "entity %d(%s) at %f %f %f\n", pCandidate->entindex(), pCandidate->GetDebugName(),
				pCandidate->GetAbsOrigin().x, pCandidate->GetAbsOrigin().y, pCandidate->GetAbsOrigin().z );
		const int nPriority = pCandidate->IsPlayer() && bPrioritizePlayers ? 2 : pCandidate->GetUsePriority( this );
		if ( nPriority < nBestPriority )
			continue;
		const float flDistanceSqr = UseCollisionDistanceSqr( pCandidate, useOrigin );
		if ( bDebug )
		{
			Vector nearest;
			pCandidate->CollisionProp()->CalcNearestPoint( useOrigin, &nearest );
			char label[264];
			Q_snprintf( label, sizeof( label ), "%d: %f", pCandidate->entindex(), flDistanceSqr );
			NDebugOverlay::Text( nearest, label, true, 0.5f );
		}
		if ( flDistanceSqr > flRangeSqr )
			continue;
		const bool bInRange = flDirectRange == flRange || flDirectRangeSqr >= flDistanceSqr;
		if ( bBestInRange && !bInRange )
			continue;
		if ( pCandidate->IsPlayer() && flRange == player_use_radius.GetFloat() && flDistanceSqr > 9216.0f )
			continue;

		CCollisionProperty *pCollision = pCandidate->CollisionProp();
		Vector worldMins, worldMaxs, triggerMins, triggerMaxs;
		pCollision->WorldSpaceAABB( &worldMins, &worldMaxs );
		pCollision->WorldSpaceTriggerBounds( &triggerMins, &triggerMaxs );
		float flBloat = triggerMaxs.x - worldMaxs.x;
		const Vector &delta = bInRange ? directDelta : rangeDelta;
		CBaseTrace boxTrace;
		Vector candidatePoint;
		if ( pCandidate->GetSolid() == SOLID_OBB )
		{
			if ( !IntersectRayWithOBB( eye, delta, pCollision->CollisionToWorldTransform(),
				pCollision->OBBMins(), pCollision->OBBMaxs(), 0.0f, &boxTrace ) )
				continue;
			// The pinned selector transforms the already world-space OBB end point again.
			VectorTransform( boxTrace.endpos, pCollision->CollisionToWorldTransform(), candidatePoint );
		}
		else if ( IntersectRayWithBox( eye, delta, worldMins, worldMaxs, 1.0f, &boxTrace ) )
		{
			candidatePoint = boxTrace.endpos;
		}
		else
		{
			CalcClosestPointOnLineSegment( pCandidate->WorldSpaceCenter(), eye, eye + delta * 0.5f, candidatePoint );
		}
		if ( flBloat > 0.0f )
		{
			Vector towardEye = eye - candidatePoint;
			const float lengthMinusOne = NormalizeUseDirection( towardEye ) - 1.0f;
			if ( !( lengthMinusOne > flBloat ) ) flBloat = lengthMinusOne;
			if ( flBloat > 0.0f )
			{
				towardEye.z = MAX( towardEye.z, 0.0f );
				candidatePoint += towardEye * flBloat;
			}
		}
		Vector nearestPoint;
		pCollision->CalcNearestPoint( candidatePoint, &nearestPoint );
		if ( flBloat > 0.0f )
		{
			Vector towardEye = eye - nearestPoint;
			const float lengthMinusOne = NormalizeUseDirection( towardEye ) - 1.0f;
			const float flShift = lengthMinusOne > flBloat ? flBloat : lengthMinusOne;
			if ( flShift > 0.0f )
			{
				towardEye.z = MAX( towardEye.z, 0.0f );
				nearestPoint += towardEye * flShift;
			}
		}
		if ( bDebug )
		{
			NDebugOverlay::Cross3D( candidatePoint, 5.0f, 0, 255, 0, true, 0.5f );
			NDebugOverlay::Cross3D( nearestPoint, 5.0f, 0, 0, 255, true, 0.5f );
		}
		Vector direction = nearestPoint - eye;
		NormalizeUseDirection( direction );
		const float flDot = DotProduct( direction, forward );
		if ( flMinimumDot > flDot || !( ( !bBestInRange && bInRange ) ||
			flBestDot < flDot || nPriority > nBestPriority ) )
			continue;
		trace_t trace;
		UTIL_TraceLine( eye, nearestPoint, 0x0200608B, &filter, &trace );
		if ( trace.fraction != 1.0f && trace.m_pEnt != pCandidate )
		{
			UTIL_TraceLine( eye, pCandidate->WorldSpaceCenter(), 0x0200608B, &filter, &trace );
			if ( trace.fraction != 1.0f && trace.m_pEnt != pCandidate )
				continue;
		}
		nBestPriority = nPriority;
		bBestInRange = bInRange;
		flBestDot = flDot;
		pBest = pCandidate;
	}
	if ( bDebug )
	{
		if ( !pBest || pBest == pDirect )
		{
			const int red = pBest ? 0 : 255;
			const int green = pBest ? 255 : 0;
			NDebugOverlay::Line( useOrigin, directTrace.endpos, red, green, 0, true, 0.5f );
			NDebugOverlay::Cross3D( directTrace.endpos, 16.0f, red, green, 0, true, 0.5f );
		}
		else
		{
			NDebugOverlay::Box( pBest->WorldSpaceCenter(), Vector( -8, -8, -8 ), Vector( 8, 8, 8 ),
				0, 255, 0, 1, 0.5f );
		}
		if ( pBest )
			Msg( "Radial using: %d(%s) at %f %f %f\n", pBest->entindex(), pBest->GetDebugName(),
				pBest->GetAbsOrigin().x, pBest->GetAbsOrigin().y, pBest->GetAbsOrigin().z );
		else
			Msg( "Radial using: no usable entity found\n" );
	}
	if ( !pBest )
	{
		// Original VEngineServer022 slot126 is not SDK IsDedicatedServer (slot2).
		float flBestFallbackDot = ShouldUseWideUseTolerance() ? 0.70f : 0.85f;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *pPlayer = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !pPlayer || pPlayer == this || !pPlayer->CanBeRevived() ||
				( pPlayer->GetTeamNumber() != TEAM_SURVIVOR && pPlayer->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
				continue;
			Vector position = pPlayer->GetAbsOrigin();
			if ( pPlayer->IsHangingFromLedge() && GetAbsOrigin().z > position.z )
				position.z += g_pGameRules->GetViewVectors()->m_vView.z;
			Vector direction = position - EyePosition();
			if ( direction.LengthSqr() >= Square( player_incap_use_radius.GetFloat() ) )
				continue;
			NormalizeUseDirection( direction );
			const float flDot = DotProduct( direction, forward );
			if ( flBestFallbackDot >= flDot )
				continue;
			CTraceFilterNoNPCsOrPlayer fallbackFilter( NULL, COLLISION_GROUP_NONE );
			trace_t trace;
			UTIL_TraceLine( position, EyePosition(), 0x0200608B, &fallbackFilter, &trace );
			if ( trace.fraction != 1.0f )
				continue;
			flBestFallbackDot = flDot;
			pBest = pPlayer;
			bBestInRange = true;
		}
	}
	if ( pOutInRange )
		*pOutInRange = bBestInRange;
	return pBest;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool CTerrorPlayer::CanPerformBackpackAction() const
{
	// S:0x2C7CE0's death-animation exception precedes every pin/weapon gate.
	if ( IsIncapacitated() )
		return false;
	const TerrorOriginalActivity activity = LookupTerrorOriginalActivity( GetTerrorActivity() );
	if ( activity == TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND || activity == TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING )
		return true;
	if ( IsBeingPounced() || m_pounceVictim.Get() || IsPummelVictim() ||
		m_pummelVictim.Get() || m_jockeyVictim.Get() || IsBeingCarried() || IsTongueVictim() )
		return false;
	const CTerrorWeapon *pWeapon = static_cast< const CTerrorWeapon * >( GetActiveWeapon() );
	return ( !pWeapon || pWeapon->CanPerformUseAction() ) && m_iCurrentUseAction == 0;
}

void CTerrorPlayer::ResetUseActionProgress()
{
	// S:0x302480 restarts a hold; it neither cancels nor clears either action.
	if ( m_lastUseActionProgressReset + action_progress_reset_interval.GetFloat() > gpGlobals->curtime )
		return;
	m_lastUseActionProgressReset = gpGlobals->curtime;
	CTerrorPlayer *pOwner = ToTerrorPlayer( m_useActionOwner.Get() );
	if ( pOwner )
	{
		pOwner->SetProgressBarStartTime( pOwner->GetSimulationTime() );
		CBaseBackpackItem *pBackpack = dynamic_cast< CBaseBackpackItem * >( GetActiveWeapon() );
		if ( pBackpack )
			pBackpack->ExtendAction();
	}
	CBaseEntity *pTarget = m_useActionTarget.Get();
	if ( pTarget && pTarget->IsPlayer() && pTarget != pOwner )
	{
		CTerrorPlayer *pPlayerTarget = ToTerrorPlayer( pTarget );
		pPlayerTarget->SetProgressBarStartTime( pPlayerTarget->GetSimulationTime() );
	}
	// The original's caller must supply a real owner; do not silently replace it.
	DispatchUseActionAnimation( pOwner->m_iCurrentUseAction, pTarget );
}

class CTerrorUsePushFilter : public CTraceFilterEntitiesOnly
{
public:
	virtual bool ShouldHitEntity( IHandleEntity *pHandleEntity, int contentsMask )
	{
		CBaseEntity *pEntity = EntityFromEntityHandle( pHandleEntity );
		return pEntity && pEntity->VPhysicsGetObject() && pEntity->GetMoveType() == MOVETYPE_VPHYSICS &&
			!CBasePlayer::CanPickupObject( pEntity, 35.0f, 128.0f );
	}
};

void CTerrorPlayer::PlayerUse()
{
	PlayerUse( NULL );
}

void CTerrorPlayer::PlayerUse( CBaseEntity *pPreferred )
{
	// S:0x312BB0 wraps S:0x07C460; do not fall back to the SDK's selector policy.
	const bool bPressedUse = ( m_afButtonPressed & IN_USE ) != 0;
	const bool bSurvivor = GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == TEAM_L4D1_SURVIVOR;
	CBaseEntity *pSelected = pPreferred;
	if ( IsAlive() && bSurvivor )
	{
		if ( ( !GetGroundEntity() || bPressedUse ||
			( m_afButtonPressed & ( IN_ATTACK | IN_JUMP | IN_USE | IN_ATTACK2 ) ) ) && m_pushEntity.Get() )
		{
			SetPushEntity( NULL );
			return;
		}
		if ( survivor_push.GetBool() && bPressedUse && sv_turbophysics.GetBool() && CanAttack() && !IsBusy() )
		{
			Vector forward;
			EyeVectors( &forward );
			if ( m_pushEntity.Get() )
			{
				Vector towardPush = m_pushEntity->WorldSpaceCenter() - EyePosition();
				VectorNormalize( towardPush );
				if ( DotProduct( towardPush, forward ) >= 0.0f )
					forward = towardPush;
				else
					SetPushEntity( NULL );
			}
			CTerrorUsePushFilter filter;
			trace_t trace;
			const Vector eye = EyePosition();
			UTIL_TraceLine( eye, eye + forward * 96.0f, 0x0200400B, &filter, &trace );
			if ( trace.m_pEnt && trace.m_pEnt->VPhysicsGetObject() )
			{
				if ( trace.m_pEnt != m_pushEntity.Get() )
					SetPushEntity( trace.m_pEnt );
				DoTerrorAnimationEvent( TERROR_ANIMEVENT_USE_PUSH, 704 );
				return;
			}
		}
		if ( m_pushEntity.Get() )
			return;
		if ( bPressedUse && !pSelected )
		{
			CBaseBackpackItem *pBackpack = dynamic_cast< CBaseBackpackItem * >( GetActiveWeapon() );
			if ( pBackpack && pBackpack->UseAsPrimary() )
				return;
			pSelected = FindUseEntity();
			if ( !pSelected && TryUseSpeech() )
				return;
		}
	}
	else if ( GetTeamNumber() == TEAM_INFECTED && bPressedUse )
	{
		HandleInfectedUse();
	}
	SetPushEntity( NULL );
	DispatchPlayerUse( pSelected );
	if ( IsAlive() && bSurvivor && bPressedUse && !pSelected )
	{
		CBaseBackpackItem *pBackpack = dynamic_cast< CBaseBackpackItem * >( GetActiveWeapon() );
		if ( pBackpack )
			SwitchToNextBestWeapon( pBackpack );
	}
}

void CTerrorPlayer::DispatchPlayerUse( CBaseEntity *pSelected )
{
	// S:0x07C460. The input output-ID and the issuer notification type agree;
	// the target's actual Use function is still reached through native entity I/O.
	if ( !( ( m_nButtons | m_afButtonPressed | m_afButtonReleased ) & IN_USE ) )
		return;
	if ( IsObserver() )
	{
		if ( m_afButtonPressed & IN_USE )
			ObserverUse( true );
		else if ( m_afButtonReleased & IN_USE )
			ObserverUse( false );
		return;
	}
	if ( m_afButtonPressed & IN_USE )
	{
		if ( ClearUseEntity() )
			return;
		if ( m_afPhysicsFlags & PFLAG_DIROVERRIDE )
		{
			m_afPhysicsFlags &= ~PFLAG_DIROVERRIDE;
			m_iTrain = TRAIN_NEW | TRAIN_OFF;
			return;
		}
		CBaseEntity *pTrain = GetGroundEntity();
		if ( pTrain && !( m_nButtons & IN_JUMP ) && ( GetFlags() & FL_ONGROUND ) &&
			( pTrain->ObjectCaps() & FCAP_DIRECTIONAL_USE ) && pTrain->OnControls( this ) )
		{
			m_afPhysicsFlags |= PFLAG_DIROVERRIDE;
			m_iTrain = TrainSpeed( pTrain->m_flSpeed, static_cast< CFuncTrackTrain * >( pTrain )->GetMaxSpeed() ) |
				TRAIN_ACTIVE | TRAIN_NEW;
			EmitSound( "Player.UseTrain" );
			return;
		}
	}
	if ( !pSelected )
		pSelected = FindUseEntity();
	if ( !pSelected )
	{
		if ( m_afButtonPressed & IN_USE )
			PlayUseDenySound();
		return;
	}
	const int caps = pSelected->ObjectCaps();
	USE_TYPE useType;
	if ( ( ( m_nButtons & IN_USE ) && ( caps & FCAP_CONTINUOUS_USE ) ) ||
		( ( m_afButtonPressed & IN_USE ) && ( caps & ( FCAP_IMPULSE_USE | FCAP_ONOFF_USE ) ) ) )
	{
		if ( caps & FCAP_CONTINUOUS_USE )
			m_afPhysicsFlags |= PFLAG_USING;
		useType = ( pSelected->ObjectCaps() & FCAP_ONOFF_USE ) ? USE_ON : USE_TOGGLE;
	}
	else if ( ( m_afButtonReleased & IN_USE ) && ( pSelected->ObjectCaps() & FCAP_ONOFF_USE ) )
	{
		useType = USE_OFF;
	}
	else if ( pSelected->m_hScriptUseTarget.Get() )
	{
		useType = USE_ON;
	}
	else
	{
		return;
	}
	variant_t emptyVariant;
	pSelected->AcceptInput( "Use", this, this, emptyVariant, useType );
	OnPlayerUsed( pSelected, useType );
}


void CTerrorPlayer::BeginDragging( CTerrorPlayer *pTarget )
{
	// S:0x334C30 is reached by the original choose_drag client command.
	if ( !pTarget || !pTarget->IsIncapacitated() || pTarget->IsHangingFromLedge() ||
		pTarget->m_isHangingFromTongue || pTarget->m_dragOwner.Get() )
		return;
	if ( pTarget->m_reviveOwner.Get() &&
		( !survivor_revive_continuous_fire.GetBool() || pTarget->m_reviveOwner.Get() != this ) )
		return;
	if ( m_dragTarget.Get() )
		StopDragging();
	EndReviving( true );
	m_dragTarget = pTarget;
	pTarget->StartBeingDragged( this );
	IGameEvent *pEvent = gameeventmanager->CreateEvent( "drag_begin" );
	if ( pEvent )
	{
		pEvent->SetInt( "userid", GetUserID() );
		pEvent->SetInt( "subject", pTarget->GetUserID() );
		gameeventmanager->FireEvent( pEvent );
	}
}

void CTerrorPlayer::StartBeingDragged( CTerrorPlayer *pOwner )
{
	// S:0x32F7C0 creates ten successive origin samples, without a range shortcut.
	if ( !pOwner )
	{
		m_dragOwner = NULL;
		return;
	}
	m_dragOwner = pOwner;
	m_dragPath.EnsureCapacity( 10 );
	const Vector position = GetAbsOrigin();
	const Vector step = ( pOwner->GetAbsOrigin() - position ) * 0.1f;
	for ( int i = 1; i <= 10; ++i )
		m_dragPath.AddToTail( position + step * static_cast< float >( i ) );
	m_dragOwnerLastPosition = pOwner->GetAbsOrigin();
}

void CTerrorPlayer::UpdateDragging()
{
	// S:0x34C070 only the real reciprocal owner runs the continuation.
	CTerrorPlayer *pTarget = ToTerrorPlayer( m_dragTarget.Get() );
	if ( !pTarget || pTarget->m_dragOwner.Get() != this )
	{
		m_dragTarget = NULL;
		return;
	}
	if ( !pTarget->IsAlive() || !pTarget->IsIncapacitated() || pTarget->IsHangingFromLedge() ||
		pTarget->m_isHangingFromTongue || !IsAlive() || IsIncapacitated() || !GetGroundEntity() ||
		GetMoveType() == MOVETYPE_LADDER || !CanPerformBackpackAction() || IsBusy() )
	{
		StopDragging();
		return;
	}
	if ( survivor_revive_continuous_fire.GetBool() && survivor_revive_after_drag.GetBool() &&
		!( m_nButtons & ( IN_JUMP | IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT ) ) &&
		GetAbsOrigin().IsZero( 0.01f ) )
	{
		// The pinned routine tests ORIGIN, not velocity, despite the cvar help text.
		BeginReviving( pTarget );
		return;
	}
	if ( survivor_drag_continuous_fire.GetBool() && !( m_nButtons & IN_ATTACK2 ) )
		StopDragging();
}

void CTerrorPlayer::UpdateBeingDragged()
{
	// S:0x34DE70 consumes one path point for each distinct owner origin.
	CTerrorPlayer *pOwner = ToTerrorPlayer( m_dragOwner.Get() );
	if ( !IsIncapacitated() || IsHangingFromLedge() || m_isHangingFromTongue || !pOwner )
	{
		StopBeingDragged();
		return;
	}
	if ( m_dragOwnerLastPosition == pOwner->GetAbsOrigin() )
		return;
	const unsigned short head = m_dragPath.Head();
	const Vector position = m_dragPath[head];
	m_dragPath.Remove( head );
	m_dragPath.AddToTail( pOwner->GetAbsOrigin() );
	SetAbsOrigin( position );
	m_dragOwnerLastPosition = pOwner->GetAbsOrigin();
}

bool CTerrorPlayer::ShouldUseWideUseTolerance() const
{
	// Pinned engine.dll VEngineServer022 vtable RVA 0x37D9BC, slot 126,
	// cell RVA 0x37DBB4 -> engine RVA 0x1319E0: xor al, al; ret.
	// This is the recovered engine implementation, not a missing-state fallback.
	return false;
}

void CTerrorPlayer::SpeakReviveInterrupted( CTerrorPlayer *pReviver, bool bLedgeHang, float flWhen )
{
	// S:0x31F180 -> common response queue S:0x02A570, mode 2.
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "Subject", pReviver->GetResponseCharacterName(), 1.0f );
	criteria.AppendCriteria( "LedgeHang", bLedgeHang ? "1" : "0", 1.0f );
	QueueResponseConceptForEntity( "ReviveMeInterrupted", criteria, flWhen, this, pReviver );
}

void CTerrorPlayer::UpdateBuildableButtonUseEntity()
{
	// S:0x318A00 records the selected model, not its script-use redirect.
	CBaseEntity *pUse = FindUseEntity( player_use_radius.GetFloat(), 0.0f, 0.0f, NULL, false );
	if ( pUse && pUse->m_hScriptUseTarget.Get() )
	{
		m_hBuildableButtonUseEnt.Set( pUse );
	}
	else
	{
		const CHandle< CBaseEntity > hZero( 0, 0 );
		if ( m_hBuildableButtonUseEnt.Get() != hZero.Get() )
			m_hBuildableButtonUseEnt.GetForModify() = hZero;
	}
}

// S32B8F0. Full native infected-use entry, not a caller-forced ghost action.
// Reuse the SAME native16373/76/80/84 state in full Spawn and ZombieManager
// explicit spawn producers; these are not a transition snapshot or new clock.
void CTerrorPlayer::HandleInfectedUse()
{
	if ( GetTeamNumber() != TEAM_INFECTED )
		return;
	if ( IsGhost() )
	{
		if ( TerrorGameRules()->IsPlayerControlledZombies() )
		{
			WarpGhostToInitialPosition( false );
			return;
		}
		// Native functor team=2, flags=0, flow-direction=0. The literal2
		// in its constructor is TEAM_SURVIVOR, not incap-filter flag2.
		CTerrorPlayer *survivor = FindHighestFlowSurvivor( 0, 0 );
		if ( survivor )
		{
			Vector position = survivor->GetAbsOrigin();
			if ( !FindNearbyTransitionSpawn( this, &position, TEAM_INFECTED, false, 100.0f ) )
				FindNearbyTransitionSpawn( this, &position, TEAM_INFECTED, false, 1000.0f );
			SetAbsOrigin( position );
			QAngle angles;
			VectorAngles( survivor->GetAbsOrigin() - GetAbsOrigin(), angles );
			SnapEyeAngles( angles );
		}
		return;
	}
	if ( IsAlive() && m_isCulling )
	{
		m_reusedInfectedSpawnHealth = GetHealth();
		m_bReusingInfectedSpawnState = true;
		if ( GetCustomAbility() )
		{
			m_reusedInfectedRecharge = GetCustomAbility()->GetActivationTimerRemaining();
			m_reusedInfectedRechargeDuration = GetCustomAbility()->GetRechargeDuration();
		}
		State_Transition( STATE_GHOST );
	}
}
