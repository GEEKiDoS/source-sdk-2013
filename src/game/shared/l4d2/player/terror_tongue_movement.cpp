//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terror_gamemovement.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "tier0/memdbgon.h"

extern ConVar tongue_start_pull_delay, tongue_victim_acceleration;
extern ConVar tongue_victim_max_speed, tongue_gravity_force;

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S36A7B0/C28F800. This is reached from WalkMove, AirMove and FullLadderMove;
// it does not replace normal walking when the smoker or actual tongue is gone.
bool CTerrorGameMovement::FullTongueMove()
{
	if ( !m_pTerrorPlayer ) return false;
	CTerrorPlayer *smoker = m_pTerrorPlayer->GetTongueOwner();
	if ( !smoker ) return false;
	CTongue *tongue = dynamic_cast< CTongue * >( smoker->GetCustomAbility() );
	if ( !tongue ) return false;

	const Vector origin = mv->GetAbsOrigin();
	Vector pullPosition = m_pTerrorPlayer->GetTonguePullPosition();
	const Vector pullGround = tongue->GetPullGroundPosition();
	if ( pullPosition.IsZero( 0.01f ) ) pullPosition = smoker->GetAbsOrigin();
	Vector direction = pullPosition - origin;
	const bool onGround = player->GetGroundEntity() != NULL;
	const float horizontalDistance = direction.Length2D();
	const bool farAway = horizontalDistance > 50.0f;
	if ( onGround )
	{
		if ( farAway && direction.z > 0.0f ) direction.z = 0.0f;
	}
	else if ( direction.z < 0.0f ) return false;

	float lift = 0.0f;
	if ( direction.z > 0.0f )
	{
		if ( !onGround && GetStandableNormal() != -1.0f ) direction.z = 0.0f;
		if ( direction.Length2DSqr() < 2304.0f )
		{
			if ( GetStandableNormal() == -1.0f )
			{
				lift = 300.0f;
				SetGroundEntity( NULL );
			}
			else if ( pullPosition.z - pullGround.z < 200.0f && origin.z - pullGround.z > 30.0f )
				lift = 10.0f;
		}
	}
	VectorNormalize( direction );
	const float start = m_pTerrorPlayer->m_tongueVictimTimer.GetStartTime();
	const float elapsed = !( start > 0.0f ) ? 99999.8984375f : gpGlobals->curtime - start;
	const float dt = gpGlobals->frametime;
	const float maximum = tongue_victim_max_speed.GetFloat();
	float speed = tongue_victim_acceleration.GetFloat() * maximum * dt + mv->m_vecVelocity.Length();
	if ( !( maximum > speed ) ) speed = maximum;
	if ( tongue_start_pull_delay.GetFloat() > elapsed ) speed = 0.0f;
	const float desiredTravel = dt * speed;
	const float travel = desiredTravel > horizontalDistance ? horizontalDistance : desiredTravel;
	Vector destination = origin + direction * travel;

	// Native100F50B0 is the four QUADRANTS, without the ordinary categorize
	// helper's restoration of the original fraction/endpos. Do not use the
	// SDK TryTouchGroundInQuadrants, whose contract restores both values.
	const Vector &mins = BaseClass::GetPlayerMins();
	const Vector &maxs = BaseClass::GetPlayerMaxs();
	const Vector below = origin - Vector( 0.0f, 0.0f, 48.0f );
	trace_t ground;
	for ( int quadrant = 0; quadrant != 4; ++quadrant )
	{
		Vector quadrantMins = mins, quadrantMaxs = maxs;
		switch ( quadrant )
		{
		case 0: quadrantMaxs.x = MIN( 0.0f, maxs.x ); quadrantMaxs.y = MIN( 0.0f, maxs.y ); break;
		case 1: quadrantMins.x = MAX( 0.0f, mins.x ); quadrantMins.y = MAX( 0.0f, mins.y ); break;
		case 2: quadrantMaxs.x = MIN( 0.0f, maxs.x ); quadrantMins.y = MAX( 0.0f, mins.y ); break;
		case 3: quadrantMins.x = MAX( 0.0f, mins.x ); quadrantMaxs.y = MIN( 0.0f, maxs.y ); break;
		}
		TryTouchGround( origin, below, quadrantMins, quadrantMaxs, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, ground );
		if ( ground.m_pEnt && !( ground.plane.normal.z < 0.7f ) ) break;
	}
	float ceiling = pullPosition.z;
	const float groundDrop = origin.z - ground.endpos.z;
	if ( ground.plane.normal.z >= 0.7f && groundDrop > 24.0f )
	{
		Vector step = origin;
		step.z = farAway ? ground.endpos.z : origin.z - ( groundDrop - 24.0f );
		TracePlayerBBox( origin, step, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, ground );
		if ( ground.fraction > 0.0f ) ceiling = ground.endpos.z;
	}
	destination.z -= tongue_gravity_force.GetFloat() * dt * dt;
	if ( lift > 0.0f ) destination.z += dt * lift;
	if ( m_pTerrorPlayer->m_isHangingFromTongue && GetStandableNormal() != -1.0f && destination.z > origin.z )
		destination.z = origin.z;

	Vector before( pullPosition.x - origin.x, pullPosition.y - origin.y, 0.0f );
	Vector after( pullPosition.x - destination.x, pullPosition.y - destination.y, 0.0f );
	VectorNormalize( before );
	VectorNormalize( after );
	if ( !( DotProduct( before, after ) > 0.0f ) )
	{
		destination.x = pullPosition.x;
		destination.y = pullPosition.y;
	}
	if ( destination.z > ceiling ) destination.z = ceiling;
	Vector horizontal = destination - mv->GetAbsOrigin();
	float vertical = horizontal.z;
	horizontal.z = 0.0f;
	float horizontalTravel = horizontal.Length2D();
	if ( !( horizontal.x > -0.01f && horizontal.x < 0.01f && horizontal.y > -0.01f && horizontal.y < 0.01f ) )
		VectorNormalize( horizontal );
	const float maximumTravel = maximum * dt;
	if ( !( maximumTravel > horizontalTravel ) ) horizontalTravel = maximumTravel;
	if ( m_pTerrorPlayer->m_isHangingFromTongue && GetStandableNormal() != -1.0f )
		vertical = dt * lift;
	else if ( !( vertical >= 0.0f ) ) vertical = mv->m_vecVelocity.z * dt;
	else if ( !( maximumTravel > vertical ) ) vertical = maximumTravel;
	destination = mv->GetAbsOrigin() + horizontal * horizontalTravel;
	destination.z = mv->GetAbsOrigin().z + vertical;
	MoveGround( destination, GetStandableNormal() );
	if ( mv->m_vecVelocity.z > 0.0f ) mv->m_vecVelocity.z *= 0.1f;
#ifndef CLIENT_DLL
	// S36B1E1 preserves both identical-value setter calls around the real
	// server-only S2A8F80 owner. Client2901F7 has no acquisition continuation.
	const Vector oldVelocity = m_pTerrorPlayer->GetAbsVelocity();
	const Vector temporaryVelocity = oldVelocity;
	m_pTerrorPlayer->SetAbsVelocity( temporaryVelocity );
	UpdateLedgeGrab();
	m_pTerrorPlayer->SetAbsVelocity( oldVelocity );
#endif
	return true;
}

// S2D2800: a handled tongue move returns before normal air acceleration and
// its OnFinishWalkMove(false) callback. The ordinary air path retains both.
void CTerrorGameMovement::AirMove()
{
	if ( FullTongueMove() ) return;
	BaseClass::AirMove();
	OnFinishWalkMove( false );
}

// S2D29E0: the tongue is also pulled while initially attached to a ladder.
void CTerrorGameMovement::FullLadderMove()
{
#ifndef CLIENT_DLL
	m_pTerrorPlayer->m_canGrabLedge = false;
#endif
	if ( FullTongueMove() )
		CategorizePosition( false );
	else
		BaseClass::FullLadderMove();
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
