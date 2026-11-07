//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/common/zombiebot_locomotion.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "tier0/vprof.h"
#include "NextBot/NextBotUtil.h"
#include "nav_ladder.h"
#include "l4d2/terror_shareddefs.h"
#include "debugoverlay_shared.h"

#include "l4d2/infected/common/zombiebot_locomotion_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// Defaults and flags: pinned server_console.json. These are the original inputs,
// not the unrelated stock/TF bot tuning values.
ConVar z_speed( "z_speed", "250", 0x00006004 );
static ConVar nb_gravity( "nb_gravity", "1000", 0x00004004 );
static ConVar nb_acceleration( "nb_acceleration", "500", 0x00004004 );
static ConVar nb_friction_forward( "nb_friction_forward", "0", 0x00004004 );
static ConVar nb_friction_sideways( "nb_friction_sideways", "3", 0x00004004 );
static ConVar nb_yaw_rate( "nb_yaw_rate", "250", 0x00004004 );
static ConVar nb_lean_max_angle( "nb_lean_max_angle", "30", 0x00004004 );
static ConVar nb_lean_rate( "nb_lean_rate", "3", 0x00004004 );

ZombieBotLocomotion::ZombieBotLocomotion( Infected *infected ) : BaseClass( infected )
{
	// S:0x3F8BD0. This global draw occurs once per component construction,
	// independently of the actor's RandomFloat(0,1) and weapon prediction RNG.
	const float speed = z_speed.GetFloat();
	m_maxRunSpeed = RandomFloat( 0.9f, 1.1f ) * speed;
}

void ZombieBotLocomotion::Reset()
{
	BaseClass::Reset();
	m_actualSpeed = 0.0f;
	m_forwardLean = 0.0f;
	m_sideLean = 0.0f;
}

float ZombieBotLocomotion::GetRunSpeed() const { return m_maxRunSpeed; }
float ZombieBotLocomotion::GetWalkSpeed() const { return 75.0f; } // S:0x3F82A0, not z_walk_speed.
float ZombieBotLocomotion::GetMaxAcceleration() const { return nb_acceleration.GetFloat(); }
float ZombieBotLocomotion::GetMaxDeceleration() const { return nb_yaw_rate.GetFloat(); } // S:0x3F86D0.
float ZombieBotLocomotion::GetGravity() const { return nb_gravity.GetFloat(); }
float ZombieBotLocomotion::GetFrictionForward() const { return nb_friction_forward.GetFloat(); }
float ZombieBotLocomotion::GetFrictionSideways() const { return nb_friction_sideways.GetFloat(); }
float ZombieBotLocomotion::GetMaxYawRate() const { return nb_yaw_rate.GetFloat(); }

bool ZombieBotLocomotion::IsEntityTraversable( CBaseEntity *entity, TraverseWhenType when ) const
{
	// S3FC040: both flags reject; unlike special infected, 0x2000 is not a pass.
	if ( !entity || ( entity->GetSpawnFlags() & 0x3000 ) != 0 )
		return false;
	if ( IsLowHealthBossBreakable( entity ) )
		return true;
	return ILocomotion::IsEntityTraversable( entity, when );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S4EA090: retain the unspilled length, but normalize using the native F32
// spill, epsilon and reciprocal. x64 uses double for the x87 return chain.
static double NormalizeZombieTraversalVector( Vector &value )
{
	const double length = sqrt( ( (double)value.x * value.x + (double)value.y * value.y ) + (double)value.z * value.z );
	const float storedLength = (float)length;
	const float divisor = storedLength + 0.00000011920928955078125f;
	const float inverse = 1.0f / divisor;
	value *= inverse;
	return length;
}

void ZombieBotLocomotion::ApplyAccumulatedApproach()
{
	// S:0x3F8F50. Inherited Approach() accumulates weighted displacement,
	// rather than choosing whichever path/avoidance request happened last.
	Vector rawPos = GetFeet();
	const float deltaT = GetUpdateInterval();
	if ( deltaT <= 0.0f )
		return;

	if ( m_accumApproachWeights > 0.0f )
	{
		Vector approachDelta = m_accumApproachVectors / m_accumApproachWeights;
		const float maxMove = GetRunSpeed() * deltaT;
		const double desiredLength = NormalizeZombieTraversalVector( approachDelta );
		const float desiredMove = desiredLength > (double)maxMove ? maxMove : (float)desiredLength;
		rawPos.x += approachDelta.x * desiredMove;
		rawPos.y += approachDelta.y * desiredMove;
		m_accumApproachVectors = vec3_origin;
		m_accumApproachWeights = 0.0f;
	}

	if ( !GetBot()->GetBodyInterface()->IsPostureMobile() )
		return;

	const Vector pos( rawPos.x, rawPos.y, GetFeet().z );
	m_moveVector = pos - GetBot()->GetPosition();
	m_moveVector.z = 0.0f;
	if ( NormalizeZombieTraversalVector( m_moveVector ) < (double)0.001f )
	{
		m_forwardLean = 0.0f;
		m_sideLean = 0.0f;
		return;
	}

	if ( DidJustJump() || !IsOnGround() )
		return;

	m_isClimbingUpToLedge = false;
	const Vector left( -m_moveVector.y, m_moveVector.x, 0.0f );
	m_moveVector = CrossProduct( left, GetGroundNormal() );
	NormalizeZombieTraversalVector( m_moveVector );

	const float forwardSpeed = DotProduct( m_velocity, m_moveVector );
	const float maxSpeed = MIN( GetDesiredSpeed(), GetSpeedLimit() );
	if ( forwardSpeed < maxSpeed )
	{
		const float ratio = forwardSpeed > 0.0f ? forwardSpeed / maxSpeed : 0.0f;
		// S3F933E..935B retains ratio^4 and acceleration in the x87 chain.
		const double wideRatio = ratio;
		const double squaredRatio = wideRatio * wideRatio;
		const double cubedRatio = squaredRatio * wideRatio;
		const double governor = 1.0 - cubedRatio * wideRatio;
		float acceleration = (float)( (double)GetMaxAcceleration() * governor );
		// Present in the original, absent from the refactored SDK locomotor.
		if ( acceleration * deltaT + forwardSpeed > maxSpeed )
			acceleration = ( maxSpeed - forwardSpeed ) / deltaT;
		m_acceleration += m_moveVector * acceleration;
	}
}

bool ZombieBotLocomotion::ClimbUpToLedge( const Vector &landingGoal, const Vector &landingForward, const CBaseEntity *obstacle )
{
	// S3FAF50. The obstacle is deliberately unused by the native entry.
	if ( z_debug_climb.GetBool() )
	{
		NDebugOverlay::Cross3D( GetFeet(), 5.0f, 0, 255, 0, true, 10.0f );
		NDebugOverlay::Cross3D( landingGoal, 5.0f, 0, 255, 0, true, 10.0f );
		NDebugOverlay::Line( GetFeet(), landingGoal, 0, 255, 0, true, 10.0f );
		NDebugOverlay::HorzArrow( landingGoal, landingGoal + landingForward * 10.0f, 2.0f, 0, 255, 0, 255, true, 10.0f );
	}
	if ( !IsOnGround() || m_isClimbingUpToLedge )
		return false;

	IBody *body = GetBot()->GetBodyInterface();
	const float height = landingGoal.z - GetFeet().z;
	static const int climbActivity[] =
	{
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_24_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_36_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_48_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_60_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_72_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_84_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_96_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_108_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_120_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_132_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_144_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_156_FROM_STAND,
		TERROR_ORIGINAL_ACT_TERROR_CLIMB_168_FROM_STAND
	};
	int climb = 0;
	while ( climb < ARRAYSIZE( climbActivity ) - 1 && !( height < 30.0f + 12.0f * climb ) )
		++climb;
	const float heightCorrection = height - ( 24.0f + 12.0f * climb );
	const float width = body->GetHullWidth();
	Vector thinMins( -1.0f, -1.0f, (float)( (double)GetStepHeight() + (double)0.1f ) );
	Vector thinMaxs( 1.0f, 1.0f, landingGoal.z - GetFeet().z );
	if ( thinMins.z > thinMaxs.z )
	{
		const float top = thinMins.z;
		thinMins.z = thinMaxs.z;
		thinMaxs.z = top;
	}
	NextBotTraversableTraceFilter filter( GetBot(), ILocomotion::IMMEDIATELY );
	Vector climbPos( landingGoal.x, landingGoal.y, GetFeet().z );
	const float maxOffset = width * 3.0f;
	float offset = 0.0f;
	bool foundSurface = false;
	trace_t surface;
	// The native checks the post-trace increment before accepting a hit. A
	// final hit whose increment passes maxOffset is still a failed search.
	while ( maxOffset >= offset )
	{
		if ( foundSurface )
			break;
		const Vector from = climbPos - landingForward * offset;
		const Vector to = GetFeet() + landingForward * ( width * 10.0f );
		TraceHull( from, to, thinMins, thinMaxs, body->GetSolidMask(), &filter, &surface );
		if ( z_debug_climb.GetBool() )
			NDebugOverlay::SweptBox( from, to, thinMins, thinMaxs, vec3_angle, surface.startsolid ? 255 : 0, 255, 0, 255, 15.0f );
		foundSurface = !surface.startsolid && ( surface.fraction < 1.0f || surface.allsolid );
		offset = width * 0.5f + offset;
	}
	if ( !( maxOffset >= offset ) || !foundSurface )
	{
		if ( GetBot()->IsDebugging( NEXTBOT_PATH ) )
			DevMsg( "%3.2f: Can't find climb surface normal\n", gpGlobals->curtime );
		m_velocity = vec3_origin;
		m_acceleration = vec3_origin;
		return false;
	}

	Vector normal = surface.plane.normal;
	normal.z = 0.0f;
	NormalizeZombieTraversalVector( normal );
	offset = 0.0f;
	for ( ;; )
	{
		const float step = (float)( (double)GetStepHeight() + (double)0.1f );
		Vector mins = body->GetHullMins();
		mins.z = (float)( (double)mins.z + (double)step );
		const Vector from = climbPos + normal * offset;
		Vector to = from;
		to.z = landingGoal.z;
		trace_t clearance;
		TraceHull( from, to, mins, body->GetHullMaxs(), body->GetSolidMask(), &filter, &clearance );
		if ( z_debug_climb.GetBool() )
		{
			const bool blocked = clearance.fraction < 1.0f || clearance.allsolid || clearance.startsolid;
			// Native debug drawing uses the original hit normal and full hull.
			const Vector debugFrom = climbPos + surface.plane.normal * offset;
			NDebugOverlay::SweptBox( debugFrom, to, body->GetHullMins(), body->GetHullMaxs(), vec3_angle, blocked ? 255 : 0, 0, 255, 255, 15.0f );
		}
		if ( !( clearance.fraction < 1.0f ) && !clearance.allsolid && !clearance.startsolid )
		{
			climbPos += normal * offset;
			break;
		}
		offset = width * 0.25f + offset;
		if ( !( maxOffset >= offset ) )
			break;
	}

	QAngle angles;
	VectorAngles( -normal, angles );
	m_nextBot->SetAbsAngles( angles );
	Vector start = climbPos;
	start.z += heightCorrection;
	DriveTo( start );
	if ( GetBot()->IsDebugging( NEXTBOT_PATH ) )
	{
		NDebugOverlay::HorzArrow( climbPos, climbPos + normal * 25.0f, 2.0f, 255, 255, 0, 255, true, 15.0f );
		NDebugOverlay::Circle( climbPos, Vector( 1, 0, 0 ), Vector( 0, -1, 0 ), 3.0f, 255, 0, 0, 255, true, 15.0f );
	}
	if ( !body->StartActivity( TerrorActivityFromOriginal( climbActivity[climb] ),
		IBody::MOTION_CONTROLLED_XY | IBody::MOTION_CONTROLLED_Z | IBody::ACTIVITY_TRANSITORY ) )
		return false;
	m_isJumping = true;
	m_isClimbingUpToLedge = true;
	m_ledgeJumpGoalPos = landingGoal;
	body->SetDesiredPosture( IBody::CROUCH );
	GetBot()->OnLeaveGround( m_nextBot->GetGroundEntity() );
	return true;
}

void ZombieBotLocomotion::JumpAcrossGap( const Vector &landingGoal, const Vector &landingForward )
{
	// S3F8370, including the original activity and unrounded range chain.
	if ( !IsOnGround() )
		return;
	IBody *body = GetBot()->GetBodyInterface();
	if ( body && !body->StartActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_JUMP_OVER_GAP ) ) )
		return;
	Vector toGoal = landingGoal - GetFeet();
	float height = toGoal.z;
	toGoal.z = 0.0f;
	const double range = NormalizeZombieTraversalVector( toGoal );
	const float maxHeight = (float)range * 0.89999998f;
	if ( height > maxHeight )
		height = maxHeight;
	const double flightTimeSquared = ( ( range - (double)height ) * 2.0 ) / (double)GetGravity();
	const float launchSpeed = (float)( (double)1.4142271280288696f * range / sqrt( flightTimeSquared ) );
	Vector ahead( toGoal.x, toGoal.y, toGoal.z + 1.0f );
	NormalizeZombieTraversalVector( ahead );
	m_velocity = ahead * launchSpeed;
	m_acceleration = vec3_origin;
	m_isJumping = true;
	m_isJumpingAcrossGap = true;
	m_isClimbingUpToLedge = false;
	GetBot()->OnLeaveGround( m_nextBot->GetGroundEntity() );
}

void ZombieBotLocomotion::Jump()
{
	// S3F8520 does not use the SDK ACT_JUMP activity.
	if ( !IsOnGround() )
		return;
	IBody *body = GetBot()->GetBodyInterface();
	if ( body && !body->StartActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_JUMP ) ) )
		return;
	const float gravity = GetGravity();
	m_velocity.z = (float)sqrt( (double)GetMaxJumpHeight() * ( (double)gravity + gravity ) );
	m_isJumping = true;
	m_isClimbingUpToLedge = false;
	GetBot()->OnLeaveGround( m_nextBot->GetGroundEntity() );
}

bool ZombieBotLocomotion::TraverseLadder()
{
	// S3FC190. Mounting is already owned by the inherited ClimbLadder and
	// DescendLadder entries, which match S3FBDF0/S3FBEE0.
	IBody *body = GetBot()->GetBodyInterface();
	if ( !body || !m_ladder )
		return false;
	const Activity dismount = TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_LADDER_DISMOUNT );
	if ( !body->IsActivity( dismount ) )
	{
		const float fraction = ( GetFeet().z - m_ladder->m_bottom.z ) / ( m_ladder->m_top.z - m_ladder->m_bottom.z );
		const float mountOffset = (float)( (double)body->GetHullWidth() * 0.75 );
		Vector position;
		position.x = m_ladder->m_bottom.x + ( m_ladder->m_top.x - m_ladder->m_bottom.x ) * fraction;
		position.y = m_ladder->m_bottom.y + ( m_ladder->m_top.y - m_ladder->m_bottom.y ) * fraction;
		position.x += m_ladder->GetNormal().x * mountOffset;
		position.y += m_ladder->GetNormal().y * mountOffset;
		position.z = m_nextBot->GetAbsOrigin().z;
		UpdatePosition( position );
		QAngle angles;
		VectorAngles( -m_ladder->GetNormal(), angles );
		m_nextBot->SetAbsAngles( angles );
	}
	if ( m_isGoingUpLadder )
	{
		Vector closest;
		m_ladderDismountGoal->GetClosestPointOnArea( m_ladder->m_top, &closest );
		const float top = closest.z > m_ladder->m_top.z ? closest.z : m_ladder->m_top.z;
		const float crouchHeight = body->GetCrouchHullHeight();
		if ( (double)crouchHeight > (double)top - GetFeet().z )
		{
			if ( !body->IsActivity( dismount ) )
				body->StartActivity( dismount, IBody::MOTION_CONTROLLED_XY | IBody::MOTION_CONTROLLED_Z );
			FaceTowards( m_ladderDismountGoal->GetCenter() );
		}
		else
			FaceTowards( m_ladder->m_top );
	}
	else
	{
		FaceTowards( m_ladder->m_bottom );
		if ( (double)GetStepHeight() + (double)m_ladderDismountGoal->GetCenter().z > GetFeet().z )
		{
			m_ladder = NULL;
			return false;
		}
	}
	m_velocity = vec3_origin;
	m_acceleration = vec3_origin;
	return true;
}

void ZombieBotLocomotion::FaceTowards( const Vector &target )
{
	// S3FC0A0 assigns the desired yaw on the unclamped branch, rather
	// than adding a wrapped delta to the previous yaw.
	const float interval = GetUpdateInterval();
	QAngle angles = m_nextBot->GetLocalAngles();
	const float desiredYaw = UTIL_VecToYaw( target - GetFeet() );
	const float difference = UTIL_AngleDiff( desiredYaw, angles.y );
	const double maxTurn = (double)GetMaxYawRate() * interval;
	const float storedMaxTurn = (float)maxTurn;
	if ( -maxTurn > (double)difference )
		angles.y -= storedMaxTurn;
	else if ( difference > storedMaxTurn )
		angles.y += storedMaxTurn;
	else
		angles.y = desiredYaw;
	m_nextBot->SetLocalAngles( angles );
}

void ZombieBotLocomotion::OnAnimationActivityComplete( int activity )
{
	// S3F8B00: only the real ladder dismount completion releases the ladder.
	if ( m_ladder && activity == TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_LADDER_DISMOUNT ) )
	{
		QAngle angles = m_nextBot->GetAbsAngles();
		angles.x = 0.0f;
		m_nextBot->SetAbsAngles( angles );
		m_ladder = NULL;
	}
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
void ZombieBotLocomotion::Update()
{
	VPROF_BUDGET( "ZombieBotLocomotion::Update", "NextBot" );

	// S:0x3FC4B0 is Update. The design's 0x3FAF50 is ClimbUpToLedge.
	// Only the native collision/ground solver moves the actor. This needs the
	// narrow friend declaration in NextBotGroundLocomotion, not copied layouts.
	ILocomotion::Update();
	const float deltaT = GetUpdateInterval();
	ApplyAccumulatedApproach();
	const Vector origPos = GetFeet();
	IBody *body = GetBot()->GetBodyInterface();
	if ( TraverseLadder() )
		return;

	if ( !body->IsPostureMobile() )
	{
		m_acceleration.x = 0.0f;
		m_acceleration.y = 0.0f;
		m_velocity.x = 0.0f;
		m_velocity.y = 0.0f;
	}

	const bool wasOnGround = IsOnGround();
	if ( !body->HasActivityType( IBody::MOTION_CONTROLLED_Z ) )
	{
		const float priorVerticalAcceleration = m_acceleration.z;
		if ( !IsOnGround() )
			m_acceleration.z -= GetGravity();
		if ( !IsClimbingOrJumping() || m_velocity.z <= 0.0f )
		{
			UpdateGroundConstraint();
			if ( IsOnGround() )
				m_acceleration.z = priorVerticalAcceleration;
		}
	}

	Vector newPos = GetFeet();
	const Vector right( m_moveVector.y, -m_moveVector.x, 0.0f );
	if ( IsOnGround() )
	{
		const float forwardSpeed = DotProduct( m_velocity, m_moveVector );
		const float sideSpeed = DotProduct( m_velocity, right );
		const float sideVelocityX = sideSpeed * right.x;
		const float sideVelocityY = right.y * sideSpeed;
		float forwardFrictionX = 0.0f;
		float forwardFrictionY = 0.0f;
		if ( forwardSpeed < 0.0f )
		{
			const float forwardVelocityX = m_moveVector.x * forwardSpeed;
			const float forwardVelocityY = m_moveVector.y * forwardSpeed;
			const float coefficient = -GetFrictionForward();
			forwardFrictionX = forwardVelocityX * coefficient;
			forwardFrictionY = forwardVelocityY * coefficient;
		}
		// S3FC75A..7E8 multiplies each projected component before friction.
		const float sideCoefficient = -GetFrictionSideways();
		m_acceleration.x += sideVelocityX * sideCoefficient + forwardFrictionX;
		m_acceleration.y += sideCoefficient * sideVelocityY + forwardFrictionY;
	}

	if ( body->HasActivityType( IBody::MOTION_CONTROLLED_XY ) )
	{
		m_acceleration.x = 0.0f;
		m_acceleration.y = 0.0f;
		m_velocity.x = m_nextBot->GetAbsVelocity().x;
		m_velocity.y = m_nextBot->GetAbsVelocity().y;
	}
	else
	{
		m_velocity.x += m_acceleration.x * deltaT;
		m_velocity.y += m_acceleration.y * deltaT;
		newPos.x += m_velocity.x * deltaT;
		newPos.y += m_velocity.y * deltaT;
	}

	if ( body->HasActivityType( IBody::MOTION_CONTROLLED_Z ) )
	{
		m_acceleration.z = 0.0f;
		m_velocity.z = m_nextBot->GetAbsVelocity().z;
	}
	else
	{
		m_velocity.z += m_acceleration.z * deltaT;
		newPos.z += m_velocity.z * deltaT;
	}

	UpdatePosition( newPos );
	const Vector adjustedVelocity = ( GetFeet() - origPos ) / deltaT;
	if ( !body->HasActivityType( IBody::MOTION_CONTROLLED_XY ) )
	{
		m_velocity.x = adjustedVelocity.x;
		m_velocity.y = adjustedVelocity.y;
	}
	if ( !body->HasActivityType( IBody::MOTION_CONTROLLED_Z ) )
		m_velocity.z = adjustedVelocity.z;

	Vector2D groundVelocity = m_velocity.AsVector2D();
	m_actualSpeed = groundVelocity.NormalizeInPlace();
	if ( IsOnGround() )
	{
		if ( m_actualSpeed > GetRunSpeed() )
		{
			m_actualSpeed = GetRunSpeed();
			m_velocity.x = m_actualSpeed * groundVelocity.x;
			m_velocity.y = m_actualSpeed * groundVelocity.y;
		}
		if ( !wasOnGround )
		{
			m_velocity.z = 0.0f;
			m_acceleration.z = 0.0f;
		}
	}
	else if ( m_velocity.IsLengthLessThan( 1.0f ) )
	{
		m_velocity = GetRunSpeed() * GetGroundMotionVector();
	}
	m_nextBot->SetAbsVelocity( m_velocity );

	QAngle lean = GetDesiredLean();
	// S3FCB70..CBFD: only the numerator and slide spill before the wide lean chain.
	const double lateralProjection = ( (double)right.y * m_acceleration.y + (double)m_acceleration.x * right.x )
		+ (double)m_acceleration.z * 0.0;
	const float maxLeanNumerator = nb_lean_max_angle.GetFloat() * m_actualSpeed;
	const float slide = (float)( lateralProjection / (double)GetMaxAcceleration() );
	const double maxLeanAngle = (double)maxLeanNumerator / (double)GetRunSpeed();
	lean.y = (float)( (double)lean.y + ( -( maxLeanAngle * (double)slide ) - (double)lean.y )
		* (double)nb_lean_rate.GetFloat() * (double)deltaT );
	SetDesiredLean( lean );
	m_acceleration = vec3_origin;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
