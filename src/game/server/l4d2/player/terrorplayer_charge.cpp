//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/infected/special/ability_charge.h"
#include "ai_criteria.h"
#include "ai_speech.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "in_buttons.h"
#include "tier0/memdbgon.h"

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern ConVar survivor_no_pounce_or_hang;
extern bool IsInCommentaryMode();
ConVar z_charger_pound_dmg( "z_charger_pound_dmg", "15", 0x00004004 );

// S30CF70. The reverse pending-owner handle is NOT m_pummelAttacker: the
// actual pummel and its victim-start event happen later in UpdatePummel.
void CTerrorPlayer::SetPummelTarget( CTerrorPlayer *victim, float delay )
{
	m_pummelTarget = victim;
	if ( victim )
	{
		victim->m_pummelObject = this;
		AddFlag( FL_FROZEN );
	}
	m_pummelTargetTime = delay == -1.0f ? -1.0f : gpGlobals->curtime + delay;
}

// S30D080. All three arguments are booleans. There is no rescuer argument,
// release-time protection timer, universal exactly-once guard or music call.
void CTerrorPlayer::ReleaseCarry( bool releaseOther, bool cancelPummel, bool preserveVictimParent )
{
	CTerrorPlayer *victim = ToTerrorPlayer( m_carryVictim.Get() );
	if ( victim )
	{
		if ( !cancelPummel ) SetPummelTarget( victim, -1.0f );
		if ( releaseOther ) victim->ReleaseCarry( false, cancelPummel, preserveVictimParent );
		IGameEvent *event = gameeventmanager->CreateEvent( "charger_carry_end" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			CTerrorPlayer *eventVictim = ToTerrorPlayer( m_carryVictim.Get() );
			if ( eventVictim ) event->SetInt( "victim", eventVictim->GetUserID() );
			gameeventmanager->FireEvent( event );
		}
		if ( m_carryVictim.Get() ) m_carryVictim = NULL;
		return;
	}
	CTerrorPlayer *attacker = ToTerrorPlayer( m_carryAttacker.Get() );
	if ( !attacker || preserveVictimParent ) return;
	m_isFirmlyAttached = false;
	SetParent( NULL );
	if ( releaseOther ) attacker->ReleaseCarry( false, cancelPummel, false );
	if ( m_carryAttacker.Get() ) m_carryAttacker = NULL;
}

static void HolsterChargeVictimWeapon( CTerrorPlayer *victim )
{
	CBaseCombatWeapon *weapon = victim->GetActiveWeapon();
	if ( !weapon ) return;
	if ( CTerrorWeapon *terrorWeapon = dynamic_cast<CTerrorWeapon *>( weapon ) ) terrorWeapon->CancelSwing();
	weapon->Holster( NULL );
	CBaseCombatWeapon *active = victim->GetActiveWeapon();
	if ( active && active != weapon ) active->Holster( NULL );
}

// S32D930. Actual victim receiver; the carrier is the argument.
void CTerrorPlayer::BecomeCarried( CTerrorPlayer *charger )
{
	EndReviving( false );
	StopBeingRevived( false );
	StopUseAction();
	StopBeingDragged();
	ClearStagger();
	ClearTug();
	CTerrorPlayer *smoker = GetTongueOwner();
	if ( smoker ) smoker->ReleaseTongue( false );
	CTerrorPlayer *hunter = GetPounceAttacker();
	if ( hunter ) hunter->ReleasePounce();
	CTerrorPlayer *jockey = GetJockeyAttacker();
	if ( jockey ) jockey->ReleaseJockey( NULL );
	charger->m_jockeyMountDuration = 0.2f;
	charger->m_jockeyMountTimer.Start( 0.2f );
	m_carryAttacker = charger;
	HolsterChargeVictimWeapon( this );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_CARRIED );
	RumbleEffect( 0x26, 0, 4 );
}
void CTerrorPlayer::CarryVictim( CTerrorPlayer *victim )
{
	IGameEvent *event = gameeventmanager->CreateEvent( "charger_carry_start" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	m_carryVictim = victim;
	victim->BecomeCarried( this );
	RumbleEffect( 0x26, 0, 4 );
}

// S32DDE0. The release order differs from the carry path above; do not
// factor both into a generic grab/reset routine.
void CTerrorPlayer::BecomePummeled( CTerrorPlayer *charger )
{
	EndReviving( false );
	StopBeingRevived( false );
	ReleasePummel( true, NULL );
	StopUseAction();
	StopBeingDragged();
	ClearStagger();
	ClearTug();
	CTerrorPlayer *smoker = GetTongueOwner();
	if ( smoker ) smoker->ReleaseTongue( false );
	CTerrorPlayer *hunter = GetPounceAttacker();
	if ( hunter ) hunter->ReleasePounce();
	CTerrorPlayer *jockey = GetJockeyAttacker();
	if ( jockey ) jockey->ReleaseJockey( NULL );
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "subject", GetResponseCharacterName() );
	ResponseQueueManager().Queue( "SurvivorWasCharged", criteria, 0.0f, ResponseTarget_t( ResponseTarget_t::ANY, NULL ), this );
	m_pummelAttacker = charger;
	RequestSurvivorBotAssistance();
	GetMusic()->OnPummeled();
}
void CTerrorPlayer::StartPummel( CTerrorPlayer *victim )
{
	victim->BecomePummeled( this );
	m_pummelVictim = victim;
	IGameEvent *event = gameeventmanager->CreateEvent( "charger_pummel_start" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}

// S331590. Charge impact is distinct from the earlier carried-player launch
// and from the animation-event-driven pound damage.
void CTerrorPlayer::ImpactChargerVictim( CTerrorPlayer *victim, bool hitWall, bool longFall )
{
	EmitSound( "ChargerZombie.ImpactHard" );
	float damage = 10.0f;
	const int difficulty = TerrorGameRules()->GetDifficultyLevel();
	if ( difficulty == 2 ) damage = 15.0f;
	else if ( difficulty == 3 ) damage = 20.0f;
	CTakeDamageInfo info( this, this, damage, DMG_CLUB );
	info.SetDamagePosition( victim->GetAbsOrigin() );
	if ( longFall ) info.AddDamageType( 0x00008000 ); // S331590: not Source DMG_FALL.
	victim->TakeDamage( info );
	if ( hitWall )
	{
		victim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_IMPACT_WALL_VICTIM );
		Vector forward;
		GetVectors( &forward, NULL, NULL );
		const Vector position = GetAbsOrigin() + forward * 12.0f;
		Bash( 0.0f );
		StaggerPlayer( NULL, &position );
		EmitInfectedSound( "ChargerZombie.Stagger", 0.0f, 0.0f );
		SetPummelTarget( victim, 0.75f );
		UTIL_ScreenShake( GetAbsOrigin(), 5.0f, 150.0f, 1.5f, 120.0f, SHAKE_START );
	}
	else
	{
		victim->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND_VICTIM );
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_IMPACT_GROUND );
		Bash( 1.0f );
		SetPummelTarget( victim, 1.0f );
	}
}

// S346930 deliberately interpolates from the victim's CURRENT position each
// frame, not a captured start. The shared firmly-attached bit lives on THIS
// carrier; the victim release writes its own bit as in the original.
void CTerrorPlayer::UpdateCarry()
{
	CTerrorPlayer *victim = ToTerrorPlayer( m_carryVictim.Get() );
	if ( !victim ) return;
	Vector position = victim->GetAbsOrigin();
	const Vector origin = GetAbsOrigin();
	if ( m_jockeyMountTimer.m_timestamp.Get() > 0.0f && gpGlobals->curtime < m_jockeyMountTimer.m_timestamp.Get() )
	{
		float fraction = 1.0f;
		if ( m_jockeyMountDuration != 0.0f ) fraction = clamp( 1.0f - m_jockeyMountTimer.GetRemainingTime() / m_jockeyMountDuration, 0.0f, 1.0f );
		position = ( origin - position ) * fraction + position;
		victim->SetAbsOrigin( position );
		m_isFirmlyAttached = false;
		return;
	}
	if ( !m_isFirmlyAttached )
	{
		m_isFirmlyAttached = true;
		victim->SetAbsOrigin( origin );
		victim->SetParent( this );
		victim->SetMoveType( MOVETYPE_NONE );
	}
}
void CTerrorPlayer::UpdatePummel()
{
	CTerrorPlayer *target = ToTerrorPlayer( m_pummelTarget.Get() );
	if ( target )
	{
		if ( m_pummelTargetTime == -1.0f )
		{
			if ( IsStaggeringOrPending() ) return;
		}
		else if ( m_pummelTargetTime > gpGlobals->curtime ) return;
		RemoveFlag( FL_FROZEN );
		SetPummelTarget( NULL, -1.0f );
		target->m_pummelObject = NULL;
		if ( target->IsAlive() && IsAlive() )
		{
			StartPummel( target );
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUND_START );
			target->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CHARGER_POUNDED );
			EmitSound( "ChargerZombie.VocalizePummel" );
			m_TimeForceExternalView = gpGlobals->curtime + 5.0f;
		}
		return;
	}
	CTerrorPlayer *attacker = ToTerrorPlayer( m_pummelAttacker.Get() );
	if ( !attacker ) return;
	const Vector origin = attacker->GetAbsOrigin();
	SetAbsOrigin( ( GetAbsOrigin() - origin ) * 0.8f + origin );
	SetAbsVelocity( vec3_origin );
}

// S302DB0 is the reached Charger-specific studio animation event handler.
// The caller retains its native event flags tests and double base forwarding.
void CTerrorPlayer::OnChargerPound()
{
	CBaseEntity *victim = m_pummelVictim.Get();
	if ( victim )
	{
		EmitSound( "ChargerZombie.Pummel", 0.0f );
		(void)TheDirector->GetDifficulty(); // S350BF0's discarded real difficulty read.
		victim->TakeDamage( CTakeDamageInfo( this, this, z_charger_pound_dmg.GetFloat(), DMG_CLUB ) );
		// Original casts/checks AFTER damage, not before the sound/damage calls.
		if ( victim->IsPlayer() )
			static_cast< CTerrorPlayer * >( victim )->SpeakIfAllowed( "chargerpound" );
	}
	UTIL_ScreenShake( GetAbsOrigin(), 2.5f, 75.0f, 1.0f, 72.0f, SHAKE_START );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
