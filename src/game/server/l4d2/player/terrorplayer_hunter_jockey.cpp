//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/infected/special/ability_lunge.h"
#include "l4d2/infected/special/ability_leap.h"
#include "ai_criteria.h"
#include "ai_speech.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "in_buttons.h"
#include "IEffects.h"
#include "te_effect_dispatch.h"
#include "effect_dispatch_data.h"
#include "ipredictionsystem.h"
#include <math.h>
#include "l4d2/player/terrorplayer_hunter_jockey_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern bool IsInCommentaryMode();

// S313170/S3131D0, the existing Hunter-owned hand-blood producers.
void CTerrorPlayer::AddBloodToHands()
{
	const int incremented = static_cast< int >( static_cast< unsigned int >( m_iBloodyHandsLevel.Get() ) + 1u );
	m_iBloodyHandsLevel = clamp( incremented, 0, 10 );
}

void CTerrorPlayer::MakeHandsBloody()
{
	m_iBloodyHandsLevel = 10;
}

ConVar survivor_no_pounce_or_hang( "survivor_no_pounce_or_hang", "0", 0x00004004 );
ConVar z_pounce_delay( "z_pounce_delay", "0", 0x00004004 );
ConVar z_pounce_damage_delay( "z_pounce_damage_delay", "1", 0x00004004 );
ConVar z_pounce_damage_interval( "z_pounce_damage_interval", "0.5", 0x00004004 );
ConVar z_pounce_damage_interrupt( "z_pounce_damage_interrupt", "50", 0x00004004, "Taking this much damage interrupts a pounce attempt" );
ConVar z_pounce_damage( "z_pounce_damage", "5", 0x00004004 );
ConVar z_pounce_silence_range( "z_pounce_silence_range", "500", 0x00004004 );
ConVar z_pounce_allow_partial_hidden( "z_pounce_allow_partial_hidden", "1", 0x00004004 );
ConVar versus_shove_hunter_fov( "versus_shove_hunter_fov", "90", 0x00006004, "Angle that survivor shove will hit hunters" );
ConVar versus_shove_hunter_fov_pouncing( "versus_shove_hunter_fov_pouncing", "45", 0x00006004, "Angle that survivor shove will hit hunters while pouncing" );
ConVar versus_shove_jockey_fov_leaping( "versus_shove_jockey_fov_leaping", "45", 0x00006004, "Angle that survivor shove will hit jockeys while leaping" );
ConVar survivor_min_lunge_stagger_speed( "survivor_min_lunge_stagger_speed", "50", 0x00004004, "Hunters must be lunging this fast to stagger a survivor." );
ConVar survivor_max_lunge_stagger_speed( "survivor_max_lunge_stagger_speed", "220", 0x00004004, "Hunters lunging this fast stagger survivors with full power." );
ConVar survivor_max_lunge_stagger_distance( "survivor_max_lunge_stagger_distance", "30", 0x00004004, "Max distance a stationary, crouched survivor can stagger after being lunged." );
ConVar z_hunter_max_pounce_bonus_damage( "z_hunter_max_pounce_bonus_damage", "24", 0x00004006 );
ConVar z_pounce_stumble_radius( "z_pounce_stumble_radius", "0", 0x00000006, "Radius of the stumble effect when a hunter pounces on someone" );
ConVar z_pounce_stumble_force( "z_pounce_stumble_force", "5", 0x00004004, "Force of the stumble effect when a hunter pounces on someone" );
ConVar z_pounce_shake_amplitude( "z_pounce_shake_amplitude", "4", 0x00000006 );
ConVar z_pounce_shake_duration( "z_pounce_shake_duration", "1.5", 0x00000006 );
ConVar z_pounce_shake_radius( "z_pounce_shake_radius", "250", 0x00000006 );
ConVar z_jockey_ride_damage_delay( "z_jockey_ride_damage_delay", "1.0", 0x00004004 );
ConVar z_jockey_ride_damage_interval( "z_jockey_ride_damage_interval", "1.0", 0x00004004 );
ConVar z_jockey_ride_damage( "z_jockey_ride_damage", "4", 0x00004004 );
ConVar z_jockey_stagger_speed( "z_jockey_stagger_speed", "2.0", 0x00004004 );
ConVar z_jockey_speed_blend( "z_jockey_speed_blend", "2.0", 0x00004004 );
ConVar z_zombie_lunge_push( "z_zombie_lunge_push", "0", 0x00004004, "Does the zombie lunge push players?" );
ConVar z_jockey_pounced_surv_bark( "z_jockey_pounced_surv_bark", "1", 0x00004004,
	"Select between behaviors for survivor shouts when pounced by jockey. 0 dispatches a SurvivorJockeyed concept to all players via THEN_ANY, 1 dispatches SurvivorJockeyed only to the pounced player and SurvivorJockeyedOther to all other players." );

static void QueuePinSpeech( const char *concept, CTerrorPlayer *subject, ResponseTarget_t::Type recipients,
	CBaseEntity *target, CBaseEntity *issuer, float when = 0.0f )
{
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "subject", subject->GetResponseCharacterName() );
	ResponseQueueManager().Queue( concept, criteria, when, ResponseTarget_t( recipients, target ), issuer );
}

static bool IsSurvivorPinTeam( const CTerrorPlayer *player )
{
	return player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S0466B0/S046600. The explicit-cone query tries center, eyes, then origin.
static bool PinFacingPoint( CTerrorPlayer *player, const Vector &point, float dot )
{
	Vector direction = point - player->EyePosition();
	VectorNormalize( direction );
	Vector forward;
	AngleVectors( player->EyeAngles(), &forward );
	return DotProduct( forward, direction ) >= dot;
}

static bool PinFacingPlayer( CTerrorPlayer *player, CTerrorPlayer *target, float dot )
{
	return PinFacingPoint( player, target->WorldSpaceCenter(), dot ) ||
		PinFacingPoint( player, target->EyePosition(), dot ) || PinFacingPoint( player, target->GetAbsOrigin(), dot );
}

static bool PinFacingAimCone( CTerrorPlayer *player, CTerrorPlayer *target )
{
	// The original feeds GetFOV() directly to cos(), without a degrees-to-radians
	// conversion. This is distinct from the explicit versus shove cones.
	const float dot = (float)cos( (double)player->GetFOV() * 0.5 );
	return PinFacingPoint( player, target->GetAbsOrigin(), dot ) ||
		PinFacingPoint( player, target->WorldSpaceCenter(), dot ) || PinFacingPoint( player, target->EyePosition(), dot );
}

static void HolsterPinWeapon( CTerrorPlayer *player )
{
	CBaseCombatWeapon *weapon = player->GetActiveWeapon();
	if ( !weapon ) return;
	static_cast< CTerrorWeapon * >( weapon )->CancelSwing();
	weapon->Holster( NULL );
	CBaseCombatWeapon *active = player->GetActiveWeapon();
	if ( active && active != weapon ) active->Holster( NULL );
}

static void HunterBlood( CTerrorPlayer *attacker, CTerrorPlayer *victim, bool initialImpact )
{
	Vector forward;
	attacker->EyeVectors( &forward );
	VectorNormalize( forward );
	forward.z -= 0.2f;
	trace_t trace;
	const Vector traceEnd = attacker->EyePosition() + forward * 80.0f;
	const Vector traceStart = attacker->EyePosition();
	UTIL_TraceLine( traceStart, traceEnd, 0x46004003, attacker, COLLISION_GROUP_NONE, &trace );
	if ( trace.m_pEnt != victim ) return;
	CDisablePredictionFiltering disablePredictionFiltering;
	UTIL_BloodDecalTrace( &trace, initialImpact ? 0 : victim->BloodColor() );
	CEffectData data;
	data.m_vOrigin = trace.endpos;
	data.m_vNormal = -forward;
	data.m_nEntIndex = victim->entindex();
	data.m_flScale = 20.0f;
	DispatchEffect( "csblood", data );
}

void CTerrorPlayer::SetJockeyVictim( CTerrorPlayer *victim ) { m_jockeyVictim = victim; }
void CTerrorPlayer::SetJockeyAttacker( CTerrorPlayer *attacker ) { m_jockeyAttacker = attacker; }

// S31E000: Hunter launch bookkeeping. CLunge owns queued velocity and cooldown.
void CTerrorPlayer::BeginLunge()
{
	ReleasePounce();
	if ( TerrorGameRules()->IsPlayerControlledZombies() ) m_isAttemptingToPounce = true;
	else if ( IsGhost() ) m_isAttemptingToPounce = false;
	else
	{
		m_isAttemptingToPounce = m_Local.m_bDucked && !IsStaggering() && ( m_nButtons & IN_DUCK );
		// S31E0C3 is only on the non-player-controlled-zombies branch.
		if ( IsLunging() ) m_isAttemptingToPounce = false;
	}
	m_pounceDamageRemaining = z_pounce_damage_interrupt.GetInt();
	if ( m_isAttemptingToPounce )
	{
		if ( GetActiveWeapon() ) GetActiveWeapon()->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_LUNGE_POUNCE ) );
		m_pounceStartPosition = GetAbsOrigin();
		if ( m_pounceSoundTimer.GetStartTime() <= 0.0f || m_pounceSoundTimer.GetElapsedTime() >= 1.5f )
		{
			EmitSound( "HunterZombie.Pounce" );
			m_pounceSoundTimer.Start();
			RumbleEffect( 0x24, 0, 4 );
		}
	}
	else if ( !IsGhost() )
	{
		EmitSound( "HunterZombie.Lunge" );
		RumbleEffect( 0x24, 0, 4 );
	}
	// Retail writes literal handle zero here, not INVALID_EHANDLE_INDEX.
	m_pounceVictim.GetForModify().Init( 0, 0 );
	m_pounceAttacker.GetForModify().Init( 0, 0 );
	m_lungeTimer.Start();
}

// S32D790. Delayed admission belongs to the contacted SURVIVOR, not the Hunter.
void CTerrorPlayer::OnPounceContact( CTerrorPlayer *victim )
{
	if ( victim && IsSurvivorPinTeam( victim ) && m_isAttemptingToPounce )
	{
		victim->m_pendingPounceTime = gpGlobals->curtime + z_pounce_delay.GetFloat();
		victim->m_pendingPounceAttacker = this;
	}
	else if ( victim )
	{
		victim->OnLungedBy( this, z_zombie_lunge_push.GetBool() );
	}
}

// S32C2D0: this is the contacted player. A shove can repel the actual Hunter;
// this is not damage to the Survivor and not an attacker-side role copy.
void CTerrorPlayer::OnLungedBy( CTerrorPlayer *attacker, bool allowStagger )
{
	if ( IsPlayingDeathAnimation() || !attacker || !attacker->IsLunging() ) return;
	bool facing;
	if ( attacker->GetTeamNumber() == TEAM_INFECTED && attacker->GetZombieClass() == 3 && TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( attacker->m_isAttemptingToPounce ) facing = PinFacingPlayer( this, attacker, static_cast< float >( cos( static_cast< double >( versus_shove_hunter_fov_pouncing.GetFloat() ) * static_cast< double >( 0.5f ) * static_cast< double >( 0.017453292f ) ) ) );
		else facing = PinFacingPoint( this, attacker->WorldSpaceCenter(), static_cast< float >( cos( static_cast< double >( versus_shove_hunter_fov.GetFloat() ) * static_cast< double >( 0.5f ) * static_cast< double >( 0.017453292f ) ) ) );
	}
	else facing = PinFacingAimCone( this, attacker );
	if ( IsRecentlyShoving() && facing )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "player_shoved" );
		if ( event )
		{
			event->SetInt( "userid", attacker->GetUserID() );
			event->SetInt( "attacker", GetUserID() );
			gameeventmanager->FireEvent( event );
		}
		Vector direction = GetAbsOrigin() - attacker->GetAbsOrigin();
		VectorNormalize( direction );
		attacker->MeleeRepel( this, direction );
		return;
	}
	StopUseAction();
	StopBeingRevived( true );
	EndReviving( true );
	ClearUseEntity();
	SetPushEntity( NULL );
	if ( CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() ) ) weapon->SuppressHelpingHand( 0.0f );
	if ( !m_hStaggerSource.Get() )
	{
		SpeakIfAllowed( "PlayerShoved" );
		if ( IsSurvivorPinTeam( this ) && !attacker->m_isAttemptingToPounce ) attacker->RecordSurvivorLungeShove( GetSurvivorCharacter() );
	}
	m_hStaggerSource = attacker;
	m_staggerSourceTimer.Start( 3.0f );
	if ( allowStagger )
	{
		const float minimum = survivor_min_lunge_stagger_speed.GetFloat();
		const float maximum = survivor_max_lunge_stagger_speed.GetFloat();
		float speed = attacker->GetAbsVelocity().Length();
		if ( speed > minimum )
		{
			if ( maximum <= speed ) speed = maximum;
			const float fraction = maximum > minimum ? speed / ( maximum - minimum ) : 1.0f;
			Knockdown( 1 );
			Vector direction = GetAbsOrigin() - attacker->GetAbsOrigin();
			VectorNormalize( direction );
			direction.z -= 0.1f;
			VectorNormalize( direction );
			m_staggerDir = direction;
			m_staggerDist = survivor_max_lunge_stagger_distance.GetFloat() * fraction;
			m_staggerStart = GetAbsOrigin();
			attacker->SetShoveForce( ( m_staggerDir.Get() * -1.5f ) * m_staggerDist );
			attacker->SetLaunchSource( this );
			attacker->SetGroundEntity( NULL );
		}
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "lunge_shove" );
	if ( event )
	{
		event->SetInt( "userid", attacker->GetUserID() );
		event->SetInt( "victim", GetUserID() );
		gameeventmanager->FireEvent( event );
	}
}

// S32E230: the victim-side writer of the ONE original 16048 IntervalTimer.
void CTerrorPlayer::OnPounced( CTerrorPlayer *attacker )
{
	const int character = GetSurvivorCharacter();
	if ( character >= 0 && character < 4 ) ++m_survivorPounces[character];
	m_pounceAttacker = attacker;
	m_pounceStartTimer.Start();
	HolsterPinWeapon( this );
	if ( CTerrorPlayer *jockey = ToTerrorPlayer( m_jockeyAttacker.Get() ) ) jockey->ReleaseJockey( NULL );
	if ( CTerrorPlayer *smoker = ToTerrorPlayer( m_tongueOwner.Get() ) ) smoker->ReleaseTongue( false );
	if ( m_carryAttacker.Get() ) ReleaseCarry( true, false, false );
	if ( CTerrorPlayer *charger = ToTerrorPlayer( m_pummelAttacker.Get() ) ) charger->ReleasePummel( true, NULL );
	EndReviving( false );
	StopBeingRevived( false );
	StopUseAction();
	StopBeingDragged();
	GetMusic()->OnPounced();
	ClearStagger();
	ClearTug();
	attacker->DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCE_ACQUIRE );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCED );
	QueuePinSpeech( "SurvivorWasPounced", this, ResponseTarget_t::ANY, NULL, this );
	RequestSurvivorBotAssistance();
	RumbleEffect( 0x2D, 0, 4 );
}

// S333C90: Hunter acquisition. In particular, there is no added victim-alive
// predicate here; the caller's native contact/delay path supplies its own gates.
void CTerrorPlayer::TryPounceVictim( CTerrorPlayer *victim )
{
	if ( survivor_no_pounce_or_hang.GetBool() || ( IsInCommentaryMode() && !victim->IsBot() ) || victim->m_pummelAttacker.Get() ) return;
	if ( TerrorGameRules()->IsSurvivalMode() || z_pounce_stumble_radius.GetFloat() > 0.0f )
	{
		UTIL_ScreenShake( GetAbsOrigin(), z_pounce_shake_amplitude.GetFloat(), 150.0f,
			z_pounce_shake_duration.GetFloat(), z_pounce_shake_radius.GetFloat(), SHAKE_START );
		StumblePounceBystanders( victim );
	}
	const int character = victim->GetSurvivorCharacter();
	if ( character >= 0 && character < 4 )
	{
		++m_survivorPounces[character];
		++m_totalSurvivorPounces;
		++m_checkpointPZPounces;
	}
	CDisablePredictionFiltering disablePredictionFiltering;
	ReleasePounce();
	m_pounceVictim = victim;
	m_pounceAttacker.GetForModify().Init( 0, 0 );
	m_isAttemptingToPounce = false;
	m_pounceLanded = true;
	m_pounceDamageRemaining = GetMaxHealth();
	UTIL_LogPrintf( "%s pounced on %s, %d damage interrupts\n", GetPlayerName(), victim->GetPlayerName(), m_pounceDamageRemaining );
	victim->OnPounced( this );
	m_pounceDamageTimer.Start( z_pounce_damage_delay.GetFloat() );
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_POUNCE_LAND );
	if ( GetActiveWeapon() ) GetActiveWeapon()->SendWeaponAnim( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_VM_POUNCE ) );
	EmitSound( "HunterZombie.Pounce.Hit" );
	m_vocalizeTimer.Start( RandomFloat( 3.0f, 4.5f ) );
	IGameEvent *event = gameeventmanager->CreateEvent( "lunge_pounce" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		event->SetFloat( "distance", ( m_pounceStartPosition.Get() - GetAbsOrigin() ).Length2D() );
	}
	if ( TerrorGameRules()->IsPlayerControlledZombies() )
	{
		const Vector delta = m_pounceStartPosition.Get() - GetAbsOrigin();
		const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		float damage = 1.0f;
		if ( distance > 300.0f )
		{
			damage = distance < 1000.0f ? ( ( distance - 300.0f ) * 0.0014285714f ) * z_hunter_max_pounce_bonus_damage.GetFloat() + 1.0f : z_hunter_max_pounce_bonus_damage.GetFloat() + 1.0f;
			UTIL_LogPrintf( "Pounce by %s dealt %0.1f damage from a 2d distance of %.0f\n", GetPlayerName(), damage, distance );
			SendPZDamageMessage( 12, victim, this, NULL, true, (int)damage );
			UpdateHighestDamagePounce( (int)damage );
		}
		if ( damage > 0.0f )
		{
			CTakeDamageInfo info( this, this, damage, DMG_CRUSH );
			CalculateMeleeDamageForce( &info, GetAbsVelocity(), Weapon_ShootPosition() );
			victim->TakeDamage( info );
			HunterBlood( this, victim, true );
			AddBloodToHands();
			AddTerrorSplatter( 1, 0.0f, 0 );
			victim->AddTerrorSplatter( 1, 0.0f, 0 );
			if ( !victim->IsAlive() )
			{
				MakeHandsBloody();
				ReleasePounce();
			}
			if ( event ) event->SetInt( "damage", (int)damage );
		}
	}
	if ( event ) gameeventmanager->FireEvent( event );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

extern ConVar survivor_incap_health;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S3347C0. THIS is the Jockey, victim is the actual CLeap::OnTouch target.
void CTerrorPlayer::TryJockeyRide( CTerrorPlayer *victim )
{
	if ( survivor_no_pounce_or_hang.GetBool() || ( IsInCommentaryMode() && !victim->IsBot() ) ||
		victim->m_pounceAttacker.Get() || victim->IsPummelVictim() )
		return;

	// S33483C..S3348DA. The native null test is only on this repel path;
	// the earlier native victim accesses deliberately have no null guard.
	if ( victim && victim->IsRecentlyShoving() )
	{
		const float cone = (float)cos( (double)versus_shove_jockey_fov_leaping.GetFloat() * 0.5 * (double)0.017453292f );
		if ( PinFacingPlayer( victim, this, cone ) )
		{
			const Vector victimOrigin = victim->GetAbsOrigin();
			Vector direction = GetAbsOrigin() - victimOrigin;
			VectorNormalize( direction );
			MeleeRepel( victim, direction );
			return;
		}
	}
	if ( victim->m_jockeyAttacker.Get() )
	{
		if ( victim->m_jockeyAttacker.Get() == this ) return;
		static_cast< CTerrorPlayer * >( victim->m_jockeyAttacker.Get() )->ReleaseJockey( NULL );
	}

	// S334907..S334913 is FLDZ: the original starts ZERO, not the declared
	// but unused z_jockey_ride_damage_delay cvar. Do not repair that omission.
	m_jockeyDamageTimer.Start( 0.0f );
	SetJockeyVictim( victim );
	SetJockeyAttacker( NULL );
	victim->SetJockeyVictim( NULL );
	victim->SetJockeyAttacker( this );
	EmitInfectedSound( "JockeyZombie.Ride", -1.0f, 0.0f );
	HolsterPinWeapon( victim );
	if ( victim->IsTongueVictim() ) victim->GetTongueOwner()->ReleaseTongue( false );
	victim->EndReviving( false );
	victim->StopBeingRevived( false );
	victim->StopUseAction();
	victim->ClearUseEntity();
	victim->SetPushEntity( NULL );
	if ( victim->GetActiveWeapon() )
		static_cast< CTerrorWeapon * >( victim->GetActiveWeapon() )->SuppressHelpingHand( 0.0f );
	victim->StopBeingDragged();
	victim->GetMusic()->OnJockeyRide();
	victim->ClearStagger();
	victim->ClearTug();
	victim->RequestSurvivorBotAssistance();
	SetModelScale( GetSurvivorCharacterScale( victim->GetSurvivorCharacter() ), 0.0f );
	EmitSound( "Zombie.Punch" );
	StumblePounceBystanders( victim );
	m_jockeyControlPhase = 0.0f;

	// S334A7B..S334B1E shares one criteria set across both queues. The first
	// ENTITY query is due at curtime+0 with a NULL issuer, unlike the second
	// ANY query's absolute0 deadline and victim issuer.
	AI_CriteriaSet criteria;
	criteria.AppendCriteria( "subject", victim->GetResponseCharacterName() );
	if ( z_jockey_pounced_surv_bark.GetBool() )
	{
		ResponseQueueManager().Queue( "SurvivorJockeyed", criteria, gpGlobals->curtime + 0.0f,
			ResponseTarget_t( ResponseTarget_t::ENTITY, victim ), NULL );
		ResponseQueueManager().Queue( "SurvivorJockeyedOther", criteria, 0.0f,
			ResponseTarget_t( ResponseTarget_t::ANY, NULL ), victim );
	}
	else
	{
		ResponseQueueManager().Queue( "SurvivorJockeyed", criteria, 0.0f,
			ResponseTarget_t( ResponseTarget_t::ANY, NULL ), this );
	}
	GetTerrorAnimState()->DoTerrorAnimationEvent(TERROR_ANIMEVENT_JOCKEY_RIDE, 0);
	victim->GetTerrorAnimState()->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JOCKEY_RIDDEN, 0 );
	m_jockeyMountDuration = 0.4f;
	m_jockeyMountTimer.Start( 0.4f );
	m_jockeyRideStartTime = gpGlobals->curtime;
	IGameEvent *event = gameeventmanager->CreateEvent( "jockey_ride" );
	if ( event )
	{
		event->SetInt( "userid", GetUserID() );
		event->SetInt( "victim", victim->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
	victim->RumbleEffect( 0x21, 0, 4 );
	victim->RumbleEffect( 0x27, 0, 2 );
	RumbleEffect( 0x21, 0, 4 );
	RumbleEffect( 0x27, 0, 2 );
}

// S347B70. Runs on both pending-contact SURVIVOR and pin-owner Hunter paths.
void CTerrorPlayer::UpdatePounce()
{
	if ( IsSurvivorPinTeam( this ) && m_pendingPounceTime > 0.0f &&
		gpGlobals->curtime > m_pendingPounceTime )
	{
		CTerrorPlayer *attacker = static_cast< CTerrorPlayer * >( m_pendingPounceAttacker.Get() );
		if ( attacker && attacker->GetTeamNumber() == TEAM_INFECTED && attacker->GetZombieClass() == 3 &&
			attacker->IsAlive() && !attacker->IsGhost() && !m_isHangingFromLedge &&
			!attacker->m_pounceVictim.Get() && !attacker->IsStaggeringOrPending() )
		{
			OnLungedBy( attacker, false );
			if ( attacker->IsAlive() && !attacker->IsStaggeringOrPending() )
				attacker->TryPounceVictim( this );
		}
		m_pendingPounceAttacker = NULL;
		m_pendingPounceTime = 0.0f;
	}

	CTerrorPlayer *victim = static_cast< CTerrorPlayer * >( m_pounceVictim.Get() );
	if ( m_pounceAttacker.Get() ) SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );
	if ( IsAlive() && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 )
	{
		if ( !victim )
		{
			if ( ( m_nButtons & IN_DUCK ) && !IsGhost() && !IsPouncing() )
			{
				bool warningTarget = false;
				// S304FB0/S304F70 use the actual enemy-seen timer12592. Its
				// non-spilled age comparison is wider, not the float API age.
				if ( m_enemySeenInterval.GetStartTime() > 0.0f &&
					(double)gpGlobals->curtime - (double)m_enemySeenInterval.GetStartTime() < 1.0 )
				{
					const float silenceRange = z_pounce_silence_range.GetFloat();
					Vector forward;
					AngleVectors( EyeAngles(), &forward );
					for ( int i = 0; i < m_visibleEnemies.Count(); ++i )
					{
						CBaseCombatCharacter *candidate = m_visibleEnemies[i].Get();
						if ( !candidate || candidate->GetTeamNumber() != TEAM_SURVIVOR || !candidate->IsAlive() )
							continue;
						const Vector eye = EyePosition();
						const Vector end = eye + forward * 100.0f;
						Vector closest;
						CalcClosestPointOnLineSegment( candidate->WorldSpaceCenter(), eye, end, closest, NULL );
						const Vector candidateOrigin = candidate->GetAbsOrigin();
						Vector top = candidateOrigin;
						top.z += candidate->CollisionProp()->OBBMaxs().z;
						Vector target;
						CalcClosestPointOnLineSegment( closest, candidateOrigin, top, target, NULL );
						Vector direction = target - EyePosition();
						const float distance = VectorNormalize( direction );
						if ( distance > silenceRange ) continue;
						const float dot = ( direction.y * forward.y + direction.x * forward.x ) + direction.z * forward.z;
						// COMISS(0.8,dot)/JBE accepts unordered, unlike dot>=0.8.
						if ( !( 0.8f > dot ) )
						{
							warningTarget = true;
							break;
						}
					}
				}
				if ( warningTarget && ( m_vocalizeTimer.IsElapsed() || m_pounceGrowlTimer.IsElapsed() ) )
				{
					const float duration = RandomFloat( 3.0f, 4.5f );
					EmitInfectedSound( "HunterZombie.Warn", duration, duration );
					m_pounceGrowlTimer.Start( m_vocalizeTimer.GetRemainingTime() );
				}
				else if ( m_vocalizeTimer.IsElapsed() )
				{
					const float duration = RandomFloat( 2.0f, 3.5f );
					EmitInfectedSound( "HunterZombie.Growl", duration, duration );
				}
			}
			// The cached no-victim path never falls into pin processing even
			// when a sound/perception callback changes some other live state.
			if ( m_pounceLanded ) ReleasePounce();
			return;
		}
		if ( gpGlobals->curtime >= m_vocalizeTimer.m_timestamp.Get() )
		{
			if ( m_pounceLanded ) EmitInfectedSound( "HunterZombie.Pounce.shred", 0.0f, 0.0f );
			else EmitInfectedSound( "HunterZombie.Warn", RandomFloat( 3.0f, 4.5f ), 0.0f );
		}
	}
	if ( !victim )
	{
		if ( m_pounceLanded ) ReleasePounce();
		return;
	}
	if ( GetMoveType() == MOVETYPE_NOCLIP )
	{
		ReleasePounce();
		return;
	}
	const Vector victimOrigin = victim->GetAbsOrigin();
	const Vector origin = GetAbsOrigin();
	Vector position;
	position.x = ( origin.x - victimOrigin.x ) * 0.8f + victimOrigin.x;
	position.y = ( origin.y - victimOrigin.y ) * 0.8f + victimOrigin.y;
	position.z = ( origin.z - victimOrigin.z ) * 0.8f + victimOrigin.z;
	SetAbsOrigin( position );
	SetAbsVelocity( vec3_origin );
	victim->Knockdown( 1 ); // S34819A ECX=EDI (victim), NOT the Hunter.
	if ( !( gpGlobals->curtime >= m_pounceDamageTimer.m_timestamp.Get() ) ) return;
	const Vector victimEyes = victim->EyePosition();
	const Vector attackPosition = Weapon_ShootPosition();
	const Vector direction = attackPosition - victimEyes;
	if ( direction.y * direction.y + direction.x * direction.x > 10000.0f )
	{
		ReleasePounce();
		return;
	}
	// Unlike the Jockey tick, Hunter restarts its damage clock BEFORE damage.
	m_pounceDamageTimer.Start( z_pounce_damage_interval.GetFloat() );
	const bool wasIncapacitated = victim->IsIncapacitated();
	float damage = z_pounce_damage.GetFloat();
	const int difficulty = TerrorGameRules()->GetDifficultyLevel();
	if ( difficulty == 2 ) damage *= 2.0f;
	else if ( difficulty == 3 ) damage *= 3.0f;
	if ( victim->IsIncapacitated() )
	{
		const float incapHealth = survivor_incap_health.GetFloat();
		const int maxHealth = victim->GetMaxHealth();
		damage = ( incapHealth / (float)maxHealth ) * damage;
	}
	CTakeDamageInfo info( this, this, damage, DMG_CLUB, 0 );
	CalculateMeleeDamageForce( &info, direction, Weapon_ShootPosition(), 1.0f );
	victim->TakeDamage( info );
	if ( wasIncapacitated != victim->IsIncapacitated() ) victim->m_vocalizeTimer.Start( 2.0f );
	HunterBlood( this, victim, false );
	AddBloodToHands();
	AddTerrorSplatter( 1, 0.0f, 0 );
	victim->AddTerrorSplatter( 1, 0.0f, 0 );
	if ( !victim->IsAlive() || victim->m_isHangingFromLedge )
	{
		MakeHandsBloody();
		ReleasePounce();
	}
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// This TU has explicit native unordered branches and float spill boundaries.
// The inherited Source projects otherwise select /fp:fast.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

extern ConVar survivor_incap_health;

// S59CD10 / S59CD70 / S59CDA0 / S59CDD0. Server-only FCVAR_CHEAT.
ConVar z_jockey_stagger_amount( "z_jockey_stagger_amount", "0.0", 0x00004004 );
ConVar z_jockey_control_variance( "z_jockey_control_variance", "0.7", 0x00004004 );
ConVar z_jockey_control_min( "z_jockey_control_min", "0.8", 0x00004004 );
ConVar z_jockey_control_max( "z_jockey_control_max", "0.8", 0x00004004 );

// S3485B0. Called unconditionally from PostThink, before the scheduled alive
// update. This is both the victim's move-type repair and the rider's update.
void CTerrorPlayer::UpdateJockeyRide()
{
	if ( m_jockeyAttacker.Get() && !m_isHangingFromLedge )
		SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );

	// The original resolves the handle, without IsPlayer/class/alive/pair gates.
	CTerrorPlayer *victim = static_cast< CTerrorPlayer * >( m_jockeyVictim.Get() );
	if ( victim )
	{
		if ( victim->IsIncapacitated() )
		{
			ReleaseJockey( NULL );
			return;
		}

		const Vector victimOrigin = victim->GetAbsOrigin();
		Vector origin = GetAbsOrigin();
		const float mountDeadline = m_jockeyMountTimer.m_timestamp.Get();
		// S3486CC/JBE treats an unordered deadline as expired. Do not express
		// this as deadline <= 0, or use a HasStarted/IsElapsed conjunction.
		if ( !( mountDeadline > 0.0f ) || gpGlobals->curtime >= mountDeadline )
		{
			if ( !m_isFirmlyAttached )
			{
				m_isFirmlyAttached = true;
				SetAbsOrigin( victimOrigin );
				SetParent( victim, -1 );
				SetMoveType( MOVETYPE_NONE, MOVECOLLIDE_DEFAULT );
			}
		}
		else
		{
			float fraction = 1.0f;
			if ( m_jockeyMountDuration != 0.0f )
			{
				const float remaining = mountDeadline - gpGlobals->curtime;
				fraction = 1.0f - remaining / m_jockeyMountDuration;
				if ( fraction < 0.0f ) fraction = 0.0f;
				else if ( fraction > 1.0f ) fraction = 1.0f;
			}
			origin.x = ( victimOrigin.x - origin.x ) * fraction + origin.x;
			origin.y = ( victimOrigin.y - origin.y ) * fraction + origin.y;
			origin.z = ( victimOrigin.z - origin.z ) * fraction + origin.z;
			SetAbsOrigin( origin );
			if ( m_isFirmlyAttached ) m_isFirmlyAttached = false;
		}

		// S34880E..S3488C9. Sample each cvar once, then consume X, Y, control
		// RNG in that order, including frames on which no damage is due.
		const float staggerSpeed = z_jockey_stagger_speed.GetFloat();
		const float controlSpeed = z_jockey_speed_blend.GetFloat();
		const float staggerMax = staggerSpeed * 2.0f;
		m_jockeyStaggerPhaseX = (float)( (double)RandomFloat( staggerSpeed, staggerMax ) * (double)gpGlobals->frametime + (double)m_jockeyStaggerPhaseX );
		m_jockeyStaggerPhaseY = (float)( (double)RandomFloat( staggerSpeed, staggerMax ) * (double)gpGlobals->frametime + (double)m_jockeyStaggerPhaseY );
		const float controlMax = controlSpeed * 2.0f;
		m_jockeyControlPhase = (float)( (double)RandomFloat( controlSpeed, controlMax ) * (double)gpGlobals->frametime + (double)m_jockeyControlPhase );

		// S3488D9/JB also returns for an unordered time/deadline.
		if ( !( gpGlobals->curtime >= m_jockeyDamageTimer.m_timestamp.Get() ) )
			return;

		const bool wasIncapacitated = victim->IsIncapacitated();
		float damage = z_jockey_ride_damage.GetFloat();
		const int difficulty = TerrorGameRules()->GetDifficultyLevel(); // S350BF0
		if ( difficulty == 2 ) damage *= 2.0f;
		else if ( difficulty == 3 ) damage *= 3.0f;

		// Deliberately retain the repeated virtual query, despite the early
		// incap release above; do not fold it into wasIncapacitated.
		if ( victim->IsIncapacitated() )
		{
			const float incapHealth = survivor_incap_health.GetFloat();
			const int maxHealth = victim->GetMaxHealth();
			damage = ( incapHealth / (float)maxHealth ) * damage;
		}
		const Vector victimEyes = victim->EyePosition();
		const Vector attackPosition = Weapon_ShootPosition();
		const Vector direction = attackPosition - victimEyes;
		CTakeDamageInfo info( this, this, damage, DMG_CLUB, 0 );
		CalculateMeleeDamageForce( &info, direction, Weapon_ShootPosition(), 1.0f );
		victim->TakeDamage( info );
		if ( wasIncapacitated != victim->IsIncapacitated() )
			victim->m_vocalizeTimer.Start( 2.0f );
		m_jockeyDamageTimer.Start( z_jockey_ride_damage_interval.GetFloat() );
		return;
	}

	// S348A56. This is the ONLY no-victim path, not an alive gate for riding.
	if ( IsAlive() && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 5 && !IsGhost() &&
		gpGlobals->curtime >= m_vocalizeTimer.m_timestamp.Get() )
	{
		const float duration = RandomFloat( 2.0f, 3.5f );
		EmitInfectedSound( "JockeyZombie.Recognize", duration, duration );
	}
}

// S2CA4A0. THIS is the ridden survivor; raw wishes and phases belong to the
// resolved attacker. This consumer is server-only, unlike the paired recv pose.
void CTerrorPlayer::ApplyJockeyControl( float *forwardMove, float *sideMove )
{
	CTerrorPlayer *jockey = static_cast< CTerrorPlayer * >( m_jockeyAttacker.Get() );
	if ( !jockey ) return;

	// S2CA4F7..S2CA558. Native degree constant bits 0x3C8EFA35; sine and
	// cosine are independently rounded to float before command rotation.
	const double yaw = (double)EyeAngles().y * (double)0.017453292f;
	const float sine = (float)sin( yaw );
	const float cosine = (float)cos( yaw );
	float jockeySide = jockey->m_jockeyRawForwardMove * sine - jockey->m_jockeyRawSideMove * cosine;
	float jockeyForward = jockey->m_jockeyRawSideMove * sine + jockey->m_jockeyRawForwardMove * cosine;
	if ( jockey->IsBot() )
	{
		jockeyForward = jockey->m_jockeyRawForwardMove;
		jockeySide = jockey->m_jockeyRawSideMove;
	}

	// Preserve the wider square/sum/root chain and its separate float spill.
	const double jockeyLength = sqrt( (double)jockeySide * (double)jockeySide + (double)jockeyForward * (double)jockeyForward );
	const float jockeyLengthFloat = (float)jockeyLength;
	if ( jockeyLength > (double)0.001f )
	{
		const float inverseLength = 1.0f / jockeyLengthFloat;
		jockeyForward = inverseLength * jockeyForward;
		jockeySide = inverseLength * jockeySide;
	}

	// S2CA5BB..S2CA648. Read variance and health before GetMaxHealth; min/max
	// afterwards. No denominator, finite-value, incap or reciprocal-pin repair.
	const float oscillation = (float)sin( (double)jockey->m_jockeyControlPhase );
	const float variance = z_jockey_control_variance.GetFloat();
	const float health = (float)GetHealth();
	const int maxHealth = GetMaxHealth();
	const float minimum = z_jockey_control_min.GetFloat();
	const float maximum = z_jockey_control_max.GetFloat();
	float control = ( maximum - minimum ) * ( 1.0f - health / (float)maxHealth ) + minimum;
	control += ( variance * oscillation ) * 0.5f;
	if ( control > 1.0f ) control = 1.0f;
	else if ( control < 0.0f ) control = 0.0f;

	// S2CA660..S2CA6B7 uses the wider scaled inputs for the length, but the
	// rounded float copies for the normalized coordinates (bits 0x3B83126F).
	const double survivorForwardWide = (double)*forwardMove * (double)0.0040000002f;
	const float survivorForwardFloat = (float)survivorForwardWide;
	const double survivorSideWide = (double)*sideMove * (double)0.0040000002f;
	const float survivorSideFloat = (float)survivorSideWide;
	const double survivorLength = sqrt( survivorForwardWide * survivorForwardWide + survivorSideWide * survivorSideWide );
	const float survivorLengthFloat = (float)survivorLength;
	float survivorForward = survivorForwardFloat;
	float survivorSide = survivorSideFloat;
	if ( survivorLength > (double)0.001f )
	{
		const float inverseLength = 1.0f / survivorLengthFloat;
		survivorForward = inverseLength * survivorForwardFloat;
		survivorSide = inverseLength * survivorSideFloat;
	}

	const float survivorControl = 1.0f - control;
	*forwardMove = survivorControl * survivorForward + control * jockeyForward;
	*sideMove = survivorControl * survivorSide + control * jockeySide;
	*forwardMove = *forwardMove * 250.0f;
	*sideMove = *sideMove * 250.0f;

	// S2CA703..S2CA72A keeps cos*amount+command wide until the final store.
	// Amount is read again for Y, as in the native code; phases are not wrapped.
	const double staggerX = cos( (double)jockey->m_jockeyStaggerPhaseX );
	const float amountX = z_jockey_stagger_amount.GetFloat();
	*forwardMove = (float)( staggerX * (double)amountX + (double)*forwardMove );
	const double staggerY = cos( (double)jockey->m_jockeyStaggerPhaseY );
	const float amountY = z_jockey_stagger_amount.GetFloat();
	*sideMove = (float)( staggerY * (double)amountY + (double)*sideMove );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

bool CTerrorPlayer::IsLungePushable() const // S2C83D0, actual virtual574.
{
	return ( GetTeamNumber() != TEAM_INFECTED || GetZombieClass() == 0 ) &&
		!CanBeRevived() &&
		LookupTerrorOriginalActivity( GetTerrorActivity() ) != 667 &&
		LookupTerrorOriginalActivity( GetTerrorActivity() ) != 669 &&
		!IsHangingFromLedge() && !IsInvulnerable() && !GetSurvivorPosition();
}

#include "l4d2/player/terrorplayer_hunter_jockey_console_server_callbacks.inc"
