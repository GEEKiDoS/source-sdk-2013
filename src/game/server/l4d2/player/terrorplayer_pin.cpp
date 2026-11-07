//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/sound/terror_music.h"
// These are genuine M5 ability owners. This translation unit is not registered
// until those implementations exist; do not replace these calls with stubs.
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_leap.h"
#include "l4d2/infected/special/ability_charge.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "in_buttons.h"
#include "activitylist.h"
#include "ai_criteria.h"
#include "l4d2/sound/ai_speechqueue.h"
#include <xmmintrin.h>
#include "tier0/memdbgon.h"

static Activity GetPinDeployActivity()
{
	static const Activity activity = (Activity)ActivityList_IndexForName( "ACT_VM_DEPLOY" );
	return activity;
}

static void FirePinRelease( const char *name, CTerrorPlayer *attacker, CTerrorPlayer *victim, CTerrorPlayer *rescuer )
{
	IGameEvent *event = gameeventmanager->CreateEvent( name );
	if ( !event ) return;
	event->SetInt( "userid", attacker->GetUserID() );
	if ( victim ) event->SetInt( "victim", victim->GetUserID() );
	if ( rescuer || !FStrEq( name, "pounce_end" ) ) event->SetInt( "rescuer", rescuer ? rescuer->GetUserID() : 0 );
	gameeventmanager->FireEvent( event );
}

// S:0x300050 -> S:0x23BAA0. Axis order, signs, sixteen hull-size
// increments and the point trace back to the old origin are observable.
void CTerrorPlayer::UnstickAfterPin()
{
	if ( IsHangingFromLedge() ) return;
	class CUnstickFilter : public CTraceFilterSimple
	{
	public:
		CUnstickFilter( CTerrorPlayer *player ) : CTraceFilterSimple( player, COLLISION_GROUP_PLAYER_MOVEMENT ), m_team( player->GetTeamNumber() ) {}
		virtual bool ShouldHitEntity( IHandleEntity *handle, int mask ) OVERRIDE
		{
			if ( !CTraceFilterSimple::ShouldHitEntity( handle, mask ) )
				return false;
			CBaseEntity *entity = EntityFromEntityHandle( handle );
			return entity && entity->GetTeamNumber() != m_team;
		}
	private:
		int m_team;
	} filter( this );
	const Vector origin = GetAbsOrigin();
	const Vector mins = CollisionProp()->OBBMins();
	const Vector maxs = CollisionProp()->OBBMaxs();
	const Vector size = maxs - mins;
	trace_t trace;
	UTIL_TraceHull( origin, origin, mins, maxs, PlayerSolidMask(), &filter, &trace );
	if ( trace.fraction == 1.0f && !trace.startsolid && !trace.allsolid ) return;
	for ( int step = 0; step < 16; ++step )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			for ( int sign = -1; sign <= 1; sign += 2 )
			{
				Vector position = origin;
				position[axis] += (float)sign * ( size[axis] * (float)step );
				UTIL_TraceHull( position, position, mins, maxs, PlayerSolidMask(), &filter, &trace );
				if ( trace.startsolid || trace.allsolid || trace.fraction != 1.0f ) continue;
				UTIL_TraceLine( origin, position, PlayerSolidMask(), &filter, &trace );
				if ( trace.fraction == 1.0f )
				{
					SetAbsOrigin( position );
					return;
				}
			}
		}
	}
}

// S:0x313220 is the JOCKEY/attacker method; a victim calls its actual
// m_jockeyAttacker's method. It is not a victim-side handle-only reset.
void CTerrorPlayer::ReleaseJockey( CTerrorPlayer *rescuer )
{
	StopSound( "JockeyZombie.Ride" );
	m_vocalizeTimer.Start( 0.0f );
	CTerrorPlayer *victim = ToTerrorPlayer( m_jockeyVictim.Get() );
	if ( !victim ) return;
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE );
	victim->GetMusic()->OnJockeyRideEnded();
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "subject", victim->GetResponseCharacterName() );
	ResponseQueueManager().Queue( "SurvivorJockeyEnded", criteria, 0.0f,
		ResponseTarget_t( ResponseTarget_t::ENTITY, victim ), NULL );
	if ( CLeap *leap = dynamic_cast<CLeap *>( m_customAbility.Get() ) )
	{
		static ConVarRef z_leap_interval_post_ride( "z_leap_interval_post_ride" );
		leap->SetRechargeTime( z_leap_interval_post_ride.GetFloat(), 0.0f );
	}
	const float duration = gpGlobals->curtime - m_jockeyRideStartTime;
	m_checkpointPZLongestJockeyRide = MAX( m_checkpointPZLongestJockeyRide.Get(), (int)duration );
	IGameEvent *event = gameeventmanager->CreateEvent( "jockey_ride_end" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetFloat( "ride_length", duration );
		event->SetInt( "victim", victim->GetUserID() );
		event->SetInt( "rescuer", rescuer ? rescuer->GetUserID() : 0 );
		gameeventmanager->FireEvent( event );
	}
	victim->m_jockeyAttacker = NULL;
	m_jockeyVictim = NULL;
	SetParent( NULL );
	SetMoveType( MOVETYPE_WALK );
	SetModelScale( 1.0f, 0.0f );
	victim->m_invulnerabilityTimer.Start( 2.0f );
	if ( victim->GetActiveWeapon() ) victim->GetActiveWeapon()->Deploy();
	if ( GetActiveWeapon() ) GetActiveWeapon()->SendWeaponAnim( GetPinDeployActivity() );
	m_jockeyMountTimer.Invalidate();
	victim->RumbleEffect( 0x27, 0, 1 );
	RumbleEffect( 0x27, 0, 1 );
}

// S:0x313630 is the Hunter/attacker's release, and still emits pounce_end
// when its victim handle cannot resolve. Do not repair that event omission.
void CTerrorPlayer::ReleasePounce()
{
	EHANDLE world;
	world.Init( 0, 0 );
	if ( m_isAttemptingToPounce && gpGlobals->curtime < m_stunTimer.m_timestamp.Get() )
		EmitSound( "HunterZombie.Pounce.Cancel" );
	if ( m_pounceLanded ) m_pounceReleaseFrame = gpGlobals->framecount;
	CTerrorPlayer *victim = static_cast<CTerrorPlayer *>( m_pounceVictim.Get() );
	if ( victim )
	{
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE );
		victim->GetMusic()->OnPounceEnded();
		CBaseEntity *worldEntity = world.Get();
		if ( victim->m_pounceAttacker.Get() != worldEntity )
			victim->m_pounceAttacker.GetForModify().Init( 0, 0 );
		if ( victim->GetActiveWeapon() ) victim->GetActiveWeapon()->Deploy();
		victim->m_invulnerabilityTimer.Start( 2.0f );
		victim->m_Local.m_bDucked = true;
		victim->m_Local.m_bDucking = false;
		victim->m_Local.m_nDuckTimeMsecs = 0;
		victim->AddFlag( FL_DUCKING );
	}
	FirePinRelease( "pounce_end", this, victim, NULL );
	CBaseEntity *worldEntity = world.Get();
	if ( m_pounceVictim.Get() != worldEntity )
		m_pounceVictim.GetForModify().Init( 0, 0 );
	worldEntity = world.Get();
	if ( m_pounceAttacker.Get() != worldEntity )
		m_pounceAttacker.GetForModify().Init( 0, 0 );
	m_isAttemptingToPounce = false;
	if ( m_pounceLanded && GetActiveWeapon() ) GetActiveWeapon()->SendWeaponAnim( GetPinDeployActivity() );
	m_pounceLanded = false;
	if ( m_customAbility ) m_customAbility->StopActivation( 0.0f );
}

// S:0x30D350 preserves its asymmetric attacker/victim branches and the
// reciprocal call's false flag. There is no global exactly-once release rule.
void CTerrorPlayer::ReleasePummel( bool releaseOther, CTerrorPlayer *rescuer )
{
	CTerrorPlayer *victim = ToTerrorPlayer( m_pummelVictim.Get() );
	if ( victim )
	{
		RemoveFlag( FL_FROZEN );
		if ( releaseOther ) victim->ReleasePummel( false, NULL );
		IGameEvent *event = gameeventmanager->CreateEvent( "charger_pummel_end" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			CTerrorPlayer *eventVictim = ToTerrorPlayer( m_pummelVictim.Get() );
			if ( eventVictim ) event->SetInt( "victim", eventVictim->GetUserID() );
			event->SetInt( "rescuer", rescuer ? rescuer->GetUserID() : 0 );
			gameeventmanager->FireEvent( event );
		}
		if ( m_pummelVictim.Get() ) m_pummelVictim = NULL;
		if ( m_pummelTarget.Get() ) m_pummelTarget = NULL;
		if ( CCharge *charge = dynamic_cast<CCharge *>( m_customAbility.Get() ) ) charge->OnPummelEnded();
		return;
	}
	CTerrorPlayer *attacker = ToTerrorPlayer( m_pummelAttacker.Get() );
	if ( !attacker ) return;
	m_invulnerabilityTimer.Start( 2.0f );
	if ( releaseOther ) attacker->ReleasePummel( false, NULL );
	SetParent( NULL );
	SetMoveType( MOVETYPE_WALK );
	m_pummelAttacker = NULL;
	m_pummelObject = NULL;
	GetMusic()->OnPummeledEnd();
	UnstickAfterPin();
}

// S:0x36D560. Independent choke reasons overlap; clearing reason3 is not
// permission to clear every other reason or to release the tongue owner.
void CTerrorPlayer::EndTongueChoke( int reasons )
{
	m_tongueChokeReasons &= ~reasons;
	if ( !m_isHangingFromTongue || m_tongueChokeReasons != 0 ) return;
	m_isHangingFromTongue = false;
	m_reachedTongueOwner = false;
	if ( reasons == 3 && GetActiveWeapon() && GetActiveWeapon()->IsEffectActive( EF_NODRAW ) )
	{
		GetActiveWeapon()->Deploy();
		if ( IsIncapacitated() )
		{
			CBaseCombatWeapon *secondary = Weapon_GetSlot( 1 );
			if ( secondary ) Weapon_Switch( secondary );
		}
		else
		{
			m_invulnerabilityTimer.Start( 2.0f );
		}
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "choke_end" );
	if ( event )
	{
		CTerrorPlayer *owner = ToTerrorPlayer( m_tongueOwner.Get() );
		if ( owner ) event->SetInt( "userid", owner->GetUserID() );
		event->SetInt( "victim", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	CTerrorPlayer *owner = GetTongueOwner(); // S36D743: fresh read after event delivery.
	if ( owner ) DebugAbilityVictim( owner, this, "Tongue choke ending." );
}

// S:0x36E8C0: the actual victim-side teardown.
void CTerrorPlayer::ReleaseTongueVictim()
{
	Vector velocity = GetAbsVelocity();
	velocity.z = 0.0f;
	SetAbsVelocity( velocity );
	if ( m_tongueOwner.Get() && m_tongueVictimTimer.HasStarted() )
		m_checkpointPZLongestSmokerGrab = MAX( m_checkpointPZLongestSmokerGrab.Get(), (int)m_tongueVictimTimer.GetElapsedTime() );
	m_tongueVictimTimer.Invalidate();
	m_hTongueReleaseOwner = m_tongueOwner.Get();
	m_tongueReleaseTimer.Start( 3.0f );
	SetImpactEnergyScale( 1.0f ); // Native36E8C0 resets impact damage, not movement time.
	EndTongueChoke( 3 );
	m_tongueOwner = NULL;
	m_isProneTongueDrag = false;
	static ConVarRef tongue_release_fatigue_penalty( "tongue_release_fatigue_penalty" );
	m_flStamina += (float)tongue_release_fatigue_penalty.GetInt();
	SetMaxSpeed( GetPlayerMaxSpeed() );
	m_tonguePullTimer.Invalidate();
	m_tongueChokeTimer.Invalidate();
	GetMusic()->OnReleasedByTongue();
	m_invulnerabilityTimer.Start( 2.0f );
	RumbleEffect( 0x25, 0, 1 );
}

// S:0x36D200: the actual attacker-side teardown. Its RTTI tongue callback
// is a proven nullsub31; it does not reset the cooldown a second time.
void CTerrorPlayer::ReleaseTongueOwner()
{
	m_tongueGrabPending = false;
	m_tongueVictim = NULL;
	SetMaxSpeed( GetPlayerMaxSpeed() );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_TONGUE_ATTACK_END );
	RumbleEffect( 0x25, 0, 1 );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S:0x307BC0. The native argument is force; apparent EDI/ESI arguments in
// userpurge pseudocode are not semantic inputs.
void CTerrorPlayer::ReleaseTongue( bool force )
{
	CTerrorPlayer *victim = ToTerrorPlayer( m_tongueVictim.Get() );
	if ( !victim && !force ) return;
	CTongue *tongue = dynamic_cast<CTongue *>( m_customAbility.Get() );
	if ( tongue && victim ) tongue->ResetTongueCooldown();
	IGameEvent *event = gameeventmanager->CreateEvent( "tongue_release" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim ? victim->GetUserID() : 0 );
		if ( tongue && victim )
		{
			const Vector delta = tongue->GetGrabPosition() - victim->GetAbsOrigin();
			const float distanceSquared = ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x;
			event->SetInt( "distance", _mm_cvttss_si32( _mm_sqrt_ss( _mm_set_ss( distanceSquared ) ) ) );
		}
		else
			event->SetInt( "distance", 0 );
		gameeventmanager->FireEvent( event );
	}
	if ( victim )
	{
		// S307D98 resolves the handle again after synchronous event delivery.
		// A cleared handle skips only victim teardown, not owner teardown.
		if ( CTerrorPlayer *releasedVictim = ToTerrorPlayer( m_tongueVictim.Get() ) )
			releasedVictim->ReleaseTongueVictim();
	}
	ReleaseTongueOwner();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
