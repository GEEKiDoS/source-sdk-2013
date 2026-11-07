//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/sound/terror_music.h"
#include "rumble_shared.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"
extern ConVar tongue_choke_damage_interval;
extern ConVar tongue_choke_damage_amount;
extern ConVar tongue_drag_damage_amount;
extern bool IsInCommentaryMode();

// S311F80. These are the real tug fields, not launch/lunge state.
void CTerrorPlayer::ClearTug()
{
	m_tugTimer.Invalidate();
	m_tugDir.Init();
	m_tugDist = 0.0f;
	m_tugStart.Init();
	m_hTugSource = NULL;
}

// S36DBC0, attacker-side producer. The source pin teardown remains in
// terrorplayer_pin.cpp; no release event/body is duplicated here.
void CTerrorPlayer::SetTongueVictim( CTerrorPlayer *victim )
{
	m_tongueVictim = victim;
	SetMaxSpeed( GetPlayerMaxSpeed() );
	RumbleEffect( 0x25, 0, 2 );
}

// S36D860, victim-side producer. The two countdowns and interval clock are
// independent, including which ones survive a release.
void CTerrorPlayer::BecomeTongueVictim( CTerrorPlayer *smoker )
{
	m_tongueVictimTimer.Start();
	m_initialTonguePullDir = GetAbsOrigin() - smoker->GetAbsOrigin();
	if ( m_jockeyAttacker.Get() ) ReleaseJockey( NULL );
	ClearUseEntity();
	m_tongueOwner = smoker;
	// Native6140 is physics impact damage scaling, not the movement timebase.
	SetImpactEnergyScale( 0.0f );
	m_tongueChokeTimer.Start( tongue_choke_damage_interval.GetFloat() );
	EmitSound( "SmokerZombie.TongueHit" );
	SetMaxSpeed( GetPlayerMaxSpeed() );
	GetMusic()->OnPulledByTongue();
	EndReviving( false );
	StopBeingRevived( false );
	StopUseAction();
	SpeakIfAllowed( "PlayerGrabbedByTongue" );
	m_salivaStart = gpGlobals->curtime;
	SetAbsVelocity( vec3_origin );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_GRABBED );
	RumbleEffect( 0x21, 0, 4 );
	RumbleEffect( 0x25, 0, 2 );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CTerrorPlayer::GrabVictimWithTongue( CTerrorPlayer *victim )
{
	static ConVarRef survivor_no_pounce_or_hang( "survivor_no_pounce_or_hang" );
	if ( survivor_no_pounce_or_hang.GetBool() ||
		( IsInCommentaryMode() && victim && !victim->IsBot() ) || !victim ||
		victim->m_tongueOwner.Get() )
	{
		// S313D42/313DFD inline this release with (dx2+dy2)+dz2.
		// The pounce/pummel rejection below uses S307BC0's (dy2+dz2)+dx2.
		CTerrorPlayer *oldVictim = ToTerrorPlayer( m_tongueVictim.Get() );
		if ( !oldVictim ) return;
		CTongue *tongue = dynamic_cast< CTongue * >( m_customAbility.Get() );
		if ( tongue ) tongue->ResetTongueCooldown();
		IGameEvent *event = gameeventmanager->CreateEvent( "tongue_release" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "victim", oldVictim->GetUserID() );
			if ( tongue )
			{
				const Vector delta = tongue->GetGrabPosition() - oldVictim->GetAbsOrigin();
				const float distanceSquared = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
				event->SetInt( "distance", _mm_cvttss_si32( _mm_sqrt_ss( _mm_set_ss( distanceSquared ) ) ) );
			}
			else
				event->SetInt( "distance", 0 );
			gameeventmanager->FireEvent( event );
		}
		if ( CTerrorPlayer *releasedVictim = ToTerrorPlayer( m_tongueVictim.Get() ) )
			releasedVictim->ReleaseTongueVictim();
		ReleaseTongueOwner();
		return;
	}
	if ( victim->m_pounceAttacker.Get() || victim->m_pummelAttacker.Get() )
	{
		ReleaseTongue( false );
		return;
	}
	CTerrorPlayer *jockey = ToTerrorPlayer( victim->m_jockeyAttacker.Get() );
	if ( jockey ) jockey->ReleaseJockey( NULL );
	SetTongueVictim( victim );
	victim->BecomeTongueVictim( this );
	IGameEvent *event = gameeventmanager->CreateEvent( "tongue_grab" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	RumbleEffect( 0x21, 0, 4 );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S36D310. Starting a second reason does NOT restart either clock, repeat
// the weapon holster, music or event. Clearing reason2 cannot end reason1.
void CTerrorPlayer::BeginTongueChoke( int reasons )
{
	const bool wasHanging = m_isHangingFromTongue;
	m_tongueChokeReasons |= reasons;
	m_isHangingFromTongue = true;
	if ( wasHanging ) return;
	if ( GetActiveWeapon() ) GetActiveWeapon()->Holster( NULL );
	m_tonguePullTimer.Start( tongue_choke_damage_interval.GetFloat() );
	m_tongueChokeStartTimer.Start();
	m_tongueChokeTimer.Invalidate();
	GetMusic()->OnChoked();
	IGameEvent *event = gameeventmanager->CreateEvent( "choke_start" );
	if ( event )
	{
		CTerrorPlayer *eventSmoker = GetTongueOwner();
		event->SetInt( "userid", eventSmoker ? eventSmoker->GetUserID() : 0 );
		event->SetInt( "victim", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	CTerrorPlayer *smoker = GetTongueOwner(); // Native re-read after FireEvent.
	if ( smoker ) DebugAbilityVictim( smoker, this, "Tongue choke starting." );
	RequestSurvivorBotAssistance();
}

// S36D750. Prone and reached-owner are different bytes. Do not merge the
// latter with this method: owner-touch36DE80 sets13297, not13304.
void CTerrorPlayer::SetProneTongueDrag( bool prone )
{
	m_isProneTongueDrag = prone;
	if ( gpGlobals->curtime - m_tongueVictimTimer.GetStartTime() > 1.0f && GetActiveWeapon() )
		GetActiveWeapon()->Holster( NULL );
	if ( !m_tongueGrabPending )
	{
		SpeakIfAllowed( "PlayerTonguePullStart" );
		CTerrorPlayer *smoker = ToTerrorPlayer( m_tongueOwner.Get() );
		if ( smoker ) smoker->EmitSound( "SmokerZombie.TongueRetract" );
		m_tongueGrabPending = true;
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S3490A0, called in S338740's player think phase, not ability update. Exact
// >= deadlines, RNG call and damage bits are consumed by infected awards.
void CTerrorPlayer::UpdateTongueVictim()
{
	if ( m_isHangingFromTongue )
	{
		if ( gpGlobals->curtime >= m_tongueVoiceTimer.m_timestamp.Get() && GetTongueOwner() )
		{
			m_tongueVoiceTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			SpeakIfAllowed( "PlayerChoke" );
		}
		if ( gpGlobals->curtime >= m_tonguePullTimer.m_timestamp.Get() && GetTongueOwner() )
		{
			m_tonguePullTimer.Start( tongue_choke_damage_interval.GetFloat() );
			float damage = tongue_choke_damage_amount.GetFloat();
			const int difficulty = TerrorGameRules()->GetDifficultyLevel();
			if ( difficulty == 2 ) damage *= 2.0f;
			else if ( difficulty == 3 ) damage *= 3.0f;
			CTerrorPlayer *attacker = GetTongueOwner();
			CTerrorPlayer *inflictor = GetTongueOwner();
			TakeDamage( CTakeDamageInfo( inflictor, attacker, damage, 0x00100000 ) );
		}
	}
	else if ( TerrorGameRules()->IsPlayerControlledZombies() && IsTongueVictim() &&
		gpGlobals->curtime >= m_tongueChokeTimer.m_timestamp.Get() && GetTongueOwner() )
	{
		m_tongueChokeTimer.Start( tongue_choke_damage_interval.GetFloat() );
		CTerrorPlayer *attacker = GetTongueOwner();
		CTerrorPlayer *inflictor = GetTongueOwner();
		TakeDamage( CTakeDamageInfo( inflictor, attacker, tongue_drag_damage_amount.GetFloat(), 0x00100000 ) );
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

