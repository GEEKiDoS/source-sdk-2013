//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/infected/base_ability.h"
#include "activitylist.h"
#include "util.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "movevars_shared.h"
#include "functorutils.h"
#include "ndebugoverlay.h"

#include "l4d2/player/terrorplayer_movement_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar survivor_max_tongue_stagger_duration( "survivor_max_tongue_stagger_duration", "1.5", 0x00004004 );
static ConVar survivor_max_tongue_stagger_distance( "survivor_max_tongue_stagger_distance", "200", 0x00004004 );
static ConVar z_max_stagger_duration( "z_max_stagger_duration", "6", 0x00004004, "Max time a PZ staggers when bashed by a survivor." );
static ConVar z_tank_max_stagger_distance( "z_tank_max_stagger_distance", "400", 0x00004004, "Max distance a Tank staggers when hurt by a grenade." );
static ConVar survivor_knockdown_roll( "survivor_knockdown_roll", "-20", 0x00004004 );
ConVar z_exploding_shove_min( "z_exploding_shove_min", "4", 0x00004004 );
ConVar z_exploding_shove_max( "z_exploding_shove_max", "5", 0x00004004 );
static ConVar z_exploding_shove_interval( "z_exploding_shove_interval", "10", 0x00004004 );
extern ConVar z_pounce_crouch_delay;
static ConVar z_tank_max_stagger_duration( "z_tank_max_stagger_duration", "6", 0x00004004,
	"Max time a Tank staggers when hit by a grenade." );
static ConVar z_tank_stagger_fade_duration( "z_tank_stagger_fade_duration", "3", 0x00004004,
	"How long the screen fade lasts when a Tank is hit by a grenade." );
static ConVar z_tank_stagger_fade_alpha( "z_tank_stagger_fade_alpha", "192", 0x00004004,
	"How opaque the screen fade is when a Tank is hit by a grenade." );
static ConVar z_max_hunter_pounce_stagger_duration( "z_max_hunter_pounce_stagger_duration", "0.5", 0x00004004,
	"Max time a PZ staggers when bashed by a survivor that was pounced by said hunter." );
extern ConVar z_pounce_stumble_force;
extern ConVar z_pounce_stumble_radius;

ConVar z_debug_ledges( "z_debug_ledges", "0", 0x00004004 );
static ConVar z_debug_falling_damage( "z_debug_falling_damage", "0", 0x00004004 );
ConVar z_grab_ledges_solo( "z_grab_ledges_solo", "0", 0x00004004,
	"For testing - grab ledges even if you're the last survivor" );
static ConVar z_grab_force( "z_grab_force", "0", 0x00004004,
	"For testing - always grab ledges regardless of estimated falling damage" );
extern ConVar survivor_incap_max_fall_damage;

namespace
{
Activity DisplacementActivity( const char *name )
{
	int activity = ActivityList_IndexForName( name );
	if ( activity == ACT_INVALID )
		activity = ActivityList_RegisterPrivateActivity( name );
	return static_cast< Activity >( activity );
}
}

// S:0x2C7950. These are activities, not m_lifeState or an invented dying flag.
bool CTerrorPlayer::IsPlayingDeathAnimation() const
{
	static Activity stand = DisplacementActivity( "ACT_TERROR_DIE_FROM_STAND" );
	static Activity running = DisplacementActivity( "ACT_TERROR_DIE_WHILE_RUNNING" );
	Activity activity = GetTerrorAnimState()->GetCurrentMainActivity();
	return activity == stand || activity == running;
}

// S:0x2C84B0. The animation system's S:0x3012C0 timer-only predicate is
// deliberately different: movement also observes the distance already moved.
const Vector *CTerrorPlayer::IsStaggering() const
{
	if ( gpGlobals->curtime >= m_staggerTimer.m_timestamp.Get() )
		return NULL;
	if ( ( GetAbsOrigin() - m_staggerStart.Get() ).Length() > m_staggerDist )
		return NULL;
	return &m_staggerDir.Get();
}

// S:0x2C9FF0. The pending value is animation event data, not an EHANDLE.
bool CTerrorPlayer::IsStaggeringOrPending() const
{
	return m_pendingStaggerEventData != -1 || IsStaggering() != NULL;
}


// S:0x2C8000 (the original script exposes this as IsGettingUp).
bool CTerrorPlayer::IsKnockedDown() const
{
	if ( m_knockdownReason == 0 )
		return false;

	float duration = 2.0f;
	if ( m_knockdownReason == 2 )
		duration = 1.5f;
	if ( m_knockdownReason == 3 )
		duration = 2.5f;

	static Activity pouncedToStand = DisplacementActivity( "ACT_TERROR_POUNCED_TO_STAND" );
	return !IsIncapacitated() &&
		( ( !m_pounceAttacker.Get() && !m_pounceVictim.Get() &&
			m_knockdownTimer.GetStartTime() > 0.0f && m_knockdownTimer.GetElapsedTime() < duration ) ||
			GetTerrorAnimState()->GetCurrentMainActivity() == pouncedToStand );
}

// S:0x311DF0. The direct animation-state call does not emit another network event.
void CTerrorPlayer::ClearStagger()
{
	m_staggerTimer.Invalidate();
	m_staggerDir = vec3_origin;
	m_staggerDist = 0.0f;
	m_staggerStart = vec3_origin;
	GetTerrorAnimState()->DoTerrorAnimationEvent(TERROR_ANIMEVENT_CLEAR_STAGGER, 0);
	m_stunTimer.Invalidate();
}

// S:0x312A80. Clearing the handle does not clear the last push distance.
void CTerrorPlayer::SetPushEntity( CBaseEntity *entity )
{
	m_pushEntity = entity;
	if ( entity )
		m_pushDistance = 32.0f;
}

// S:0x3085C0. This provenance is independent of stagger/tongue-release credit.
void CTerrorPlayer::SetLaunchSource( CBaseEntity *entity )
{
	m_hLaunchSource = entity;
	if ( entity )
		m_launchSourceTimer.Start( 1.0f );
	else
		m_launchSourceTimer.Invalidate();
}

// S3033D0 and S303430 are distinct timed provenance readers. The caller,
// not either reader, chooses player/tongue/current-displacement priority.
CBaseEntity *CTerrorPlayer::GetLedgeCauser() const
{
	if ( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_staggerSourceTimer.m_timestamp.Get() ) )
		return NULL;
	return m_hStaggerSource.Get();
}

CBaseEntity *CTerrorPlayer::GetTongueReleaseSource() const
{
	if ( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_tongueReleaseTimer.m_timestamp.Get() ) )
		return NULL;
	return m_hTongueReleaseOwner.Get();
}

// S:0x303490. This is not GetLedgeCauser: current tongue ownership has
// priority, and launch credit survives its timer while the victim is airborne.
CBaseEntity *CTerrorPlayer::GetDisplacingEntity() const
{
	if ( m_tongueOwner.Get() )
		return m_tongueOwner.Get();
	if ( GetTongueReleaseSource() )
		return GetTongueReleaseSource();
	if ( GetLedgeCauser() )
		return GetLedgeCauser();
	if ( ( m_launchSourceTimer.m_timestamp.Get() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) < static_cast< double >( m_launchSourceTimer.m_timestamp.Get() ) ) ||
		( m_hLaunchSource.Get() && !GetGroundEntity() ) )
		return m_hLaunchSource.Get();
	return NULL;
}

// S:0x32CE80. Life owns reciprocal pin releases; the player owns displacement.
void CTerrorPlayer::StaggerPlayer( CBaseEntity *source, const Vector *sourcePosition )
{
	if ( IsPlayingDeathAnimation() || IsGhost() || IsIncapacitated() )
		return;

	EndReviving( true );
	ClearUseEntity();
	SetPushEntity( NULL );

	CTerrorPlayer *rescuer = ToTerrorPlayer( source );
	if ( m_jockeyVictim.Get() )
		ReleaseJockey( rescuer );
	if ( CTerrorPlayer *jockey = ToTerrorPlayer( m_jockeyAttacker.Get() ) )
		jockey->ReleaseJockey( rescuer );
	if ( m_pounceVictim.Get() )
		ReleasePounce();
	if ( CTerrorPlayer *hunter = ToTerrorPlayer( m_pounceAttacker.Get() ) )
		hunter->ReleasePounce();
	ReleasePummel( true, rescuer );

	if ( m_tongueVictim.Get() )
		ReleaseTongue( false );
	if ( CTerrorPlayer *smoker = ToTerrorPlayer( m_tongueOwner.Get() ) )
		smoker->ReleaseTongue( false );

	CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() );
	if ( weapon )
		weapon->SuppressHelpingHand();

	if ( !m_hStaggerSource.Get() && ( GetTeamNumber() == TEAM_SURVIVOR || GetTeamNumber() == 4 ) )
		SpeakIfAllowed( "PlayerShoved" );
	m_hStaggerSource = source ? source->MyCombatCharacterPointer() : NULL;
	m_staggerSourceTimer.Start( 3.0f );

	// The original requires a source or an explicit source position. It has no
	// world-origin or player-forward fallback for two null arguments.
	Vector direction = GetAbsOrigin() - ( sourcePosition ? *sourcePosition : source->GetAbsOrigin() );
	if ( source == this && !sourcePosition )
		EyeVectors( &direction );
	VectorNormalize( direction );
	QAngle angles;
	VectorAngles( direction, angles );
	float relativeYaw = AngleNormalize( GetAbsAngles().y - angles.y );
	int directionIndex;
	if ( relativeYaw >= -45.0f && relativeYaw <= 45.0f )
		directionIndex = 0;
	else if ( relativeYaw > 45.0f && relativeYaw < 135.0f )
		directionIndex = 3;
	else if ( relativeYaw > -135.0f && relativeYaw < -45.0f )
		directionIndex = 2;
	else
		directionIndex = 1;
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_STAGGER, directionIndex );

	if ( GetTeamNumber() == TEAM_INFECTED )
	{
		m_staggerTimer.Start( z_max_stagger_duration.GetFloat() );
		m_staggerDist = z_tank_max_stagger_distance.GetFloat();
	}
	else
	{
		m_staggerTimer.Start( survivor_max_tongue_stagger_duration.GetFloat() );
		m_staggerDist = survivor_max_tongue_stagger_distance.GetFloat();
	}
	m_staggerDir = direction;
	m_staggerStart = GetAbsOrigin();
}

// S:0x316050. The impulse is additive, including any pre-existing velocity.
void CTerrorPlayer::Launch( const Vector &velocity, TerrorPlayerAnimEvent event, CBaseEntity *source, float externalViewDuration )
{
	SetGroundEntity( NULL );
	Vector origin = GetAbsOrigin();
	trace_t trace;
	UTIL_TraceEntity( this, origin, origin + Vector( 0.0f, 0.0f, 8.0f ), PlayerSolidMask(), this,
		COLLISION_GROUP_PLAYER_MOVEMENT, &trace );
	if ( trace.fraction == 1.0f && !trace.startsolid && !trace.allsolid )
		SetAbsOrigin( trace.endpos );

	ApplyAbsVelocityImpulse( velocity );
	SetLaunchSource( source );
	DoTerrorAnimationEvent( event, 0 );
	ClearUseEntity();
	if ( CTerrorPlayer *smoker = ToTerrorPlayer( m_tongueOwner.Get() ) )
		smoker->ReleaseTongue( false );
	if ( CTerrorPlayer *jockey = ToTerrorPlayer( m_jockeyAttacker.Get() ) )
		jockey->ReleaseJockey( NULL );
	if ( externalViewDuration > 0.0f )
		m_TimeForceExternalView = gpGlobals->curtime + externalViewDuration;
}

// S:0x316EB0. Reason and timer are independent of the incap/life state.
void CTerrorPlayer::Knockdown( int reason )
{
	m_knockdownTimer.Start();
	m_knockdownReason = reason;
	ClearUseEntity();
	SetPushEntity( NULL );
	if ( reason == 2 )
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_HIT_BY_TANK, 0 );
	if ( reason != 1 && reason != 3 )
		SetAbsVelocity( vec3_origin );
	if ( reason == 2 || reason == 3 )
	{
		QAngle punch = GetPunchAngle();
		punch.z = survivor_knockdown_roll.GetFloat();
		SetPunchAngle( punch );
	}
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S2A7B60 predicts DAMAGE with actual ballistic hull traces, not fall height.
double CTerrorPlayer::GetPredictedFallDamage()
{
	if ( !IsAlive() || GetGroundEntity() || GetMoveType() == MOVETYPE_LADDER )
		return 0.0;
	if ( z_grab_force.GetBool() )
		return 9999999.0;

	const Vector mins = GetPlayerMins();
	const Vector maxs = GetPlayerMaxs();
	Vector position = GetAbsOrigin();
	Vector velocity = GetAbsVelocity() * 0.5f;
	float flGravityStep = sv_gravity.GetFloat() * 0.1f;
	if ( GetGravity() != 0.0f )
		flGravityStep *= GetGravity();
	const float flDamageLimit = static_cast< float >( survivor_incap_max_fall_damage.GetInt() + 50 );
	CTraceFilterNoNPCsOrPlayer filter( this, COLLISION_GROUP_PLAYER_MOVEMENT );
	const int nTeam = GetTeamNumber();
	const unsigned int nMask = ( nTeam == TEAM_SURVIVOR || nTeam == 4 ) ? 0x02014A0Bu : 0x0202520Bu;
	float flDamage = 0.0f;
	int nIterations = 0;
	if ( flDamageLimit > 0.0f )
	{
		for ( ; ; )
		{
			if ( nIterations >= 30 )
				break;
			++nIterations;
			const Vector destination = position + velocity * 0.1f;
			trace_t trace;
			UTIL_TraceHull( position, destination, mins, maxs, nMask, &filter, &trace );
			if ( trace.allsolid || trace.startsolid )
				return 0.0;
			if ( trace.fraction < 1.0f && trace.plane.normal.z > 0.7f )
				velocity.z += ( ( 1.0f - trace.fraction ) * flGravityStep ) * 0.5f;

			const double flRawDamage = CalculateTerrorFallDamage( -velocity.z - fall_speed_safe.GetFloat() );
			flDamage = static_cast< float >( flRawDamage );
			if ( flRawDamage < 0.0 )
				flDamage = 0.0f;
			if ( z_debug_falling_damage.GetBool() )
			{
				float flColorDamage = flDamage;
				if ( flColorDamage > 100.0f )
					flColorDamage = 100.0f;
				else if ( flColorDamage < 0.0f )
					flColorDamage = 0.0f;
				NDebugOverlay::Line( trace.startpos, trace.endpos,
					static_cast< int >( ( flColorDamage * 0.01f ) * 255.0f ),
					static_cast< int >( ( 1.0f - flColorDamage * 0.01f ) * 255.0f ),
					0, true, 30.0f );
			}
			if ( trace.fraction >= 1.0f )
			{
				velocity.z -= flGravityStep;
			}
			else if ( trace.plane.normal.z > 0.7f )
			{
				if ( z_debug_falling_damage.GetBool() )
					DevMsg( "Flat ground, %f damage, %d iterations\n", flDamage, nIterations );
				return flDamage;
			}
			else if ( velocity.x > -0.01f && velocity.x < 0.01f &&
				velocity.y > -0.01f && velocity.y < 0.01f )
			{
				Vector side, slope;
				CrossProduct( trace.plane.normal, Vector( 0.0f, 0.0f, 1.0f ), side );
				CrossProduct( side, trace.plane.normal, slope );
				const float flSpeed = static_cast< float >( sqrt(
					( static_cast< double >( velocity.x ) * velocity.x + static_cast< double >( velocity.y ) * velocity.y ) +
					static_cast< double >( velocity.z ) * velocity.z ) );
				velocity = -slope * flSpeed;
			}
			else
			{
				Vector clipped;
				PhysicsClipVelocity( velocity, trace.plane.normal, clipped, 1.5f );
				if ( clipped.x * velocity.x + clipped.y * velocity.y > 0.0f )
				{
					velocity = clipped;
				}
				else
				{
					velocity.x = 0.0f;
					velocity.y = 0.0f;
					if ( velocity.z >= clipped.z )
						velocity.z = clipped.z;
				}
			}
			position = trace.endpos;
			if ( flDamageLimit <= flDamage )
				break;
		}
	}
	if ( z_debug_falling_damage.GetBool() )
		DevMsg( "Too many traces, %f damage, %d iterations\n", flDamage, nIterations );
	return flDamage;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S34B1E0. The history is an ordered timestamp vector, not a shove counter.
void CTerrorPlayer::UpdateStagger()
{
	while ( m_staggerHistory.Count() &&
		gpGlobals->curtime - z_exploding_shove_interval.GetFloat() > m_staggerHistory[0] )
	{
		m_staggerHistory.Remove( 0 );
		if ( !m_staggerHistory.Count() )
			m_staggerThreshold = RandomInt( z_exploding_shove_min.GetInt(), z_exploding_shove_max.GetInt() );
	}

	if ( TerrorGameRules()->IsPlayerControlledZombies() &&
		m_staggerTimer.m_timestamp.Get() > 0.0f &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_staggerTimer.m_timestamp.Get() ) &&
		GetTeamNumber() == TEAM_INFECTED &&
		( GetZombieClass() == 1 || GetZombieClass() == 3 || GetZombieClass() == 5 ) )
	{
		SetCycle( 0.99000001f );
		ClearStagger();
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 )
		{
			if ( CBaseAbility *ability = m_customAbility.Get() )
				ability->SetRechargeTime( z_pounce_crouch_delay.GetFloat(), 0.0f );
		}
		return;
	}

	if ( IsAlive() && GetTeamNumber() == TEAM_INFECTED )
	{
		if ( m_pendingStaggerEventData == -1 || !GetGroundEntity() )
			return;
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_STAGGER, m_pendingStaggerEventData );
		GetTerrorAnimState()->ResetAnimationMovement();
		m_staggerTimer.Start( z_max_stagger_duration.GetFloat() );
	}
	m_pendingStaggerEventData = -1;
}

// S38C720. Only the actual Smoker/Boomer/Hunter class predicates qualify.
bool CTerrorPlayer::IsNearShoveLimit() const
{
	return m_staggerHistory.Count() >= m_staggerThreshold - 1 &&
		( ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 ) ||
		  ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 1 ) ||
		  ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 ) );
}

// S32BB00. Hunter knockoff and weapon melee use the same real shove history.
#pragma float_control( precise, on, push )
void CTerrorPlayer::MeleeRepel( CTerrorPlayer *source, const Vector &force )
{
	if ( IsPlayingDeathAnimation() )
		return;
	const bool bSourcePounced = source && source->IsBeingPounced();
	EndReviving( true );

	Vector shoveForce = force;
	if ( shoveForce.LengthSqr() > 10000.0f )
	{
		VectorNormalize( shoveForce );
		shoveForce *= 100.0f;
	}
	Vector direction( shoveForce.x, shoveForce.y, 0.0f );
	VectorNormalize( direction );
	m_staggerDir = direction;
	QAngle shoveAngles;
	VectorAngles( m_staggerDir.Get(), shoveAngles );
	float duration = z_tank_max_stagger_duration.GetFloat();
	const float relativeYaw = AngleNormalize( GetAbsAngles().y - shoveAngles.y );
	int directionIndex = 1;
	// S32BCF7..32BD34: an unordered yaw retains the default rear index.
	if ( !( relativeYaw <= 45.0f ) || relativeYaw < -45.0f )
	{
		if ( relativeYaw > 45.0f && relativeYaw < 135.0f )
			directionIndex = 3;
		else if ( relativeYaw < -45.0f && relativeYaw > -135.0f )
			directionIndex = 2;
	}
	else
	{
		directionIndex = 0;
	}
	if ( bSourcePounced && GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 )
		directionIndex = 0;

	m_staggerDist = z_tank_max_stagger_distance.GetFloat();
	m_staggerStart = GetAbsOrigin();
	m_isAttemptingToPounce = false;
	bool bImmediateStagger = false;
	if ( !m_isCommentaryStatue )
	{
		if ( GetGroundEntity() )
		{
			DoTerrorAnimationEvent( TERROR_ANIMEVENT_STAGGER, directionIndex );
			GetTerrorAnimState()->ResetAnimationMovement();
			bImmediateStagger = true;
		}
		else
		{
			m_pendingStaggerEventData = directionIndex;
			SetShoveForce( shoveForce );
			SetLaunchSource( source );
			SetGroundEntity( NULL );
		}
	}
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
	{
		const color32 fade = { 255, 0, 0, static_cast< unsigned char >( z_tank_stagger_fade_alpha.GetInt() ) };
		UTIL_ScreenFade( this, fade,
			z_tank_stagger_fade_duration.GetFloat(), 0.0f, 1 );
	}
	else
	{
		duration = bSourcePounced && TerrorGameRules()->IsPlayerControlledZombies() ?
			z_max_hunter_pounce_stagger_duration.GetFloat() : z_max_stagger_duration.GetFloat();
	}
	if ( bImmediateStagger )
	{
		Bash( duration );
		m_staggerTimer.Start( duration );
	}
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE, 0 );
	if ( CBaseAbility *ability = m_customAbility.Get() )
	{
		if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
			ability->StopActivation( z_tank_max_stagger_duration.GetFloat() );
		else if ( ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 ) ||
			( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 5 ) )
			ability->StopActivation( 0.5f );
	}
	if ( !bSourcePounced &&
		( ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 ) ||
		  ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 1 ) ) )
	{
		m_bashedStart = gpGlobals->curtime;
	}
	m_staggerHistory.AddToTail( gpGlobals->curtime );
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() >= 1 && GetZombieClass() <= 6 )
	{
		if ( m_staggerHistory.Count() < m_staggerThreshold )
		{
			CTakeDamageInfo info( this, source, 250.0f, DMG_CLUB, 0 );
			OnInfectedPain( info );
		}
		else
		{
			trace_t trace;
			memset( &trace, 0, sizeof( trace ) );
			const Vector center = WorldSpaceCenter();
			trace.startpos = center;
			trace.endpos = center;
			trace.plane.normal = -direction;
			trace.plane.dist = ( trace.plane.normal.y * center.y + trace.plane.normal.x * center.x ) +
				trace.plane.normal.z * center.z;
			trace.plane.type = 5;
			trace.plane.signbits = SignbitsForPlane( &trace.plane );
			trace.contents = CONTENTS_HITBOX;
			trace.m_pEnt = this;
			CTakeDamageInfo info( this, source, 250.0f, DMG_CLUB, 0 );
			// S32C227..32C24A: skip only the undefined source-position/force step.
			if ( source )
				CalculateMeleeDamageForce( &info, direction, source->Weapon_ShootPosition(), 0.2f );
			ClearMultiDamage();
			DispatchTraceAttack( info, direction, &trace );
			ApplyMultiDamage();
		}
	}
}
#pragma float_control( pop )

// S32C9B0. Its second stack argument is genuinely not read by the native body.
void CTerrorPlayer::OnShovedBy( CTerrorPlayer *source, bool )
{
	if ( IsPlayingDeathAnimation() || !source )
		return;
	Vector force = GetAbsOrigin() - source->GetAbsOrigin();
	VectorNormalize( force );
	force *= z_pounce_stumble_force.GetFloat();
	if ( IsPlayingDeathAnimation() || IsBeingPounced() )
		return;
	StopUseAction();
	StopBeingRevived( true );
	EndReviving( true );
	ClearUseEntity();
	SetPushEntity( NULL );
	if ( CTerrorWeapon *weapon = dynamic_cast< CTerrorWeapon * >( GetActiveWeapon() ) )
		weapon->SuppressHelpingHand( 0.0f );

	Vector shoveForce = force;
	if ( shoveForce.LengthSqr() > 10000.0f )
	{
		VectorNormalize( shoveForce );
		shoveForce *= 100.0f;
	}
	Vector direction( shoveForce.x, shoveForce.y, 0.0f );
	VectorNormalize( direction );
	m_staggerDir = direction;
	QAngle shoveAngles;
	VectorAngles( m_staggerDir.Get(), shoveAngles );
	const float relativeYaw = AngleNormalize( GetAbsAngles().y - shoveAngles.y );
	int directionIndex = 1;
	if ( relativeYaw > 45.0f || relativeYaw < -45.0f )
	{
		if ( relativeYaw > 45.0f && relativeYaw < 135.0f )
			directionIndex = 3;
		else if ( relativeYaw < -45.0f && relativeYaw > -135.0f )
			directionIndex = 2;
	}
	else
	{
		directionIndex = 0;
	}
	m_staggerDist = z_tank_max_stagger_distance.GetFloat();
	m_staggerStart = GetAbsOrigin();
	m_isAttemptingToPounce = false;
	if ( GetGroundEntity() )
	{
		DoTerrorAnimationEvent( TERROR_ANIMEVENT_STAGGER, directionIndex );
		GetTerrorAnimState()->ResetAnimationMovement();
		Bash( z_max_stagger_duration.GetFloat() );
		m_staggerTimer.Start( z_max_stagger_duration.GetFloat() );
	}
	else
	{
		m_pendingStaggerEventData = directionIndex;
		SetShoveForce( shoveForce );
		SetLaunchSource( source );
		SetGroundEntity( NULL );
	}
	DoTerrorAnimationEvent( TERROR_ANIMEVENT_CANCEL_ATTACK_GESTURE, 0 );
	m_staggerHistory.AddToTail( gpGlobals->curtime );
}

// S2FBDB0/S32D830/S330110. The actual Hunter acquisition calls this same
// connected TEAM2 roster producer; no sphere entity scan or second list.
void CTerrorPlayer::StumblePounceBystanders( CTerrorPlayer *ignored )
{
	class ShoveFunctor
	{
	public:
		ShoveFunctor( CTerrorPlayer *source, CTerrorPlayer *ignored ) :
			m_source( source ), m_ignored( ignored ), m_origin( source->WorldSpaceCenter() ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			if ( player->GetTeamNumber() != TEAM_SURVIVOR )
				return true;
			const Vector delta = player->WorldSpaceCenter() - m_origin;
			const float distance = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
			const float radius = TerrorGameRules()->IsSurvivalMode() ? 160.0f : z_pounce_stumble_radius.GetFloat();
			if ( radius > distance && player != m_ignored && !player->IsIncapacitated() &&
				player->IsAlive() && TerrorGameRules() &&
				TerrorGameRules()->GetAmountOfEntityVisible( m_origin, player ) > 0.0f )
			{
				player->OnShovedBy( m_source, true );
			}
			return true;
		}
		CTerrorPlayer *m_source;
		CTerrorPlayer *m_ignored;
		Vector m_origin;
	} shove( this, ignored );
	ForEachPlayer( shove );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S2C9620/S2C8390 retain their separate, repeated ghost-query policies.
bool CTerrorPlayer::ShouldPlayTerrorFootstepSound() const
{
	return !IsGhost() && !( GetFlags() & FL_FROZEN ) &&
		( !( GetFlags() & FL_ATCONTROLS ) ||
		  ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 6 ) ) && !IsGhost();
}

bool CTerrorPlayer::IsTerrorFootstepSurfaceValid( float speed, bool force ) const
{
	return !IsGhost() && !( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 );
}

const char *CTerrorPlayer::GetTerrorFootstepSoundName( const char *sound, bool right, float speed, bool force )
{
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 3 )
		return "";
	static char name[64];
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 )
		Q_snprintf( name, sizeof( name ), "Tank.%s", sound );
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 2 )
		Q_snprintf( name, sizeof( name ), "Boomer.%s", sound );
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 5 )
		Q_snprintf( name, sizeof( name ), "Jockey.%s", sound );
	else if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 6 )
		Q_snprintf( name, sizeof( name ), "Charger.Default.%s", right ? "RunRight" : "RunLeft" );
	else if ( GetTeamNumber() == TEAM_INFECTED )
		Q_snprintf( name, sizeof( name ), "Infected.%s", sound );
	else
		Q_snprintf( name, sizeof( name ), "Survivor.%s", sound );
	return name;
}

extern ConVar z_tank_footstep_shake_amplitude;
extern ConVar z_tank_footstep_shake_duration;
extern ConVar z_tank_footstep_shake_radius;
extern ConVar z_tank_footstep_shake_interval;
extern ConVar z_noise_level_footstep;

// S30E8B0. Native325590 is the positive running-timer timestamp predicate,
// not IsAlive. The shake excludes the source Tank, then noise is set separately.
void CTerrorPlayer::OnTerrorFootstep( const Vector &origin, bool left, bool force )
{
	if ( GetTeamNumber() == TEAM_INFECTED && GetZombieClass() == 8 &&
		static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_tankFootstepShakeTimer.m_timestamp.Get() ) &&
		IsRunning() )
	{
		CBasePlayer *ignoredPlayers[1] = { this };
		CUtlVector< CBasePlayer * > ignored( ignoredPlayers, 1, 1 );
		const float radius = z_tank_footstep_shake_radius.GetFloat();
		const float duration = z_tank_footstep_shake_duration.GetFloat();
		const float amplitude = z_tank_footstep_shake_amplitude.GetFloat();
		UTIL_ScreenShake( GetAbsOrigin(), amplitude, 150.0f, duration, radius, SHAKE_START, false, &ignored );
		m_tankFootstepShakeTimer.Start( z_tank_footstep_shake_interval.GetFloat() );
	}
	if ( force || IsRunning() )
		SetNoiseLevel( z_noise_level_footstep.GetFloat() );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
