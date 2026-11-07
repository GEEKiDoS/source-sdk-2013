//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_locomotion.h"
#include "l4d2/player/terrorplayer.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "nav_mesh.h"
#include "nav_area.h"
#include "obstacle_pushaway.h"
#include "props.h"
#include "tier0/memdbgon.h"

ConVar sb_locomotion_wait_threshold( "sb_locomotion_wait_threshold", "10", 0x00004004 );
ConVar sb_debug_locomotion( "sb_debug_locomotion", "0", 0x00004004 );
extern ConVar NextBotPlayerMove;
extern ConVar z_gun_damage;

// Native 351C10 is a deliberately narrower predicate than 12A440.
static bool IsImmediatelyBreakableObstacle( CBaseEntity *obstacle )
{
	int group = obstacle->GetCollisionGroup();
	return ( group == COLLISION_GROUP_BREAKABLE_GLASS || group == COLLISION_GROUP_NONE ) &&
		obstacle->m_takedamage == DAMAGE_YES && static_cast< float >( obstacle->GetHealth() ) <= 10.0f &&
		( FClassnameIs( obstacle, "func_breakable_surf" ) || FClassnameIs( obstacle, "func_breakable" ) || dynamic_cast< CBreakableProp * >( obstacle ) != NULL );
}

SurvivorLocomotion::SurvivorLocomotion( INextBot *bot ) : PlayerLocomotion( bot )
{
	m_approachDirection.Init();
	m_approachWeight = 0.0f;
	m_approachCount = 0;
	m_previousFeet.Init();
	m_hasMoved = false;
}

void SurvivorLocomotion::Reset()
{
	PlayerLocomotion::Reset();
	m_approachDirection.Init();
	m_approachWeight = 0.0f;
	m_approachCount = 0;
	m_hasMoved = false;
	m_previousFeet.Init();
	m_waitTimer.Invalidate();
}

void SurvivorLocomotion::Approach( const Vector &pos, float goalWeight )
{
	Vector direction = pos - GetFeet();
	direction.NormalizeInPlace();
	m_approachDirection += goalWeight * direction;
	m_approachWeight += goalWeight;
	++m_approachCount;
	if ( sb_debug_locomotion.GetBool() )
	{
		float width = MIN( 10.0f, goalWeight * 5.0f );
		NDebugOverlay::HorzArrow( GetFeet(), GetFeet() + 25.0f * direction, width, 255, 255, 0, 255, true, 0.1f );
	}
}

void SurvivorLocomotion::Update()
{
	PlayerLocomotion::Update();
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( GetBot()->GetEntity() );
	if ( ( !m_waitTimer.IsElapsed() || !player->CanPerformBackpackAction() || player->IsBusy() ||
		player->IsStaggeringOrPending() || !NextBotPlayerMove.GetBool() ) && IsOnGround() )
	{
		m_approachDirection.Init();
		m_approachCount = 0;
		m_hasMoved = false;
		m_approachWeight = 0.0f;
		m_previousFeet = GetFeet();
		ClearStuckStatus();
		return;
	}

	if ( m_approachWeight > 0.0f )
	{
		if ( m_approachCount > 1 && GetUpdateInterval() > 0.0f && !IsUsingLadder() && IsOnGround() )
		{
			float speed = ( GetFeet() - m_previousFeet ).Length() / GetUpdateInterval();
			if ( speed <= sb_locomotion_wait_threshold.GetFloat() )
			{
				if ( m_hasMoved )
				{
					m_waitTimer.Start( RandomFloat( 2.0f, 3.0f ) );
					ClearStuckStatus();
					return;
				}
			}
			else
			{
				m_hasMoved = true;
			}
		}

		m_approachDirection *= 1.0f / m_approachWeight;
		if ( sb_debug_locomotion.GetBool() && GetUpdateInterval() > 0.0f )
		{
			NDebugOverlay::HorzArrow( GetFeet(), GetFeet() + m_approachDirection / GetUpdateInterval(), 5.0f, 255, 0, 0, 255, true, 0.1f );
		}
		m_previousFeet = GetFeet();
		Vector moveGoal = GetFeet() + m_approachDirection;
		CNavArea *currentArea = player->GetLastKnownArea();
		CNavArea *goalArea = TheNavMesh->GetNavArea( moveGoal, 120.0f );
		if ( currentArea && goalArea && !currentArea->IsDamaging() && goalArea->IsDamaging() && player->GetTimeSinceBurnDamage() < 10.0f )
		{
			m_waitTimer.Start( RandomFloat( 2.0f, 3.0f ) );
			ClearStuckStatus();
			return;
		}
		PlayerLocomotion::Approach( moveGoal, 1.0f );
		m_approachDirection.Init();
		m_approachWeight = 0.0f;
		m_approachCount = 0;
	}

	IBody *body = GetBot()->GetBodyInterface();
	NextBotTraceFilterIgnoreActors filter( player, COLLISION_GROUP_NONE );
	const Vector &center = player->WorldSpaceCenter();
	Vector swingGoal = center + 50.0f * GetGroundMotionVector();
	trace_t trace;
	UTIL_TraceHull( center, swingGoal, Vector( -1.0f, -1.0f, -1.0f ), Vector( 1.0f, 1.0f, 1.0f ), body->GetSolidMask(), &filter, &trace );
	if ( trace.DidHitNonWorldEntity() )
	{
		CTakeDamageInfo info( player, player, player->GetActiveWeapon(), vec3_origin, vec3_origin, z_gun_damage.GetFloat(), DMG_CLUB );
		if ( IsBreakableEntity( trace.m_pEnt, false, false, &info ) )
		{
			static PressAltFireButtonReply meleeReply;
			Vector aimGoal = player->WorldSpaceCenter() + 50.0f * GetGroundMotionVector();
			GetBot()->GetBodyInterface()->AimHeadTowards( aimGoal, IBody::CRITICAL, 0.5f, &meleeReply, "Looking toward breakable for melee swing" );
		}
	}
}

bool SurvivorLocomotion::IsScrambling() const
{
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( GetBot()->GetEntity() );
	return player->GetTimeSinceLadderInteraction() < 1.0f || !IsOnGround() || IsClimbingOrJumping() || IsUsingLadder();
}

bool SurvivorLocomotion::IsAreaTraversable( const CNavArea *area ) const
{
	return !area->HasAttributes( 0x30000 ) && !area->IsBlocked( TEAM_SURVIVOR, false );
}

bool SurvivorLocomotion::IsEntityTraversable( CBaseEntity *obstacle, TraverseWhenType when ) const
{
	if ( IsImmediatelyBreakableObstacle( obstacle ) )
		return true;
	return ILocomotion::IsEntityTraversable( obstacle, when );
}

float SurvivorLocomotion::GetRunSpeed() const
{
	return 230.0f;
}

float SurvivorLocomotion::GetWalkSpeed() const
{
	return 100.0f;
}
