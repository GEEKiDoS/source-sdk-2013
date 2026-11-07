//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terror_gamemovement.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/entities/func_simpleladder.h"
#include "movevars_shared.h"
#include "in_buttons.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/weapons/terror_weapon.h"
#include "datacache/imdlcache.h"
#ifdef CLIENT_DLL
#include "l4d2/weapons/c_base_mounted_weapon.h"
#include "l4d2/infected/common/c_infected.h"
#else
#include "l4d2/weapons/base_mounted_weapon.h"
#include "l4d2/infected/common/infected.h"
#include "NextBot/NextBotManager.h"
#include "NextBot/NextBotIntentionInterface.h"
#include "l4d2/weapons/weapon_claw.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "ipredictionsystem.h"
#endif
#ifndef CLIENT_DLL
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_area.h"
#include "ndebugoverlay.h"
#endif
#include "l4d2/player/terror_gamemovement_console_server_cvars.inc"
#include "l4d2/terror_trace_filter.h"
#include "l4d2/terror_physics_math.h"
#include "model_types.h"
#include "tier0/memdbgon.h"

ConVar z_hunter_ground_normal( "z_hunter_ground_normal", "0.2", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_min_ladder_mount_dot( "z_min_ladder_mount_dot", "0.5", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
,
	"Minimum 2D dot product from player's view to a ladder to be able to grab it" );
extern ConVar survivor_allow_crawling;
extern ConVar survivor_revive_continuous_fire;
extern ConVar survivor_crouch_speed;
extern ConVar survivor_incapacitated_roll;
extern ConVar z_tank_grenade_roll;
extern ConVar punch_angle_decay_rate;
#ifndef CLIENT_DLL
extern ConVar z_debug_ledges;
extern ConVar z_grab_ledges_solo;
#endif

static CTerrorGameMovement s_TerrorGameMovement;
IGameMovement *g_pGameMovement = &s_TerrorGameMovement;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CTerrorGameMovement, IGameMovement, INTERFACENAME_GAMEMOVEMENT, s_TerrorGameMovement );

CTerrorGameMovement::CTerrorGameMovement()
{
	m_pTerrorPlayer = NULL;
}

// S2D2570/C276A80 dispatch the existing ability recharge callback at crouch start.
void CTerrorGameMovement::OnStartDuck()
{
	if ( CBaseAbility *ability = m_pTerrorPlayer->GetCustomAbility() )
		ability->OnOwnerLanded();
}

// S2D49C0/C2788C0. The real server physics-push caller S143F90 reaches
// S2D7390's scoped override; the target calls its game-owned method directly.
static int s_terrorMovementTraceDepth;
#ifndef CLIENT_DLL
static CBaseEntity *s_terrorMovementTraceOverride;
#endif
static CTraceFilterPlayerMovement s_ghostMovementFilters[8];
// S599610/C513370 construct the native no-NPC-or-player filter array.
static CTraceFilterNoNPCsOrPlayer s_noNPCsOrPlayerMovementFilters[8] =
{
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE ),
	CTraceFilterNoNPCsOrPlayer( NULL, COLLISION_GROUP_NONE )
};
static CTraceFilterSkipTwoEntities s_terrorMovementFilters[8] =
{
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE ),
	CTraceFilterSkipTwoEntities( NULL, NULL, COLLISION_GROUP_NONE )
};

ITraceFilter *CTerrorGameMovement::GetMovementTraceFilter( int collisionGroup )
{
	CBaseEntity *second = player->m_hLastStuckActor.Get();
	if ( m_bInStuckTest )
		second = NULL;
#ifndef CLIENT_DLL
	if ( s_terrorMovementTraceOverride )
		second = s_terrorMovementTraceOverride;
#endif
	if ( s_terrorMovementTraceDepth >= 8 )
		return NULL;
	const int index = s_terrorMovementTraceDepth;
	s_terrorMovementTraceDepth = (int)( (unsigned int)s_terrorMovementTraceDepth + 1u );
	if ( index < 0 )
		return NULL; // Ledger306, same counter writes as the native.

	if ( m_pTerrorPlayer && m_pTerrorPlayer->IsGhost() )
	{
		CTraceFilterPlayerMovement *filter = &s_ghostMovementFilters[index];
		filter->SetPassEntity( player );
		filter->SetPassEntity2( second );
		filter->SetCollisionGroup( collisionGroup );
		return filter;
	}
	if ( m_pTerrorPlayer && m_pTerrorPlayer->CanBeRevived() )
	{
		CTraceFilterNoNPCsOrPlayer *filter = &s_noNPCsOrPlayerMovementFilters[index];
		// Native's pass entity is the second entity, in addition to the actor exclusions.
		filter->SetPassEntity( second );
		filter->SetCollisionGroup( collisionGroup );
		return filter;
	}
	if ( m_pTerrorPlayer && m_pTerrorPlayer->IsJockeyVictim() )
	{
		CTraceFilterNoNPCsOrPlayer *filter = &s_noNPCsOrPlayerMovementFilters[index];
		filter->SetPassEntity( player );
		filter->SetCollisionGroup( collisionGroup );
		return filter;
	}

	CTraceFilterSkipTwoEntities *filter = &s_terrorMovementFilters[index];
	filter->SetPassEntity( player );
	if ( m_pTerrorPlayer && m_pTerrorPlayer->IsBeingCarried() )
		filter->SetPassEntity2( m_pTerrorPlayer->m_carryAttacker.Get() );
	else if ( m_pTerrorPlayer && m_pTerrorPlayer->GetCarryVictim() )
		filter->SetPassEntity2( m_pTerrorPlayer->GetCarryVictim() );
	else if ( m_pTerrorPlayer && m_pTerrorPlayer->GetPummelTarget() )
		filter->SetPassEntity2( m_pTerrorPlayer->GetPummelTarget() );
	else if ( m_pTerrorPlayer && m_pTerrorPlayer->IsPummelVictim() )
		filter->SetPassEntity2( m_pTerrorPlayer->GetPummelAttacker() );
	else
		filter->SetPassEntity2( second );
	filter->SetCollisionGroup( collisionGroup );
	return filter;
}

void CTerrorGameMovement::ReleaseMovementTraceFilter( ITraceFilter **filter )
{
	s_terrorMovementTraceDepth = (int)( (unsigned int)s_terrorMovementTraceDepth - 1u );
	*filter = NULL;
}
#ifndef CLIENT_DLL
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S2D7390. Reached from real native S143F90 physics push, NOT engine-only.
void CTerrorGameMovement::UnblockPusher( CBasePlayer *subject, CBaseEntity *pusher )
{
	if ( !subject || !subject->IsPlayer() || !pusher )
		return;
	CTerrorPlayer *other = static_cast< CTerrorPlayer * >( subject );
	CTerrorPlayer *savedPlayer = m_pTerrorPlayer;
	m_pTerrorPlayer = other;
	player = other;
	s_terrorMovementTraceOverride = pusher;
	ITraceFilter *filter = GetMovementTraceFilter( COLLISION_GROUP_PLAYER_MOVEMENT );
	// The native restores BOTH pointers from its one saved Terror pointer.
	m_pTerrorPlayer = savedPlayer;
	player = savedPlayer;
	s_terrorMovementTraceOverride = NULL;

	Vector velocity = other->GetAbsVelocity();
	const Vector start = other->GetAbsOrigin();
	Vector direction = other->WorldSpaceCenter() - pusher->WorldSpaceCenter();
	direction.z = 0.0f;
	if ( direction.x > -0.01f && direction.x < 0.01f &&
		direction.y > -0.01f && direction.y < 0.01f )
	{
		ReleaseMovementTraceFilter( &filter );
		return;
	}
	const double length = sqrt( ( (double)direction.x * direction.x + (double)direction.y * direction.y ) + (double)direction.z * direction.z );
	const float roundedLength = (float)length;
	const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
	direction *= inverseLength;
	const Vector destination = start + direction * 18.0f;
	const unsigned int mask = PlayerSolidMask( false, other );
	trace_t directTrace;
	TryGroundMove( other, start, destination, NULL, 0.7f, velocity, directTrace, filter, mask );
	const Vector error = directTrace.endpos - destination;
	if ( error.x > -0.01f && error.x < 0.01f && error.y > -0.01f && error.y < 0.01f && error.z > -0.01f && error.z < 0.01f )
	{
		other->SetAbsOrigin( directTrace.endpos );
		ReleaseMovementTraceFilter( &filter );
		return;
	}
	Vector bestPosition = directTrace.endpos;
	Vector bestVelocity = velocity;
	Vector bestStep = vec3_origin;
	const float stepSize = other->m_Local.m_flStepSize;
	const Vector steps[5] = { Vector( 0.0f, 0.0f, stepSize ), Vector( stepSize, 0.0f, 0.0f ),
		Vector( -stepSize, 0.0f, 0.0f ), Vector( 0.0f, stepSize, 0.0f ), Vector( 0.0f, -stepSize, 0.0f ) };
	for ( int i = 0; i < ARRAYSIZE( steps ); ++i )
	{
		Vector candidateVelocity = velocity;
		trace_t candidateTrace;
		TryGroundMove( other, start, destination, &steps[i], 0.7f, candidateVelocity, candidateTrace, filter, mask );
		const float candidateSpeed = ( candidateVelocity.y * candidateVelocity.y + candidateVelocity.x * candidateVelocity.x ) + candidateVelocity.z * candidateVelocity.z;
		const float bestSpeed = ( bestVelocity.y * bestVelocity.y + bestVelocity.x * bestVelocity.x ) + bestVelocity.z * bestVelocity.z;
		if ( candidateSpeed > bestSpeed )
		{
			bestPosition = candidateTrace.endpos;
			bestVelocity = candidateVelocity;
			bestStep = steps[i];
		}
	}
	if ( bestStep.x > -0.01f && bestStep.x < 0.01f && bestStep.y > -0.01f && bestStep.y < 0.01f && bestStep.z > -0.01f && bestStep.z < 0.01f )
		other->SetAbsOrigin( directTrace.endpos );
	else
		other->SetAbsOrigin( bestPosition );
	ReleaseMovementTraceFilter( &filter );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif
// S2D33F0/C277A50 gates stuck checks, not acceleration (native slots188/104).
// Acceleration retains CCSGameMovement::CanAccelerate, including during a ride.
bool CTerrorGameMovement::CanCheckStuck()
{
	if ( !m_pTerrorPlayer )
		return true;
	if ( m_pTerrorPlayer->IsIncapacitated() || m_pTerrorPlayer->m_isHangingFromLedge ||
		m_pTerrorPlayer->IsBeingPounced() || m_pTerrorPlayer->m_pounceVictim.Get() ||
		m_pTerrorPlayer->IsJockeyVictim() || m_pTerrorPlayer->m_jockeyVictim.Get() ||
		m_pTerrorPlayer->IsPummelVictim() || m_pTerrorPlayer->m_pummelVictim.Get() ||
		m_pTerrorPlayer->IsTongueVictim() || m_pTerrorPlayer->GetTongueVictim() || m_pTerrorPlayer->IsKnockedDown() )
		return false;
	// GetActivity is a pure read. Native helper 2C76C0 is a subset of 2C75D0;
	// its complete union is below, with native 719's explicit exception retained.
	const int activity = LookupTerrorOriginalActivity( m_pTerrorPlayer->GetTerrorActivity() );
	if ( activity == 719 )
		return true;
	switch ( activity )
	{
	case 621: case 622: case 623: case 624: case 625: case 626: case 627: case 628:
	case 629: case 630: case 631: case 632: case 633: case 634: case 635: case 636:
	case 644: case 645: case 646: case 647: case 667: case 669: case 710: case 711:
	case 720: case 722: case 724: case 729: case 731: case 734: case 736:
	case 765: case 766: case 767: case 768: case 792: case 793: case 794: case 795:
		return false;
	default:
		return true;
	}
}


void CTerrorGameMovement::ProcessMovement( CBasePlayer *pPlayer, CMoveData *pMove )
{
#ifndef CLIENT_DLL
	m_lungePushedPlayers.RemoveAll();
#endif
	m_pTerrorPlayer = ToTerrorPlayer( pPlayer );
	BaseClass::ProcessMovement( pPlayer, pMove );
	m_pTerrorPlayer = NULL;
}

Vector CTerrorGameMovement::GetPlayerMins( bool bDucked ) const
{
	return bDucked ? TERROR_VEC_DUCK_HULL_MIN : TERROR_VEC_HULL_MIN;
}

Vector CTerrorGameMovement::GetPlayerMaxs( bool bDucked ) const
{
	return bDucked ? TERROR_VEC_DUCK_HULL_MAX : TERROR_VEC_HULL_MAX;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S2D22F0/C2767C0 delegates to the real actor view-offset policy.
Vector CTerrorGameMovement::GetPlayerViewOffset( bool duck ) const
{
	return m_pTerrorPlayer->GetMovementViewOffset( duck );
}


// S2D35E0/C276C00. Actual existing player max-speed value, NOT a fresh policy
// query: SetupMove/RecalculateMaxSpeed owns that value and its query ordering.
void CTerrorGameMovement::SetMoveDirection( const Vector &direction )
{
	QAngle reference, desired;
	VectorAngles( Vector( 1.0f, 0.0f, 0.0f ), reference );
	VectorAngles( direction, desired );
	const QAngle eyes = m_pTerrorPlayer->EyeAngles();
	reference.y = ( eyes.y - desired.y ) + reference.y;
	Vector forward;
	AngleVectors( reference, &forward );
	const float maxSpeed = m_pTerrorPlayer->MaxSpeed();
	mv->m_flSideMove = maxSpeed * forward.y;
	mv->m_flForwardMove = maxSpeed * forward.x;
	mv->m_flUpMove = 0.0f;
}

// S2D5380/C278BE0. Native Tear path is already established by ProcessMovement.
void CTerrorGameMovement::PlayerMove()
{
	CMDLCacheCriticalSection cacheCriticalSection( mdlcache );
	UpdateStandableNormal();
	if ( m_pTerrorPlayer->IsAutoCrouching() )
		mv->m_nButtons |= IN_DUCK;

	// IsBusy is the genuine virtual S2C9380/C26A510. The independent raw
	// airMovementRestricted flag is consumed later, after the stun branch.
	if ( m_pTerrorPlayer->IsBusy() && !m_pTerrorPlayer->m_jockeyVictim.Get() )
	{
		mv->m_flSideMove = 0.0f;
		mv->m_flUpMove = 0.0f;
		mv->m_nButtons &= 0xFFFFF9EDu;
		if ( !m_pTerrorPlayer->IsIncapacitated() || !survivor_allow_crawling.GetBool() )
		{
			mv->m_flForwardMove = 0.0f;
			mv->m_nButtons &= ~IN_FORWARD;
		}
	}
#ifndef CLIENT_DLL
	if ( m_pTerrorPlayer->m_jockeyAttacker.Get() )
		m_pTerrorPlayer->ApplyJockeyControl( &mv->m_flForwardMove, &mv->m_flSideMove );
#endif

	if ( m_pTerrorPlayer && m_pTerrorPlayer->IsAlive() )
	{
#ifndef CLIENT_DLL
		// FCOMIP/JNB: unordered current-time/deadline enters suppression too.
		if ( !( gpGlobals->curtime >= m_pTerrorPlayer->m_stunTimer.m_timestamp.Get() ) )
		{
			mv->m_nButtons &= 0xFFFFF9E3u;
			if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED )
				mv->m_nButtons &= 0xFFFFF7FEu;
			if ( player->m_Local.m_bDucked )
				mv->m_nButtons |= IN_DUCK;
			mv->m_flForwardMove = 0.0f;
			mv->m_flSideMove = 0.0f;
			mv->m_flUpMove = 0.0f;
		}
#endif
		if ( m_pTerrorPlayer->IsAirMovementRestricted() )
		{
			if ( m_pTerrorPlayer->GetGroundEntity() )
				m_pTerrorPlayer->SetAirMovementRestricted( false );
			else
			{
				mv->m_nButtons &= 0xFFFFF9E3u;
				mv->m_flForwardMove = 0.0f;
				mv->m_flSideMove = 0.0f;
				mv->m_flUpMove = 0.0f;
			}
		}
	}
	if ( m_pTerrorPlayer )
	{
		const Vector *stagger = m_pTerrorPlayer->IsStaggering();
		if ( stagger )
			SetMoveDirection( *stagger );
	}

	if ( m_pTerrorPlayer->m_pushEntity.Get() )
	{
		mv->m_nButtons |= IN_SPEED;
		const Vector origin = mv->GetAbsOrigin();
		Vector nearest;
		m_pTerrorPlayer->m_pushEntity.Get()->CollisionProp()->CalcNearestPoint( origin, &nearest );
		Vector direction( nearest.x - origin.x, nearest.y - origin.y, 0.0f );
		const float desiredDistance = m_pTerrorPlayer->m_pushDistance;
		const float distance = m_pTerrorPlayer->GetPushDistance();
		if ( distance > desiredDistance + 4.0f )
			SetMoveDirection( direction );
		else if ( desiredDistance - 4.0f > distance )
			SetMoveDirection( -direction );
		else
			mv->m_flForwardMove = 0.0f;
	}
	CBaseMountedWeapon *mounted = m_pTerrorPlayer->GetMountedWeapon();
	if ( mounted )
	{
		mv->m_nButtons &= ~IN_SPEED;
		const Vector origin = mv->GetAbsOrigin();
		Vector userPosition;
		mounted->GetUserPosition( &userPosition );
		SetMoveDirection( Vector( userPosition.x - origin.x, userPosition.y - origin.y, 0.0f ) );
	}

	// positionEntity is a movement ground attachment, not SetParent. Resolve
	// twice at the original boundary; do not retain the earlier raw pointer.
	if ( m_pTerrorPlayer && m_pTerrorPlayer->m_positionEntity.Get() )
		m_pTerrorPlayer->SetGroundEntity( m_pTerrorPlayer->m_positionEntity.Get() );
	else
		BaseClass::PlayerMove();

	if ( m_pTerrorPlayer && ( player->GetFlags() & FL_ONGROUND ) )
	{
		if ( m_pTerrorPlayer->GetVelocityModifier() < 1.0f )
			m_pTerrorPlayer->SetVelocityModifier( gpGlobals->frametime * 0.33333334f + m_pTerrorPlayer->GetVelocityModifier() );
		if ( m_pTerrorPlayer->GetVelocityModifier() > 1.0f )
			m_pTerrorPlayer->SetVelocityModifier( 1.0f );
	}
	const Vector viewOffset = m_pTerrorPlayer->ComputeMovementViewOffset( mv->GetAbsOrigin(), mv->m_vecAngles );
	m_pTerrorPlayer->SetViewOffset( viewOffset );
}

// S2D4D40/C277B70. Do not append a rider block to the old partial method.
void CTerrorGameMovement::CheckParameters()
{
	if ( !m_pTerrorPlayer )
	{
		BaseClass::CheckParameters();
		return;
	}
	CheckDuckUntilOnGround();
	CTongue *tongue = NULL;
	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
		m_pTerrorPlayer->m_zombieClass == 1 &&
#else
		m_pTerrorPlayer->GetZombieClass() == 1 &&
#endif
		m_pTerrorPlayer->m_customAbility.Get() )
		tongue = dynamic_cast< CTongue * >( m_pTerrorPlayer->GetCustomAbility() );

	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED && !m_pTerrorPlayer->IsBot() )
	{
		if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
			m_pTerrorPlayer->m_zombieClass == 7 &&
#else
			m_pTerrorPlayer->GetZombieClass() == 7 &&
#endif
			m_pTerrorPlayer->m_zombieState != 2 )
		{
			mv->m_nButtons |= IN_DUCK;
			mv->m_nButtons &= ~IN_JUMP;
		}
	}
	if ( !m_pTerrorPlayer->CanJump() )
		mv->m_nButtons &= ~IN_JUMP;

	if ( m_pTerrorPlayer->m_reviveOwner.Get() )
	{
		mv->m_nButtons &= 0xFFFFF1C5u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->IsIncapacitated() )
	{
		mv->m_nButtons &= 0xFFFFF1CDu;
		mv->m_flUpMove = 0.0f;
		mv->m_flSideMove = 0.0f;
		if ( !survivor_allow_crawling.GetBool() )
		{
			mv->m_nButtons &= ~IN_FORWARD;
			mv->m_flForwardMove = 0.0f;
		}
		else
		{
			if ( mv->m_flForwardMove < 0.0f )
				mv->m_flForwardMove = 0.0f;
			mv->m_flClientMaxSpeed = m_pTerrorPlayer->GetIncapCrawlSpeed();
		}
		goto ParameterTail;
	}
	// Native unordered progress takes the NORMAL path, not the frozen path.
	if ( m_pTerrorPlayer->m_reviveTarget.Get() && m_pTerrorPlayer->GetProgressBarProgress() < 0.1f &&
		!survivor_revive_continuous_fire.GetBool() )
	{
		mv->m_nButtons &= 0xFFFFF9E7u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->GetCustomAbility() && m_pTerrorPlayer->GetCustomAbility()->GetMovementSpeed() >= 0.0f )
	{
		mv->m_nButtons &= 0xFFFDFFFBu;
		mv->m_nButtons |= IN_FORWARD;
		if ( m_pTerrorPlayer->GetCustomAbility() && m_pTerrorPlayer->GetCustomAbility()->GetMaxSpeed() > -1.0f )
		{
			if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
				m_pTerrorPlayer->m_zombieClass == 6 )
#else
				m_pTerrorPlayer->GetZombieClass() == 6 )
#endif
			{
				mv->m_nButtons &= 0xFFFFF1C1u;
				mv->m_flForwardMove = 0.0f;
				mv->m_flUpMove = 0.0f;
				mv->m_flSideMove = 0.0f;
			}
			mv->m_flClientMaxSpeed = m_pTerrorPlayer->GetCustomAbility()->GetMaxSpeed();
		}
		mv->m_flForwardMove = m_pTerrorPlayer->GetCustomAbility()->GetMovementSpeed();
		goto ParameterTail;
	}
	if ( m_pTerrorPlayer->IsBeingPounced() || m_pTerrorPlayer->IsPummelVictim() )
	{
		mv->m_nButtons |= IN_DUCK;
		mv->m_nButtons &= 0xFFFFF1C5u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->IsPouncing() )
	{
		mv->m_nButtons |= IN_DUCK;
		mv->m_nButtons &= 0xFFFFF9C4u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->IsJockeyRiding() )
	{
#ifndef CLIENT_DLL
		m_pTerrorPlayer->m_jockeyRawForwardMove = mv->m_flForwardMove / mv->m_flClientMaxSpeed;
		m_pTerrorPlayer->m_jockeyRawSideMove = mv->m_flSideMove / mv->m_flClientMaxSpeed;
		m_pTerrorPlayer->m_jockeyRawYaw = m_pTerrorPlayer->EyeAngles().y;
		m_pTerrorPlayer->m_jockeyDesiredMove = Vector( m_pTerrorPlayer->m_jockeyRawForwardMove,
			m_pTerrorPlayer->m_jockeyRawSideMove, 0.0f );
		m_pTerrorPlayer->m_jockeyRotatedDesire = !m_pTerrorPlayer->IsBot();
#endif
		mv->m_nButtons &= 0xFFFFF9C0u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->IsJockeyVictim() || m_pTerrorPlayer->IsJockeyRiding() )
	{
		mv->m_nButtons &= 0x618u;
		goto ParameterTail;
	}
	if ( m_pTerrorPlayer->m_pummelVictim.Get() )
	{
		mv->m_nButtons &= 0xFFFFF9C1u;
		goto ClearMotion;
	}
	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
		m_pTerrorPlayer->m_zombieClass == 1 &&
#else
		m_pTerrorPlayer->GetZombieClass() == 1 &&
#endif
		tongue && tongue->IsTongueExtended() )
	{
		mv->m_nButtons &= 0xFFFFFFF9u;
	}
	else if ( ( mv->m_nButtons & IN_DUCK ) ||
#ifdef CLIENT_DLL
		player->IsDucking() ||
#else
		player->m_Local.m_bDucking ||
#endif
		( player->GetFlags() & FL_DUCKING ) )
	{
		if ( player->GetMoveType() != MOVETYPE_NOCLIP )
			mv->m_flClientMaxSpeed = m_pTerrorPlayer ? m_pTerrorPlayer->GetCrouchTopSpeed() : survivor_crouch_speed.GetFloat();
	}
	else if ( mv->m_nButtons & IN_SPEED )
	{
		mv->m_flClientMaxSpeed = m_pTerrorPlayer->GetWalkTopSpeed();
	}
	else if ( player->GetWaterLevel() >= WL_Eyes && player->GetMoveType() != MOVETYPE_NOCLIP )
	{
		mv->m_nButtons &= 0xFFFFF7FCu;
	}
	goto ParameterTail;

ClearMotion:
	mv->m_flForwardMove = 0.0f;
	mv->m_flUpMove = 0.0f;
	mv->m_flSideMove = 0.0f;
ParameterTail:
	CheckSpeedParameters();
	CheckAngleParameters();
	CheckGroundObstacle();
}

// S2D3F00/C277870, native byte284 overrides CCS' ground modifier.
float CTerrorGameMovement::GetSpeedModifier() const
{
	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
		m_pTerrorPlayer->m_zombieClass == 8 &&
#else
		m_pTerrorPlayer->GetZombieClass() == 8 &&
#endif
		TerrorGameRules()->IsPlayerControlledZombies() )
	{
		if ( ( mv->m_nButtons & IN_DUCK ) ||
#ifdef CLIENT_DLL
			player->IsDucking() ||
#else
			player->m_Local.m_bDucking ||
#endif
			( player->GetFlags() & FL_DUCKING ) )
			return 1.0f;
	}
	return BaseClass::GetSpeedModifier();
}

// S2D3F60/C278670, reached ONCE by the genuine CCS post-parameter stage.
void CTerrorGameMovement::DecayPunchAngle()
{
	if ( !m_pTerrorPlayer )
		return;
	float targetRoll;
	if ( m_pTerrorPlayer->IsIncapacitated() && !m_pTerrorPlayer->m_isHangingFromLedge &&
		!m_pTerrorPlayer->m_isHangingFromTongue )
		targetRoll = survivor_incapacitated_roll.GetFloat();
	else if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
#ifdef CLIENT_DLL
		m_pTerrorPlayer->m_zombieClass == 8 &&
#else
		m_pTerrorPlayer->GetZombieClass() == 8 &&
#endif
		m_pTerrorPlayer->IsStaggering() )
		targetRoll = z_tank_grenade_roll.GetFloat();
	else
	{
		BaseClass::DecayPunchAngle();
		return;
	}
	if ( targetRoll == 0.0f )
	{
		BaseClass::DecayPunchAngle();
		return;
	}
	const float oldRoll = m_pTerrorPlayer->m_Local.m_vecPunchAngle->z;
	Vector direction( m_pTerrorPlayer->m_Local.m_vecPunchAngle->x, m_pTerrorPlayer->m_Local.m_vecPunchAngle->y, 0.0f );
	float length = (float)TerrorPhysicsNormalize( direction ); // Native return spills before decay.
	length -= ( length * 0.5f + punch_angle_decay_rate.GetFloat() ) * gpGlobals->frametime;
	if ( !( length > 0.0f ) ) length = 0.0f; // native COMISS/JBE includes NaN.
	const float pitch = length * direction.x;
	const float yaw = (float)( (double)direction.y * (double)length );
	const float roll = ( targetRoll - oldRoll ) * gpGlobals->frametime * 10.0f + oldRoll;
	m_pTerrorPlayer->m_Local.m_vecPunchAngle.SetX( pitch );
	m_pTerrorPlayer->m_Local.m_vecPunchAngle.SetY( yaw );
	m_pTerrorPlayer->m_Local.m_vecPunchAngle.SetZ( roll );
}

ConVar z_lunge_up( "z_lunge_up", "200", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );
ConVar z_player_lunge_up( "z_player_lunge_up", "150", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

namespace
{
bool HasNativeQueuedMovement( const Vector &movement )
{
	// Native COMISS/JBE treats NaN as nonzero/outside the open tolerance.
	return !( movement.x > -0.01f && movement.x < 0.01f && movement.y > -0.01f &&
		movement.y < 0.01f && movement.z > -0.01f && movement.z < 0.01f );
}

bool IsNativeSurvivorTeam( int team )
{
	return team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR;
}
}

// S2D2B10/C276820: landing contact precedes the real base ground setter.
void CTerrorGameMovement::SetGroundEntity( trace_t *trace )
{
	if ( trace && trace->m_pEnt && trace->m_pEnt->IsPlayer() && m_pTerrorPlayer )
	{
#ifdef CLIENT_DLL
		(void)m_pTerrorPlayer->IsLunging();
#else
		if ( m_pTerrorPlayer->IsLunging() && m_pTerrorPlayer->IsAttemptingToPounce() &&
			IsNativeSurvivorTeam( trace->m_pEnt->GetTeamNumber() ) )
			m_pTerrorPlayer->OnPounceContact( static_cast< CTerrorPlayer * >( trace->m_pEnt ) );
#endif
	}
	BaseClass::SetGroundEntity( trace );
	if ( player->GetGroundEntity() )
		m_pTerrorPlayer->RecordLastStandingInfo( mv->GetAbsOrigin(), mv->m_vecVelocity, mv->m_vecViewAngles );
}

// S2D2500: genuine native water-movement capability, separate from CanJump.
bool CTerrorGameMovement::CanSwim() const
{
	if ( m_pTerrorPlayer )
	{
		if ( IsNativeSurvivorTeam( m_pTerrorPlayer->GetTeamNumber() ) ||
			m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED )
			return false;
	}
	return true;
}

// S2D2490: the original swim-sound virtual suppresses actual ghosts only.
void CTerrorGameMovement::PlaySwimSound()
{
	if ( !m_pTerrorPlayer->IsGhost() )
		BaseClass::PlaySwimSound();
}

// S2D6080/C277480. This is shared by ordinary jumping and ability jumping.
void CTerrorGameMovement::PerformJump( const Vector *queuedMovement )
{
	SetGroundEntity( NULL );
	m_pTerrorPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_JUMP, 0 );
	float groundFactor = 1.0f;
	surfacedata_t *pSurfaceData = player->GetSurfaceData();
	if ( pSurfaceData )
		groundFactor = pSurfaceData->game.jumpFactor;
	m_pCSPlayer->SetDuckUntilOnGround( true );
	const float oldOriginZ = mv->GetAbsOrigin().z;
	const double height = SetCrouchForJump( true ) ? 57.0 - (double)( mv->GetAbsOrigin().z - oldOriginZ ) : 57.0;
	const float oldVelocityZ = mv->m_vecVelocity.z;
	mv->m_vecVelocity.z = (float)( sqrt( height * 1600.0 ) * (double)groundFactor );
	if ( m_pTerrorPlayer )
	{
		if ( queuedMovement )
		{
			mv->m_vecVelocity.x = queuedMovement->x;
			mv->m_vecVelocity.y = queuedMovement->y;
			const float up = m_pTerrorPlayer->IsBot() ? z_lunge_up.GetFloat() : z_player_lunge_up.GetFloat();
			mv->m_vecVelocity.z = queuedMovement->z + up;
		}
		const float stamina = m_pTerrorPlayer->GetStamina();
		if ( stamina > 0.0f )
			mv->m_vecVelocity.z = (float)( ( 100.0 - (double)stamina * 0.001 * 19.0 ) * 0.01 * (double)mv->m_vecVelocity.z );
		if ( queuedMovement || ( player->GetTeamNumber() == TEAM_INFECTED && !m_pTerrorPlayer->IsBot() ) )
		{
			if ( m_pTerrorPlayer->GetTeamNumber() != TEAM_INFECTED ||
#ifdef CLIENT_DLL
				m_pTerrorPlayer->m_zombieClass != 5 )
#else
				m_pTerrorPlayer->GetZombieClass() != 5 )
#endif
				m_pTerrorPlayer->SetStamina( 1315.7894f );
		}
	}
	FinishGravity();
	mv->m_outJumpVel.z = ( mv->m_vecVelocity.z - oldVelocityZ ) + mv->m_outJumpVel.z;
	mv->m_outStepHeight += 0.1f;
#ifndef CLIENT_DLL
	IGameEvent *event = gameeventmanager->CreateEvent( "player_jump" );
	if ( event )
	{
		event->SetInt( "userid", player->GetUserID() );
		gameeventmanager->FireEvent( event );
	}
#endif
}

// S2D7CB0/C278190. CheckParameters owns the separate virtual CanJump gate.
bool CTerrorGameMovement::CheckJumpButton()
{
	if ( m_pTerrorPlayer && player->pl.deadflag )
	{
		mv->m_nOldButtons |= IN_JUMP;
		return false;
	}
	if ( player->GetWaterJumpTime() != 0.0f )
	{
		player->SetWaterJumpTime( player->GetWaterJumpTime() - gpGlobals->frametime );
		if ( player->GetWaterJumpTime() < 0.0f ) player->SetWaterJumpTime( 0.0f );
		return false;
	}
	if ( player->GetWaterLevel() >= WL_Eyes && !CanSwim() )
		return false;
	if ( player->GetWaterLevel() >= WL_Waist && CanSwim() )
	{
		SetGroundEntity( NULL );
		if ( player->GetWaterType() == CONTENTS_WATER )
			mv->m_vecVelocity.z = 100.0f;
		else if ( player->GetWaterType() == CONTENTS_SLIME )
			mv->m_vecVelocity.z = 80.0f;
		if ( player->GetSwimSoundTime() <= 0.0f )
		{
			player->SetSwimSoundTime( 1000.0f );
			PlaySwimSound();
		}
		return false;
	}
	if ( !player->GetGroundEntity() )
	{
		mv->m_nOldButtons |= IN_JUMP;
		return false;
	}
	if ( mv->m_nOldButtons & IN_JUMP )
		return false;
	PerformJump( NULL );
#ifndef CLIENT_DLL
	m_pTerrorPlayer->SetLaunchSource( NULL );
#endif
	mv->m_nOldButtons |= IN_JUMP;
	return true;
}

// S2D6330/C279040, native virtual byte136. FullWalkMove calls this once after
// ordinary IN_JUMP processing in BOTH water and non-water branches.
void CTerrorGameMovement::HandleAbilityJump()
{
	if ( m_pTerrorPlayer && HasNativeQueuedMovement( m_pTerrorPlayer->m_shoveForce ) )
	{
		if ( m_pTerrorPlayer->IsBeingPounced() )
			m_pTerrorPlayer->SetShoveForce( vec3_origin );
		else if ( !m_pTerrorPlayer->IsPouncing() )
		{
			if ( player->GetMoveType() == MOVETYPE_LADDER )
			{
				player->SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );
				SetGroundEntity( NULL );
			}
			m_pTerrorPlayer->SetShoveForce( vec3_origin );
			return;
		}
	}
	CBaseAbility *ability = m_pTerrorPlayer->GetCustomAbility();
	Vector *queued = ability ? ability->GetQueuedMovement( true ) : NULL;
	if ( m_pTerrorPlayer && player->pl.deadflag )
	{
		if ( queued && HasNativeQueuedMovement( *queued ) )
			queued->Init( 0.0f, 0.0f, 0.0f );
		mv->m_nOldButtons |= IN_ATTACK;
		return;
	}
	if ( player->GetWaterLevel() > WL_Waist )
	{
		if ( queued && HasNativeQueuedMovement( *queued ) )
			queued->Init( 0.0f, 0.0f, 0.0f );
		return;
	}
	if ( !ability || !queued || !HasNativeQueuedMovement( *queued ) )
		return;
	const Vector movement = *queued;
	queued->Init( 0.0f, 0.0f, 0.0f );
	if ( player->GetMoveType() == MOVETYPE_LADDER )
	{
		player->SetMoveType( MOVETYPE_WALK, MOVECOLLIDE_DEFAULT );
		SetGroundEntity( NULL );
	}
#ifdef CLIENT_DLL
	if ( player->IsDucked() )
#else
	if ( player->m_Local.m_bDucked )
#endif
	{
		Vector end = mv->GetAbsOrigin();
		end.z += 20.0f;
		trace_t trace;
		TracePlayerBBox( mv->GetAbsOrigin(), end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
		if ( trace.fraction > 0.0f )
			mv->SetAbsOrigin( trace.endpos );
	}

	if ( AngleNormalize( mv->m_vecAbsViewAngles.x ) > -20.0f )
	{
		Vector forward, right, up;
		AngleVectors( mv->m_vecAbsViewAngles, &forward, &right, &up );
		if ( HasNativeQueuedMovement( up ) )
		{
			VectorNormalize( up );
			Vector best = mv->GetAbsOrigin();
			float bestFraction = 0.0f;
			Vector candidate;
			for ( float offset = 0.0f; offset <= 64.0f; offset += 8.0f )
			{
				const Vector displacement = up * offset;
				trace_t trace;
				TracePlayerBBox( mv->GetAbsOrigin(), mv->GetAbsOrigin() + displacement,
					PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
				if ( !trace.startsolid && trace.fraction > 0.0f )
				{
					candidate = mv->GetAbsOrigin() + displacement * trace.fraction;
					TracePlayerBBox( candidate, candidate + forward * 128.0f,
						PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
					if ( !trace.startsolid )
					{
						if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() && IsNativeSurvivorTeam( trace.m_pEnt->GetTeamNumber() ) )
						{
							mv->SetAbsOrigin( candidate );
							goto Launch;
						}
						if ( trace.fraction > bestFraction )
						{
							bestFraction = trace.fraction;
							best = candidate;
							if ( trace.fraction == 1.0f ) break;
						}
					}
				}
			}
			if ( bestFraction < 1.0f )
			{
				VectorNormalize( right );
				for ( float offset = -32.0f; offset <= 32.0f; offset += 8.0f )
				{
					const Vector displacement = right * offset;
					trace_t trace;
					TracePlayerBBox( mv->GetAbsOrigin(), mv->GetAbsOrigin() + displacement,
						PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
					if ( !trace.startsolid && trace.fraction > 0.0f )
					{
						candidate = mv->GetAbsOrigin() + displacement * trace.fraction;
						TracePlayerBBox( candidate, candidate + forward * 128.0f,
							PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, trace );
						if ( !trace.startsolid )
						{
							if ( trace.m_pEnt && trace.m_pEnt->IsPlayer() && IsNativeSurvivorTeam( trace.m_pEnt->GetTeamNumber() ) )
							{
								mv->SetAbsOrigin( candidate );
								goto Launch;
							}
							if ( trace.fraction > bestFraction )
							{
								bestFraction = trace.fraction;
								best = candidate;
								if ( trace.fraction == 1.0f ) break;
							}
						}
					}
				}
			}
			if ( bestFraction > 0.0f )
				mv->SetAbsOrigin( best );
		}
	}
Launch:
	PerformJump( &movement );
}

// S2D24C0/C2769C0: only ghosts retain the faster ladder speed.
float CTerrorGameMovement::ClimbSpeed() const
{
	return m_pTerrorPlayer->IsGhost() ? 200.0f : 100.0f;
}

// S2D36A0/C278050. This is the actual ladder virtual, byte164. Do not put
// byte136's queued jump body here. Existing CCS owns native history recording.
bool CTerrorGameMovement::LadderMove()
{
#ifndef CLIENT_DLL
	if ( !m_pTerrorPlayer )
		return BaseClass::LadderMove();
#endif
	if ( m_pTerrorPlayer && ( m_pTerrorPlayer->IsIncapacitated() ||
		m_pTerrorPlayer->IsBeingPounced() || m_pTerrorPlayer->IsPouncing() ||
		m_pTerrorPlayer->IsBeingCarried() || m_pTerrorPlayer->m_carryVictim.Get() ||
		m_pTerrorPlayer->IsPummelVictim() || m_pTerrorPlayer->m_pummelVictim.Get() ||
		m_pTerrorPlayer->IsJockeyVictim() || m_pTerrorPlayer->IsJockeyRiding() ||
		m_pTerrorPlayer->m_usingMountedWeapon ) )
		return false;
	const bool onLadder = BaseClass::LadderMove();
#ifndef CLIENT_DLL
	if ( onLadder )
		m_pTerrorPlayer->m_canGrabLedge = false;
#endif
	return onLadder;
}

#ifndef CLIENT_DLL
namespace
{
// S2D4C80/2D78E0: the original CUtlVector reserves32, does NOT truncate32.
// It walks the actual NextBot linked-list registry in registration order.
class CollectGroundOverlappingInfected
{
public:
	CollectGroundOverlappingInfected( const Vector &mins, const Vector &maxs )
		: m_mins( mins ), m_maxs( maxs ), m_entities( 0, 32 ) {}
	bool operator()( INextBot *bot )
	{
		CBaseCombatCharacter *entity = bot->GetEntity();
		if ( entity )
		{
			Infected *infected = entity->MyInfectedPointer();
			if ( infected && !infected->IsEFlagSet( EFL_KILLME ) )
			{
				Vector mins, maxs;
				infected->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
				if ( maxs.x >= m_mins.x && m_maxs.x >= mins.x && maxs.y >= m_mins.y &&
					m_maxs.y >= mins.y && maxs.z >= m_mins.z && m_maxs.z >= mins.z )
					m_entities.AddToTail( infected );
			}
		}
		return true;
	}
	Vector m_mins, m_maxs;
	CUtlVector< CBaseEntity * > m_entities;
};
}
#endif

// S2D7A20/C276CC0, genuinely virtual after speed and angles. This does not
// invoke the base stacking implementation as well as the Terror override.
void CTerrorGameMovement::CheckGroundObstacle()
{
	if ( ( m_pTerrorPlayer && m_pTerrorPlayer->IsObserver() ) || player->GetMoveType() == MOVETYPE_LADDER )
		return;
	CBaseEntity *ground = player->GetGroundEntity();
	if ( !ground || m_pTerrorPlayer->IsLunging() || !ground->MyCombatCharacterPointer() )
		return;
#ifndef CLIENT_DLL
	if ( !ground->IsPlayer() )
	{
		Infected *infected = ground->MyInfectedPointer();
		if ( infected && ( infected->GetTeamNumber() != TEAM_INFECTED || infected->GetZombieClass() == 0 ) )
		{
			Vector mins = g_pGameRules->GetViewVectors()->m_vDuckHullMin + player->GetLocalOrigin();
			const Vector maxs = g_pGameRules->GetViewVectors()->m_vDuckHullMax + player->GetLocalOrigin();
			mins.z -= 18.0f;
			CollectGroundOverlappingInfected collect( mins, maxs );
			TheNextBots().ForEachBot( collect );
			for ( int i = 0; i < collect.m_entities.Count(); ++i )
			{
				CTakeDamageInfo damage( player, player, 50.0f, DMG_CLUB );
				collect.m_entities[i]->TakeDamage( damage );
			}
			return;
		}
	}
#else
	if ( !ground->IsPlayer() )
	{
		C_Infected *infected = ground->MyInfectedPointer();
		if ( infected && infected->GetRagdollType() == 1 )
			return;
	}
#endif
	Vector direction( mv->m_vecVelocity.x, mv->m_vecVelocity.y, 0.0f );
	if ( direction.x > -0.0000099999997f && direction.x < 0.0000099999997f &&
		direction.y > -0.0000099999997f && direction.y < 0.0000099999997f &&
		direction.z > -0.0000099999997f && direction.z < 0.0000099999997f )
	{
		direction.x = RandomFloat( -1.0f, 1.0f );
		direction.y = RandomFloat( -1.0f, 1.0f );
	}
	VectorNormalize( direction );
	SetMoveDirection( direction );
	mv->m_flForwardMove *= 3.0f;
	mv->m_flSideMove *= 3.0f;
	mv->m_nButtons = 0;
	mv->m_nImpulseCommand = 0;
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void CTerrorGameMovement::HandleDuckingSpeedCrop()
{
	// The original override (0x2D22E0) is empty: CheckParameters already
	// selects the crouch maximum. Do not apply the foundation's extra 1/3.
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

// S2D81F0/C27A8B0: mounted, tongue, stamina/ability, then ordinary movement.
void CTerrorGameMovement::WalkMove()
{
#ifndef CLIENT_DLL
	if ( player->GetGroundEntity() )
		m_pTerrorPlayer->m_canGrabLedge = false;
#endif
	CBaseMountedWeapon *mounted = m_pTerrorPlayer->GetMountedWeapon();
	if ( mounted )
	{
		Vector destination;
		mounted->GetUserPosition( &destination );
		const Vector origin = mv->GetAbsOrigin();
		destination.z = origin.z;
		const Vector delta = destination - origin;
		const float distanceSquared = ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z;
		if ( distanceSquared > 1.0f )
		{
			MoveGround( destination, GetStandableNormal() );
			CategorizePosition( true );
		}
		return;
	}
	if ( m_pTerrorPlayer && m_pTerrorPlayer->IsTongueVictim() && FullTongueMove() )
	{
		CategorizePosition( false );
		return;
	}

	const bool applyStamina = m_pTerrorPlayer && m_pTerrorPlayer->GetStamina() > 0.0f &&
		!IsNativeSurvivorTeam( m_pTerrorPlayer->GetTeamNumber() );
	bool abilityOwnsMovement = false;
	if ( m_pTerrorPlayer &&
#ifdef CLIENT_DLL
		m_pTerrorPlayer->GetCustomAbility() )
#else
		m_pTerrorPlayer->m_customAbility.Get() )
#endif
	{
		CBaseAbility *ability = m_pTerrorPlayer->GetCustomAbility();
		if ( ability )
			abilityOwnsMovement = ability->GetMovementSpeed() >= 0.0f;
	}
	if ( !abilityOwnsMovement && applyStamina )
	{
		const double staminaScale = pow(
			( 100.0 - static_cast< double >( m_pTerrorPlayer->GetStamina() ) * 0.001 * 19.0 ) * 0.01,
			static_cast< double >( gpGlobals->frametime ) * 70.0 );
		mv->m_vecVelocity.x = static_cast< float >( static_cast< double >( mv->m_vecVelocity.x ) * staminaScale );
		mv->m_vecVelocity.y = static_cast< float >( staminaScale * static_cast< double >( mv->m_vecVelocity.y ) );
	}

	Vector forward, right;
	AngleVectors( mv->m_vecViewAngles, &forward, &right, NULL );
	forward.z = 0.0f;
	right.z = 0.0f;
	VectorNormalize( forward );
	VectorNormalize( right );
	Vector wishdir( right.x * mv->m_flSideMove + forward.x * mv->m_flForwardMove,
		right.y * mv->m_flSideMove + forward.y * mv->m_flForwardMove, 0.0f );
	float wishSpeed = VectorNormalize( wishdir );
	if ( wishSpeed > mv->m_flMaxSpeed )
		wishSpeed = mv->m_flMaxSpeed;
	else if ( wishSpeed < 0.0f )
		wishSpeed = 0.0f;

	CBaseEntity *pounceVictim = m_pTerrorPlayer->m_pounceVictim.Get();
	if ( pounceVictim )
	{
		const Vector &origin = m_pTerrorPlayer->GetAbsOrigin();
		const Vector &victimOrigin = pounceVictim->GetAbsOrigin();
		wishdir.Init( victimOrigin.x - origin.x, victimOrigin.y - origin.y, 0.0f );
		wishSpeed = VectorNormalize( wishdir );
		if ( wishSpeed > mv->m_flMaxSpeed )
			wishSpeed = mv->m_flMaxSpeed;
		else if ( wishSpeed < 0.0f )
			wishSpeed = 0.0f;
	}
	CBaseEntity *pummelVictim = m_pTerrorPlayer->m_pummelVictim.Get();
	if ( pummelVictim )
	{
		const Vector &origin = m_pTerrorPlayer->GetAbsOrigin();
		const Vector &victimOrigin = pummelVictim->GetAbsOrigin();
		wishdir.Init( victimOrigin.x - origin.x, victimOrigin.y - origin.y, 0.0f );
		wishSpeed = VectorNormalize( wishdir );
		if ( wishSpeed > mv->m_flMaxSpeed )
			wishSpeed = mv->m_flMaxSpeed;
		else if ( wishSpeed < 0.0f )
			wishSpeed = 0.0f;
	}

	mv->m_vecVelocity.z = 0.0f;
	float acceleration = sv_accelerate.GetFloat();
	if ( m_pTerrorPlayer->IsGhost() &&
#ifdef CLIENT_DLL
		!player->IsDucked() &&
#else
		!m_pTerrorPlayer->m_Local.m_bDucked &&
#endif
		!( mv->m_nButtons & IN_SPEED ) )
	{
		if ( mv->m_vecVelocity.x * mv->m_vecVelocity.x + mv->m_vecVelocity.y * mv->m_vecVelocity.y > 102400.0f )
			acceleration *= 0.8f;
		else if ( mv->m_vecVelocity.x * mv->m_vecVelocity.x + mv->m_vecVelocity.y * mv->m_vecVelocity.y > 48400.0f )
			acceleration *= 0.7f;
		else
			acceleration *= 0.5f;
	}
	else if ( m_pTerrorPlayer->IsCrawling() )
	{
		acceleration *= 10.0f;
	}
	Accelerate( wishdir, wishSpeed, acceleration );

	const float speed = sqrtf( ( mv->m_vecVelocity.x * mv->m_vecVelocity.x +
		mv->m_vecVelocity.y * mv->m_vecVelocity.y ) + mv->m_vecVelocity.z * mv->m_vecVelocity.z );
	if ( speed > mv->m_flMaxSpeed )
	{
		const float scale = mv->m_flMaxSpeed / speed;
		mv->m_vecVelocity.x *= scale;
		mv->m_vecVelocity.y *= scale;
	}
	mv->m_vecVelocity += player->GetBaseVelocity();
	if ( sqrtf( ( mv->m_vecVelocity.x * mv->m_vecVelocity.x + mv->m_vecVelocity.y * mv->m_vecVelocity.y ) +
		mv->m_vecVelocity.z * mv->m_vecVelocity.z ) < 1.0f )
	{
		mv->m_vecVelocity.Init();
		return;
	}

	Vector destination = mv->GetAbsOrigin();
	destination.x += mv->m_vecVelocity.x * gpGlobals->frametime;
	destination.y += mv->m_vecVelocity.y * gpGlobals->frametime;
	MoveGround( destination, GetStandableNormal() );
	mv->m_vecVelocity -= player->GetBaseVelocity();
	mv->m_outWishVel += wishdir * wishSpeed;
	StayOnGround();
	OnFinishWalkMove( player->GetGroundEntity() != NULL );
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
void CTerrorGameMovement::TraceGroundMoveHull( const Vector &start, const Vector &end,
	const Vector &mins, const Vector &maxs, unsigned int mask, ITraceFilter *filter, trace_t &trace )
{
	TracePlayerBBoxWithFilter( start, end, mins, maxs, mask, filter, trace );
	if ( m_bProcessingMovement && !trace.startsolid && trace.fraction < 1.0f )
		OnPlayerMoveCollision( start, end, &trace );
}

void CTerrorGameMovement::TryGroundMove( CTerrorPlayer *movingPlayer, const Vector &vecStart, const Vector &vecDestination, const Vector *pStep,
	float flStandableNormal, Vector &vecVelocity, trace_t &trace, ITraceFilter *filter, unsigned int mask )
{
	// 0x2D6BC0 slides displacement (not the incoming velocity) up to three
	// times, then reconstructs velocity from the actual displacement.
	const CViewVectors *views = g_pGameRules->GetViewVectors();
	Vector mins = views->m_vHullMin, maxs = views->m_vHullMax;
	mins.z = movingPlayer->m_Local.m_bDucked ? views->m_vDuckHullMin.z : views->m_vHullMin.z;
	maxs.z = movingPlayer->m_Local.m_bDucked ? views->m_vDuckHullMax.z : views->m_vHullMax.z;
	Vector vecDelta = vecDestination - vecStart;
	Vector vecMoveStart = vecStart;
	Vector vecMoveEnd = vecDestination;
	Vector vecStepTaken = vec3_origin;
	if ( pStep )
	{
		TracePlayerBBoxWithFilter( vecStart, vecStart + *pStep, mins, maxs, mask, filter, trace );
		if ( trace.fraction > 0.0f && !trace.startsolid )
		{
			vecMoveStart = trace.endpos;
			vecStepTaken = vecMoveStart - vecStart;
			vecMoveEnd += vecStepTaken;
		}
	}

	TraceGroundMoveHull( vecMoveStart, vecMoveEnd, mins, maxs, mask, filter, trace );
	if ( trace.fraction < 1.0f )
	{
		if ( trace.m_pEnt && m_bProcessingMovement )
		{
			m_groundMoveContacts.AddToTail( trace.m_pEnt );
			MoveHelper()->AddToTouched( trace, vecVelocity );
		}
		if ( trace.plane.normal.z < flStandableNormal )
		{
			trace.plane.normal.z = 0.0f;
			// Native S4EA090 widens the norm, then spills before reciprocal.
			const double length = sqrt( ( (double)trace.plane.normal.x * trace.plane.normal.x +
				(double)trace.plane.normal.y * trace.plane.normal.y ) + (double)trace.plane.normal.z * trace.plane.normal.z );
			const float roundedLength = (float)length;
			const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
			trace.plane.normal *= inverseLength;
		}
		vecDelta *= 1.0f - trace.fraction;
		vecDelta -= trace.plane.normal * ( ( trace.plane.normal.y * vecDelta.y + trace.plane.normal.x * vecDelta.x ) + trace.plane.normal.z * vecDelta.z );
		vecMoveStart = trace.endpos;
		TraceGroundMoveHull( vecMoveStart, vecMoveStart + vecDelta, mins, maxs, mask, filter, trace );
		if ( trace.fraction == 0.0f && vecDelta.z < 0.0f )
		{
			vecDelta.z = 0.0f;
			TraceGroundMoveHull( vecMoveStart, vecMoveStart + vecDelta, mins, maxs, mask, filter, trace );
		}
		if ( trace.fraction < 1.0f )
		{
			if ( trace.m_pEnt && m_bProcessingMovement )
			{
				m_groundMoveContacts.AddToTail( trace.m_pEnt );
				MoveHelper()->AddToTouched( trace, vecVelocity );
			}
			vecDelta *= 1.0f - trace.fraction;
			vecDelta -= trace.plane.normal * ( ( trace.plane.normal.y * vecDelta.y + trace.plane.normal.x * vecDelta.x ) + trace.plane.normal.z * vecDelta.z );
			vecMoveStart = trace.endpos;
			TraceGroundMoveHull( vecMoveStart, vecMoveStart + vecDelta, mins, maxs, mask, filter, trace );
		}
	}

	if ( !( vecStepTaken.x > -0.01f && vecStepTaken.x < 0.01f &&
		vecStepTaken.y > -0.01f && vecStepTaken.y < 0.01f &&
		vecStepTaken.z > -0.01f && vecStepTaken.z < 0.01f ) )
	{
		vecMoveStart = trace.endpos;
		TracePlayerBBoxWithFilter( vecMoveStart, vecMoveStart - vecStepTaken, mins, maxs, mask, filter, trace );
	}
	if ( !( gpGlobals->frametime > 0.0f ) )
		vecVelocity.Init();
	else
	{
		Vector displacement = trace.endpos - vecStart;
		bool climbing = false;
		switch ( LookupTerrorOriginalActivity( movingPlayer->GetTerrorActivity() ) )
		{
		case 710: case 711: case 719: case 720: case 722: case 724: case 729: case 731: case 734: case 736:
			climbing = true;
			break;
		default:
			break;
		}
		if ( !climbing && !movingPlayer->IsTongueVictim() )
			displacement.z = 0.0f;
		vecVelocity = displacement * ( 1.0f / gpGlobals->frametime );
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
void CTerrorGameMovement::MoveGround( const Vector &vecDestination, float flStandableNormal )
{
	ITraceFilter *filter = GetMovementTraceFilter( COLLISION_GROUP_PLAYER_MOVEMENT );
	m_groundMoveContacts.RemoveAll();
	const Vector start = mv->GetAbsOrigin();
	Vector downVelocity = mv->m_vecVelocity;
	trace_t downTrace;
	TryGroundMove( m_pTerrorPlayer, start, vecDestination, NULL, flStandableNormal,
		downVelocity, downTrace, filter, PlayerSolidMask() );
	const Vector error = downTrace.endpos - vecDestination;
	const bool reachedDestination = error.x > -0.01f && error.x < 0.01f &&
		error.y > -0.01f && error.y < 0.01f && error.z > -0.01f && error.z < 0.01f;
	trace_t upTrace;
	Vector upVelocity;
	trace_t *selectedTrace = &downTrace;
	const Vector *selectedVelocity = &downVelocity;
	bool stepped = false;
	if ( !reachedDestination )
	{
		const Vector step( 0.0f, 0.0f, player->m_Local.m_flStepSize );
		upVelocity = mv->m_vecVelocity;
		TryGroundMove( m_pTerrorPlayer, start, vecDestination, &step, flStandableNormal,
			upVelocity, upTrace, filter, PlayerSolidMask() );
		const float upSpeed = ( upVelocity.y * upVelocity.y + upVelocity.x * upVelocity.x ) + upVelocity.z * upVelocity.z;
		const float downSpeed = ( downVelocity.y * downVelocity.y + downVelocity.x * downVelocity.x ) + downVelocity.z * downVelocity.z;
		if ( upTrace.plane.normal.z > flStandableNormal && upSpeed > downSpeed )
		{
			selectedTrace = &upTrace;
			selectedVelocity = &upVelocity;
			stepped = true;
		}
	}
#ifndef CLIENT_DLL
	// S2D803E..807D / S2D8120..8159: veto the selected result only.
	// A displaced player bypasses the query; ANSWER_UNDEFINED is accepted.
	if ( !m_pTerrorPlayer->GetDisplacingEntity() )
	{
		INextBot *bot = player->MyNextBotPointer();
		if ( bot && bot->GetIntentionInterface()->IsPositionAllowed( bot, selectedTrace->endpos ) == ANSWER_NO )
		{
			ReleaseMovementTraceFilter( &filter );
			return;
		}
	}
#endif
	CheckAutoCrouch( vecDestination, *selectedTrace, stepped, filter );
	mv->SetAbsOrigin( selectedTrace->endpos );
	mv->m_vecVelocity = *selectedVelocity;
	ReleaseMovementTraceFilter( &filter );
}

void CTerrorGameMovement::CheckAutoCrouch( const Vector &vecDestination,
	const trace_t &trace, bool bStepped, ITraceFilter *filter )
{
	if ( !m_pTerrorPlayer || player->m_Local.m_bDucked || !m_pTerrorPlayer->IsAutoCrouchEnabled() )
		return;
	const float dx = trace.endpos.x - vecDestination.x;
	const float dy = trace.endpos.y - vecDestination.y;
	// S2D3C6E/71 rejects only an ordered distance below .01.
	if ( dy * dy + dx * dx < 0.01f )
		return;
	FOR_EACH_VEC( m_groundMoveContacts, i )
	{
		CBaseEntity *contact = m_groundMoveContacts[i];
		if ( contact && m_pTerrorPlayer->IsUseableEntity( contact, 0 ) )
			return;
	}
	const CViewVectors *views = g_pGameRules->GetViewVectors();
	Vector mins = views->m_vHullMin;
	Vector maxs = views->m_vHullMax;
	mins.z = views->m_vDuckHullMin.z;
	maxs.z = views->m_vDuckHullMax.z;
	Vector probeStart = trace.startpos;
	Vector probeEnd = vecDestination;
	if ( !bStepped && gpGlobals->tickcount % 2 != 0 )
	{
		probeStart.z += player->m_Local.m_flStepSize;
		probeEnd.z += player->m_Local.m_flStepSize;
	}
	trace_t duckTrace;
	TracePlayerBBoxWithFilter( probeStart, probeEnd, mins, maxs, PlayerSolidMask(), filter, duckTrace );
	if ( !duckTrace.startsolid && duckTrace.fraction < 1.0f )
		OnPlayerMoveCollision( probeStart, probeEnd, &duckTrace );
	// S2D3E1D/24: unordered is rejected after the possibly mutating hook.
	if ( !( duckTrace.fraction >= 1.0f ) || duckTrace.startsolid )
		return;
	if ( !bStepped && gpGlobals->tickcount % 2 != 0 )
	{
		probeStart = duckTrace.endpos;
		probeEnd = probeStart;
		probeEnd.z -= player->m_Local.m_flStepSize;
		TracePlayerBBoxWithFilter( probeStart, probeEnd, mins, maxs, PlayerSolidMask(), filter, duckTrace );
		if ( !duckTrace.startsolid && duckTrace.fraction < 1.0f )
			OnPlayerMoveCollision( probeStart, probeEnd, &duckTrace );
		// S2D3ED0/DE: unordered fraction/normal is accepted at this gate.
		if ( duckTrace.fraction < 1.0f && duckTrace.plane.normal.z < 0.7f )
			return;
	}
	m_pTerrorPlayer->StartAutoCrouch();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S2D3250, first in the real PlayerMove path.
void CTerrorGameMovement::UpdateStandableNormal()
{
	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED && m_pTerrorPlayer->GetZombieClass() == 3 &&
		player->m_Local.m_bDucked )
	{
		m_flStandableNormal = z_hunter_ground_normal.GetFloat();
	}
	else if ( m_pTerrorPlayer->m_tongueOwner.Get() )
	{
		const Vector origin = mv->GetAbsOrigin();
		trace_t trace;
		TracePlayerBBox( origin, origin - Vector( 0.0f, 0.0f, 24.0f ), PlayerSolidMask(),
			COLLISION_GROUP_PLAYER_MOVEMENT, trace );
		m_flStandableNormal = trace.plane.normal.z >= 0.7f ? -1.0f : 0.7f;
	}
	else
	{
		m_flStandableNormal = 0.7f;
	}
}

// S2D22A0/C276770 pass the real bool argument and record grounded movement.
void CTerrorGameMovement::CategorizePosition( bool bForceMoveToEndPos )
{
	BaseClass::CategorizePosition( bForceMoveToEndPos );
	if ( player->GetGroundEntity() )
		m_pCSPlayer->RecordLastStandingInfo( mv->GetAbsOrigin(), mv->m_vecVelocity, mv->m_vecViewAngles );
}

// S2D27A0/C276B30 differ: only the server additionally checks displacement.
bool CTerrorGameMovement::ShouldStayOnGround()
{
	if ( !m_pTerrorPlayer )
		return true;
	if ( m_pTerrorPlayer->m_tongueOwner.Get() )
		return false;
#ifndef CLIENT_DLL
	if ( m_pTerrorPlayer->GetDisplacingEntity() )
		return false;
#endif
	return true;
}

// S2D8960/C27AFB0 consume authored motion; it is not absolute velocity.
void CTerrorGameMovement::FullCustomMove()
{
	if ( m_pTerrorPlayer )
	{
		if ( m_pTerrorPlayer->m_isHangingFromLedge )
		{
			FullLedgeMove();
			return;
		}

		if ( CTerrorPlayer::IsAttackBlockingActivity( m_pTerrorPlayer->GetTerrorActivity() ) ||
			CTerrorPlayer::IsClimbingActivity( m_pTerrorPlayer->GetTerrorActivity() ) )
		{
			if ( !m_pTerrorPlayer->IsStaggering() )
			{
				Vector movement;
				m_pTerrorPlayer->GetTerrorAnimState()->GetAnimationMovement( &movement );
				m_pTerrorPlayer->GetTerrorAnimState()->ResetAnimationMovement();
				bool bDirectTankMovement = false;
				if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED && m_pTerrorPlayer->GetZombieClass() == 8 )
				{
					switch ( LookupTerrorOriginalActivity( m_pTerrorPlayer->GetTerrorActivity() ) )
					{
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_38_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_50_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_70_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_115_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_130_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_150_FROM_STAND:
					case TERROR_ORIGINAL_ACT_TERROR_CLIMB_166_FROM_STAND:
						bDirectTankMovement = true;
						break;
					default:
						break;
					}
				}
				if ( bDirectTankMovement )
				{
					mv->SetAbsOrigin( mv->GetAbsOrigin() + movement );
					const float flInverseFrameTime = 1.0f / gpGlobals->frametime;
					mv->m_vecVelocity = movement * flInverseFrameTime;
				}
				else
				{
					MoveGround( mv->GetAbsOrigin() + movement, GetStandableNormal() );
				}
				if ( !CTerrorPlayer::IsClimbingActivity( m_pTerrorPlayer->GetTerrorActivity() ) )
					StayOnGround();
				CategorizePosition( false );
				return;
			}
		}
		if ( m_pTerrorPlayer->IsStaggeringOrPending() )
		{
			Vector movement;
			m_pTerrorPlayer->GetTerrorAnimState()->GetAnimationMovement( &movement );
			m_pTerrorPlayer->GetTerrorAnimState()->ResetAnimationMovement();
			MoveGround( mv->GetAbsOrigin() + movement, GetStandableNormal() );
			StayOnGround();
			CategorizePosition( false );
#ifndef CLIENT_DLL
			if ( !m_pTerrorPlayer->GetGroundEntity() )
				m_pTerrorPlayer->ClearStagger();
#endif
			return;
		}
	}
	player->SetMoveType( MOVETYPE_WALK );
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S2D3820 validates real support and transform; C277660 has no such test.
void CTerrorGameMovement::FullLedgeMove()
{
	mv->m_vecVelocity.Init();
#ifndef CLIENT_DLL
	if ( !m_pTerrorPlayer->m_isHangingFromLedge )
	{
		player->SetMoveType( MOVETYPE_WALK );
		return;
	}
	CBaseEntity *surface = m_pTerrorPlayer->m_hangSurface.Get();
	if ( !surface || m_pTerrorPlayer->m_hangSurfaceOrigin != surface->GetAbsOrigin() ||
		m_pTerrorPlayer->m_hangSurfaceAngles != surface->GetAbsAngles() )
	{
		m_pTerrorPlayer->SelfReleaseLedge();
		return;
	}
	const float flTimestamp = m_pTerrorPlayer->m_hangTimer.GetStartTime();
	const double flElapsed = flTimestamp > 0.0f ?
		static_cast< double >( gpGlobals->curtime ) - static_cast< double >( flTimestamp ) :
		static_cast< double >( 99999.9f );
	if ( flElapsed < 1.0 )
	{
		const Vector offset = mv->GetAbsOrigin() - m_pTerrorPlayer->m_hangPos.Get();
		const float flDistance = sqrtf( ( offset.y * offset.y + offset.z * offset.z ) + offset.x * offset.x );
		if ( flDistance <= 1.0f )
			mv->SetAbsOrigin( m_pTerrorPlayer->m_hangPos );
		else
			mv->SetAbsOrigin( m_pTerrorPlayer->m_hangPos.Get() * 0.8f +
				mv->GetAbsOrigin() * ( 1.0f - 0.8f ) );
		return;
	}
#endif
	if ( m_pTerrorPlayer->GetTerrorAnimState()->IsLedgeClimbAnimation() )
	{
		Vector movement;
		m_pTerrorPlayer->GetTerrorAnimState()->GetAnimationMovement( &movement );
		m_pTerrorPlayer->GetTerrorAnimState()->ResetAnimationMovement();
		QAngle angles;
		const Vector &hangNormal = m_pTerrorPlayer->m_hangNormal;
		VectorAngles( -hangNormal, angles );
		VectorYawRotate( movement, angles.y, movement );
		mv->SetAbsOrigin( mv->GetAbsOrigin() + movement );
	}
	else
	{
		mv->SetAbsOrigin( m_pTerrorPlayer->m_hangPos );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif


// S2D2DF0/C2771C0 use the real authored ladder normal, team and surface.
bool CTerrorGameMovement::OnLadder( trace_t &trace )
{
	if ( trace.fraction == 1.0f || m_pTerrorPlayer->IsStaggering() ||
		!m_pTerrorPlayer->CanPerformBackpackAction() )
		return false;
	switch ( LookupTerrorOriginalActivity( m_pTerrorPlayer->GetTerrorActivity() ) )
	{
	case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_TANKPUNCH:
	case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_TANKPUNCH:
	case TERROR_ORIGINAL_ACT_TERROR_TANKPUNCH_LAND:
	case TERROR_ORIGINAL_ACT_TERROR_HIT_BY_CHARGER:
	case TERROR_ORIGINAL_ACT_TERROR_IDLE_FALL_FROM_CHARGERHIT:
	case TERROR_ORIGINAL_ACT_TERROR_CHARGERHIT_LAND_SLOW:
		return false;
	default:
		break;
	}
	if ( !BaseClass::OnLadder( trace ) )
		return false;
	CFuncSimpleLadder *ladder = dynamic_cast< CFuncSimpleLadder * >( trace.m_pEnt );
	if ( ladder )
	{
		if ( ladder->GetTeamNumber() != 0 && ladder->GetTeamNumber() != player->GetTeamNumber() )
			return false;
		Vector normal = ladder->GetClimbableNormal();
		Vector plane = trace.plane.normal;
		if ( normal.z < 1.0f && plane.z < 1.0f )
		{
			normal.z = 0.0f;
			plane.z = 0.0f;
			VectorNormalize( normal );
			VectorNormalize( plane );
			if ( normal.z < 1.0f && DotProduct( plane, normal ) < 0.9f )
				return false;
		}
	}
	else if ( ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == 4 ) &&
		( !Q_strcmp( trace.surface.name, "TOOLS/CLIMB_VERSUS" ) || !Q_strcmp( trace.surface.name, "noclimb" ) ) )
	{
		return false;
	}
	if ( m_pTerrorPlayer->IsTongueVictim() )
		return false;
	if ( player->GetMoveType() != MOVETYPE_LADDER && player->GetGroundEntity() )
	{
		Vector forward;
		AngleVectors( mv->m_vecAngles, &forward );
		if ( fabs( forward.z ) == 1.0f )
			return false;
		Vector2D view( forward.x, forward.y );
		Vector2D toward( -trace.plane.normal.x, -trace.plane.normal.y );
		Vector2DNormalize( view );
		Vector2DNormalize( toward );
		if ( z_min_ladder_mount_dot.GetFloat() > DotProduct2D( toward, view ) )
			return false;
	}
	return true;
}

// S2D21B0/C276670. The NULL override selects the actual moving player.
unsigned int CTerrorGameMovement::PlayerSolidMask( bool brushOnly, CBasePlayer *pPlayerOverride )
{
	CTerrorPlayer *subject = static_cast< CTerrorPlayer * >( pPlayerOverride ? pPlayerOverride : player );
	unsigned int extraContents = 0;
	if ( subject->IsGhost() )
		extraContents = 0x00400000u;
	else
	{
		const int team = subject->GetTeamNumber();
		if ( team == TEAM_SURVIVOR || team == 4 )
			extraContents = 0x00000800u;
		else if ( team == TEAM_INFECTED )
			extraContents = 0x00001000u;
	}
	const bool infected = subject->GetTeamNumber() == TEAM_INFECTED;
	const bool bot = subject->IsBot();
	if ( brushOnly )
	{
		if ( infected )
			return extraContents | ( bot ? 0x0002420Bu : 0x8002420Bu );
		return extraContents | 0x0001420Bu;
	}
	if ( infected )
		return extraContents | ( bot ? 0x0202420Bu : 0x8202420Bu );
	return extraContents | 0x0201420Bu;
}

// S2D3760/C2780F0. The client has no server geometry scan.
void CTerrorGameMovement::OnFinishWalkMove( bool bOnGround )
{
	if ( !m_pTerrorPlayer || m_pTerrorPlayer->IsIncapacitated() || !m_pTerrorPlayer->IsAlive() ||
		m_pTerrorPlayer->IsFallingFromLedge() )
		return;
	const int team = m_pTerrorPlayer->GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != 4 &&
		!( team == TEAM_INFECTED && m_pTerrorPlayer->GetZombieClass() == 2 ) )
		return;
	if ( !m_pTerrorPlayer->IsJockeyVictim() && !m_pTerrorPlayer->m_jockeyVictim.Get() &&
		!m_pTerrorPlayer->IsBeingCarried() )
		BaseClass::OnFinishWalkMove( bOnGround );
#ifndef CLIENT_DLL
	UpdateLedgeGrab();
#endif
}

#ifndef CLIENT_DLL
static bool IsLedgeSurfaceAllowed( CBaseEntity *surface )
{
	// S2A7AA0 admits NULL, but the geometry writer separately requires a
	// genuine trace surface. No NPC or moving door/dynamic-prop support.
	return !surface || ( !surface->MyCombatCharacterPointer() &&
		!surface->ClassMatches( "func_door*" ) && !surface->ClassMatches( "prop_door*" ) &&
		!surface->ClassMatches( "prop_dynamic*" ) );
}

static void DrawLedgeCameraCross( const Vector &position, float size, int r, int g, int b, bool noDepthTest, float duration )
{
	// S:A2020 uses the genuine index-one listen host, not a fabricated view.
	CBasePlayer *host = UTIL_GetListenServerHost();
	if ( !host )
		return;
	Vector right, up;
	host->EyeVectors( NULL, &right, &up );
	NDebugOverlay::Line( position - right * size, position + right * size, r, g, b, noDepthTest, duration );
	NDebugOverlay::Line( position - up * size, position + up * size, r, g, b, noDepthTest, duration );
}


#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S2A81B0. Only real trace geometry, support and NAV may prepare a hang.
int CTerrorGameMovement::TryLedgeGrab( const Vector &inputDirection, float verticalStep, float probeDistance )
{
	const int team = m_pTerrorPlayer->GetTeamNumber();
	if ( team != TEAM_SURVIVOR && team != 4 &&
		!( team == TEAM_INFECTED && m_pTerrorPlayer->GetZombieClass() == 2 ) )
		return 0;
	if ( inputDirection.z < -400.0f )
		return 0;
	Vector direction( inputDirection.x, inputDirection.y, 0.0f );
	if ( direction.x > -0.01f && direction.x < 0.01f && direction.y > -0.01f && direction.y < 0.01f )
		return 0;
	const double length = sqrt( ( (double)direction.x * direction.x + (double)direction.y * direction.y ) + (double)direction.z * direction.z );
	const float roundedLength = (float)length;
	const float inverseLength = 1.0f / ( roundedLength + FLT_EPSILON );
	direction *= inverseLength;
	if ( z_debug_ledges.GetInt() )
		NDebugOverlay::HorzArrow( mv->GetAbsOrigin(), mv->GetAbsOrigin() + direction * 100.0f,
			1.0f, 0, 0, 255, 255, true, 1000.0f );

	const CViewVectors *view = g_pGameRules->GetViewVectors();
	const Vector &standingMins = view->m_vHullMin;
	const Vector &standingMaxs = view->m_vHullMax;
	const Vector &duckMins = view->m_vDuckHullMin;
	const Vector &duckMaxs = view->m_vDuckHullMax;
	CTraceFilterNoNPCsOrPlayer filter( player, COLLISION_GROUP_PLAYER_MOVEMENT );
	Vector start = mv->GetAbsOrigin();
	Vector end = start - direction * probeDistance;
	trace_t trace;
	TracePlayerBBoxWithFilter( start, end, standingMins, standingMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( trace.startsolid || trace.fraction == 1.0f )
	{
		if ( z_debug_ledges.GetInt() )
			NDebugOverlay::HorzArrow( start, end, 2.0f, 255, 0, 0, 20, true, 1000.0f );
		return 0;
	}

	start = mv->GetAbsOrigin();
	end = start;
	end.z -= verticalStep;
	TracePlayerBBoxWithFilter( start, end, standingMins, standingMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( trace.startsolid || trace.fraction < 1.0f )
		return 0;
	const Vector climbEndPosition = trace.endpos;
	for ( int attempt = 0; attempt < 3; ++attempt )
	{
		start = mv->GetAbsOrigin();
		end = start - direction * probeDistance;
		end.z -= static_cast< float >( attempt ) * verticalStep;
		TracePlayerBBoxWithFilter( start, end, standingMins, standingMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
		if ( !trace.startsolid && trace.fraction < 1.0f )
			break;
		if ( z_debug_ledges.GetInt() )
			NDebugOverlay::SweptBox( trace.startpos, trace.endpos, standingMins, standingMaxs, vec3_angle,
				255, 0, 128, 100, 1000.0f );
	}
	if ( trace.startsolid || trace.fraction == 1.0f || !IsLedgeSurfaceAllowed( trace.m_pEnt ) )
		return 0;
	const Vector hangNormal = trace.plane.normal;
	Vector hangPosition = trace.endpos;
	CBaseEntity *surface = trace.m_pEnt;
	if ( !surface )
		return 0;
	if ( z_debug_ledges.GetInt() )
	{
		NDebugOverlay::Cross3D( m_pTerrorPlayer->m_lastStandingPosition, 10.0f, 0, 255, 0, true, 1000.0f );
		NDebugOverlay::Cross3D( trace.endpos, 16.0f, 255, 0, 0, true, 1000.0f );
	}

	start = hangPosition;
	end = start + Vector( 0.0f, 0.0f, 64.0f );
	TracePlayerBBoxWithFilter( start, end, duckMins, duckMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( z_debug_ledges.GetInt() )
		NDebugOverlay::SweptBox( trace.startpos, trace.endpos, duckMins, duckMaxs, vec3_angle,
			255, 0, 0, 100, 1000.0f );
	if ( trace.fraction != 1.0f )
	{
		start.x = end.x = mv->GetAbsOrigin().x;
		start.y = end.y = mv->GetAbsOrigin().y;
		TracePlayerBBoxWithFilter( start, end, duckMins, duckMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
		if ( trace.startsolid || trace.fraction != 1.0f )
		{
			if ( z_debug_ledges.GetInt() )
				NDebugOverlay::SweptBox( trace.startpos, trace.endpos, duckMins, duckMaxs, vec3_angle,
					255, 0, 0, 100, 1000.0f );
			return 0;
		}
	}

	start = trace.endpos;
	end = start - direction * 32.0f;
	TracePlayerBBoxWithFilter( start, end, duckMins, duckMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( z_debug_ledges.GetInt() )
	{
		NDebugOverlay::SweptBox( trace.startpos, trace.endpos, duckMins, duckMaxs, vec3_angle,
			0, 255, 128, 100, 1000.0f );
		NDebugOverlay::SweptBox( start, end, duckMins, duckMaxs, vec3_angle, 255, 127, 0, 0, 100.0f );
	}
	if ( trace.startsolid || trace.fraction < 0.1f )
		return 0;
	start = trace.endpos;
	end = start - Vector( 0.0f, 0.0f, 65.0f );
	TracePlayerBBoxWithFilter( start, end, duckMins, duckMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( trace.startsolid || trace.fraction == 1.0f || !( trace.plane.normal.z >= 0.7f ) ||
		!IsLedgeSurfaceAllowed( trace.m_pEnt ) )
		return 0;
	const Vector standPosition = trace.endpos;
	hangPosition.z = standPosition.z - standingMaxs.z;

	const Vector smallMins( -8.0f, -8.0f, 0.0f );
	const Vector smallMaxs( 8.0f, 8.0f, 1.0f );
	start.Init( hangPosition.x, hangPosition.y, standPosition.z );
	end = start - Vector( 0.0f, 0.0f, 60.0f );
	TracePlayerBBoxWithFilter( start, end, smallMins, smallMaxs, PlayerSolidMask( false, NULL ), &filter, trace );
	if ( trace.startsolid )
	{
		if ( z_debug_ledges.GetInt() )
			NDebugOverlay::Box( start, smallMins, smallMaxs, 255, 0, 0, 10, 1000.0f );
		return 0;
	}
	if ( trace.fraction < 1.0f && trace.plane.normal.z >= 0.7f )
	{
		if ( z_debug_ledges.GetInt() )
			NDebugOverlay::SweptBox( start, end, smallMins, smallMaxs, vec3_angle, 255, 0, 0, 10, 1000.0f );
		return 0;
	}
	if ( z_debug_ledges.GetInt() )
		NDebugOverlay::SweptBox( start, end, smallMins, smallMaxs, vec3_angle, 0, 255, 0, 10, 1000.0f );

	TerrorNavArea *standArea = ToTerrorNavArea( TheNavMesh->GetNearestNavArea(
		hangPosition, true, 120.0f, false, false, TEAM_ANY, false ) );
	// This entry reads native336: the raw forward/from-start flow, not332.
	if ( !standArea || standArea->GetFlowDistanceFromStart() < 0.0f )
	{
		if ( z_debug_ledges.GetInt() )
		{
			NDebugOverlay::Sphere( hangPosition, 120.0f, 255, 0, 0, true, 1000.0f );
			DrawLedgeCameraCross( hangPosition, 120.0f, 255, 0, 128, true, 1000.0f );
		}
		return 0;
	}
	if ( z_debug_ledges.GetInt() )
	{
		NDebugOverlay::HorzArrow( hangPosition, standArea->GetCenter(), 5.0f, 255, 0, 0, 255, true, 30.0f );
		DevMsg( "nearby area is %d\n", standArea->GetID() );
	}

	const Vector fallPosition = hangPosition;
	QAngle angles;
	VectorAngles( hangNormal, angles );
	while ( angles.y > 90.0f )
		angles.y -= 90.0f;
	const double radians = static_cast< double >( angles.y ) * static_cast< double >( 0.017453292f );
	const double cosine = cos( radians );
	const double sine = sin( radians );
	const float scale = static_cast< float >( cosine > sine ? cosine : sine );
	hangPosition.x += ( standPosition.x - hangPosition.x ) * ( 1.0f - scale );
	hangPosition.y += ( standPosition.y - hangPosition.y ) * ( 1.0f - scale );
	switch ( m_pTerrorPlayer->GetSurvivorCharacter() )
	{
	case 1:
		hangPosition.z += 8.0f;
		break;
	case 3:
		hangPosition.z += 3.0f;
		break;
	}
	m_pTerrorPlayer->PrepareLedgeGrab( hangPosition, standPosition, fallPosition, hangNormal,
		climbEndPosition, surface, standArea );
	return 2;
}

// S2A8F80 owns the airborne ladder/ledge search, after walk completion.
void CTerrorGameMovement::UpdateLedgeGrab()
{
	if ( !m_pTerrorPlayer || !player->IsAlive() )
		return;
	if ( player->GetGroundEntity() || player->GetMoveType() == MOVETYPE_LADDER ||
		( !m_pTerrorPlayer->CanPerformBackpackAction() && !m_pTerrorPlayer->IsTongueVictim() ) ||
		player->GetMoveType() == MOVETYPE_NOCLIP )
	{
		m_pTerrorPlayer->m_canGrabLedge = false;
		return;
	}
	const int team = m_pTerrorPlayer->GetTeamNumber();
	int grabMode = ( team == TEAM_SURVIVOR || team == 4 ) ? 3 : 1;
	const double predictedDamage = m_pTerrorPlayer->GetPredictedFallDamage();
	const float fallDamage = static_cast< float >( predictedDamage );
	if ( 95.0 > predictedDamage )
	{
		m_pTerrorPlayer->m_canGrabLedge = false;
		grabMode = 1;
	}
	if ( fallDamage < 1.0f || m_pTerrorPlayer->GetTimeSinceLadderInteraction() < 1.0 )
	{
		m_pTerrorPlayer->m_canGrabLedge = false;
		return;
	}
	if ( grabMode != 1 && player->GetTeamNumber() == TEAM_SURVIVOR &&
		CountLivingSurvivors( 1 ) <= 1 && !z_grab_ledges_solo.GetBool() )
	{
		m_pTerrorPlayer->m_canGrabLedge = false;
		grabMode = 1;
	}
	m_pTerrorPlayer->m_canGrabLedge = false;
	if ( team != TEAM_SURVIVOR && team != 4 &&
		!( team == TEAM_INFECTED && m_pTerrorPlayer->GetZombieClass() == 2 ) )
		return;

	Vector directions[8] =
	{
		Vector( 1.0f, 0.0f, 0.0f ), Vector( -1.0f, 0.0f, 0.0f ),
		Vector( 0.0f, 1.0f, 0.0f ), Vector( 0.0f, -1.0f, 0.0f )
	};
	for ( int i = 0; i < 4; ++i )
	{
		QAngle angles( 0.0f, AngleNormalizePositive( mv->m_vecAngles.y + static_cast< float >( i ) * 90.0f ), 0.0f );
		AngleVectors( angles, &directions[i + 4] );
	}
	const Vector &mins = BaseClass::GetPlayerMins();
	const Vector &maxs = BaseClass::GetPlayerMaxs();
	const unsigned int mask = PlayerSolidMask( false, NULL );
	const unsigned int ladderMask = LadderMask();
	CTraceFilterNoNPCsOrPlayer filter( player, COLLISION_GROUP_PLAYER_MOVEMENT );
	const Vector origin = mv->GetAbsOrigin();
	if ( mv->m_nButtons & IN_JUMP )
		return;
	if ( z_debug_ledges.GetInt() < 0 )
		z_debug_ledges.SetValue( z_debug_ledges.GetInt() + 1 );
	for ( int i = 0; i < ARRAYSIZE( directions ); ++i )
	{
		trace_t trace;
		Vector end = origin + directions[i] * 64.0f;
		TracePlayerBBoxWithFilter( origin, end, mins, maxs, mask, &filter, trace );
		bool onLadder = OnLadder( trace );
		if ( !onLadder )
		{
			end = trace.endpos + directions[i] * 2.0f;
			if ( FStrEq( trace.surface.name, "TOOLS/TOOLSCLIP" ) || FStrEq( trace.surface.name, "TOOLS/TOOLSPLAYERCLIP" ) )
			{
				TracePlayerBBoxWithFilter( origin, end, mins, maxs, ladderMask, &filter, trace );
				onLadder = OnLadder( trace );
			}
		}
		if ( onLadder )
		{
			player->SetMoveType( MOVETYPE_LADDER );
			player->SetMoveCollide( MOVECOLLIDE_DEFAULT );
			m_pTerrorPlayer->SetLadderNormal( trace.plane.normal );
			m_pTerrorPlayer->SetLadderSurfaceProps( trace.surface.surfaceProps );
			mv->m_vecVelocity.Init();
			mv->SetAbsOrigin( trace.endpos );
			return;
		}
	}
	if ( grabMode == 1 )
		return;
	for ( int i = 0; i < ARRAYSIZE( directions ); ++i )
	{
		const Vector direction( directions[i].x, directions[i].y, mv->m_vecVelocity.z );
		if ( TryLedgeGrab( direction, 40.0f, 64.0f ) == 2 )
		{
			m_pTerrorPlayer->GrabLedge( mv->GetAbsOrigin() );
			return;
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
#endif

// S2D2A50/S2D41E0/S2D5910. The paired client C276980 collision hook is empty.
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

#ifndef CLIENT_DLL
extern bool g_bMovementOptimizations;

// S2D2A50: the contacted player's own hull and ignore entity, not this mover's hull.
static void TraceLungePushPlayerBBox( CBasePlayer *other, const Vector &start, const Vector &end,
	unsigned int mask, int collisionGroup, trace_t &trace )
{
	const Vector maxs = other->GetPlayerMaxs();
	const Vector mins = other->GetPlayerMins();
	Ray_t ray;
	ray.Init( start, end, mins, maxs );
	CTraceFilterSimple filter( other, collisionGroup );
	enginetrace->TraceRay( ray, mask, &filter, &trace );
	if ( r_visualizetraces.GetBool() )
		DebugDrawLine( trace.startpos, trace.endpos, 255, 0, 0, true, -1.0f );
}

// S2D41E0: independent four-bump solve for another player. No contact recursion,
// touch-list mutation, rough-landing sound, step trace or crease normalization.
int CTerrorGameMovement::TryLungePushPlayerMove( CTerrorPlayer *other, Vector &position, Vector &velocity,
	const Vector *firstDestination, const trace_t *firstTrace )
{
	Vector planes[5];
	int planeCount = 0;
	int blocked = 0;
	float timeLeft = gpGlobals->frametime;
	float allFraction = 0.0f;
	const Vector primalVelocity = velocity;
	Vector originalVelocity = velocity;
	Vector newVelocity = vec3_origin;

	for ( int bump = 0; bump < 4; ++bump )
	{
		if ( sqrtf( ( velocity.x * velocity.x + velocity.y * velocity.y ) + velocity.z * velocity.z ) == 0.0f )
			break;
		const Vector end = position + velocity * timeLeft;
		trace_t trace;
		if ( g_bMovementOptimizations && firstDestination && end == *firstDestination )
			trace = *firstTrace;
		else
			TraceLungePushPlayerBBox( other, position, end, PlayerSolidMask( false, other ),
				COLLISION_GROUP_PLAYER_MOVEMENT, trace );

		allFraction += trace.fraction;
		if ( trace.allsolid )
		{
			velocity = vec3_origin;
			return 4;
		}
		if ( trace.fraction > 0.0f )
		{
			position = trace.endpos;
			originalVelocity = velocity;
			planeCount = 0;
		}
		if ( trace.fraction == 1.0f )
			break;
		if ( (double)trace.plane.normal.z > (double)GetStandableNormal() )
			blocked |= 1;
		if ( trace.plane.normal.z == 0.0f )
			blocked |= 2;
		timeLeft -= trace.fraction * timeLeft;
		if ( planeCount >= ARRAYSIZE( planes ) )
		{
			velocity = vec3_origin;
			break;
		}
		planes[planeCount++] = trace.plane.normal;
		if ( planeCount == 1 && other->GetMoveType() == MOVETYPE_WALK && !other->GetGroundEntity() )
		{
			if ( !( (double)planes[0].z > (double)GetStandableNormal() ) )
				ClipVelocity( originalVelocity, planes[0], newVelocity,
					1.0f + ( 1.0f - other->m_surfaceFriction ) * sv_bounce.GetFloat() );
			else
				ClipVelocity( originalVelocity, planes[0], newVelocity, 1.0f );
			velocity = newVelocity;
			originalVelocity = newVelocity;
		}
		else
		{
			int plane;
			for ( plane = 0; plane < planeCount; ++plane )
			{
				ClipVelocity( originalVelocity, planes[plane], velocity, 1.0f );
				int otherPlane;
				for ( otherPlane = 0; otherPlane < planeCount; ++otherPlane )
				{
					if ( otherPlane != plane &&
						( velocity.y * planes[otherPlane].y + velocity.x * planes[otherPlane].x ) + velocity.z * planes[otherPlane].z < 0.0f )
						break;
				}
				if ( otherPlane == planeCount )
					break;
			}
			if ( plane == planeCount )
			{
				if ( planeCount != 2 )
				{
					velocity = vec3_origin;
					break;
				}
				Vector direction;
				CrossProduct( planes[0], planes[1], direction );
				const float distance = ( direction.y * velocity.y + direction.x * velocity.x ) + direction.z * velocity.z;
				velocity = direction * distance;
			}
			if ( ( velocity.y * primalVelocity.y + velocity.x * primalVelocity.x ) + velocity.z * primalVelocity.z <= 0.0f )
			{
				velocity = vec3_origin;
				break;
			}
		}
	}
	if ( allFraction == 0.0f )
		velocity = vec3_origin;
	return blocked;
}
#endif

void CTerrorGameMovement::OnPlayerMoveCollision( const Vector &start, const Vector &end, trace_t *trace )
{
#ifndef CLIENT_DLL
	if ( !player || m_pTerrorPlayer->IsGhost() || !trace || !trace->m_pEnt )
		return;
	CBaseEntity *other = trace->m_pEnt;
	if ( m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED && m_pTerrorPlayer->IsLunging() )
	{
		// Native2068 saved trace is write-only in the movement-owner code census;
		// standing write-only-field elision removes only that unused copy/storage.
		if ( !m_pTerrorPlayer->IsGhost() && !m_pTerrorPlayer->IsAttemptingToPounce() )
		{
			CTerrorPlayer *victim = ToTerrorPlayer( other );
			if ( victim && m_lungePushedPlayers.Find( victim ) == m_lungePushedPlayers.InvalidIndex() && victim->IsLungePushable() )
			{
				CTerrorWeapon *weapon = static_cast< CTerrorWeapon * >( m_pTerrorPlayer->GetActiveWeapon() );
				// NativeS2D5A1C first-lookup NULL dereference: approved minimal skip
				// only the ID query/callback, then continue native push bookkeeping.
				if ( weapon && weapon->GetWeaponID() == TERROR_WEAPON_HUNTER_CLAW )
					static_cast< CHunterClaw * >( weapon )->OnPounceHit();
				m_lungePushedPlayers.AddToTail( victim );
				Vector position = victim->GetAbsOrigin();
				Vector pushVelocity = mv->m_vecVelocity;
				TryLungePushPlayerMove( victim, position, pushVelocity, NULL, NULL );

				Vector stepEnd = victim->GetAbsOrigin();
				stepEnd.z += victim->GetStepSize();
				trace_t stepTrace;
				TraceLungePushPlayerBBox( victim, victim->GetAbsOrigin(), stepEnd, PlayerSolidMask( false, victim ),
					COLLISION_GROUP_PLAYER_MOVEMENT, stepTrace );
				if ( stepTrace.fraction > 0.0f )
				{
					Vector steppedPosition = stepTrace.endpos;
					Vector stepVelocity = mv->m_vecVelocity;
					TryLungePushPlayerMove( victim, steppedPosition, stepVelocity, NULL, NULL );
					const Vector steppedDistance = steppedPosition - stepTrace.endpos;
					const Vector directDistance = position - victim->GetAbsOrigin();
					if ( ( steppedDistance.y * steppedDistance.y + steppedDistance.x * steppedDistance.x ) + steppedDistance.z * steppedDistance.z >
						( directDistance.y * directDistance.y + directDistance.x * directDistance.x ) + directDistance.z * directDistance.z )
						position = steppedPosition;
				}
				victim->SetAbsOrigin( position );
				m_pTerrorPlayer->OnPounceContact( victim );
				if ( mv->m_vecVelocity.z > 0.0f )
					mv->m_vecVelocity.z *= 0.5f;
				TracePlayerBBox( start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, *trace );
				victim->SetAbsVelocity( mv->m_vecVelocity );
				return;
			}
			Infected *infected = dynamic_cast< Infected * >( trace->m_pEnt );
			if ( infected )
			{
				infected->MyNextBotPointer()->OnShoved( m_pTerrorPlayer );
				return;
			}
		}
	}
	if ( FClassnameIs( other, "pipe_bomb_projectile" ) )
	{
		const Vector displacement = end - start;
		const Vector origin = other->GetAbsOrigin();
		const Vector destination = origin + displacement;
		trace_t pipeTrace;
		UTIL_TraceEntity( other, origin, destination, other->PhysicsSolidMaskForEntity(),
			other, other->GetCollisionGroup(), &pipeTrace );
		if ( pipeTrace.fraction > 0.0f && !pipeTrace.startsolid )
		{
			// Native moves to the requested destination, NOT pipeTrace.endpos.
			other->SetAbsOrigin( destination );
			TracePlayerBBox( start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, *trace );
		}
	}
	else
	{
		const bool charger = !IsNativeSurvivorTeam( m_pTerrorPlayer->GetTeamNumber() ) && m_pTerrorPlayer->GetZombieClass() == 6;
		const float speedSquared = ( mv->m_vecVelocity.x * mv->m_vecVelocity.x + mv->m_vecVelocity.y * mv->m_vecVelocity.y ) + mv->m_vecVelocity.z * mv->m_vecVelocity.z;
		const bool slow = speedSquared <= 14400.0f;
		if ( speedSquared > 14400.0f || charger )
		{
			Vector direction = trace->endpos - trace->startpos;
			VectorNormalize( direction );
			bool handled = false;
			if ( m_pTerrorPlayer->GetCustomAbility() )
			{
				CBaseAbility *ability = m_pTerrorPlayer->GetCustomAbility();
				if ( ability ) // Standing undefined-dereference minimal skip.
					handled = ability->OnPlayerMoveCollision( other, trace->endpos, direction, *trace, mv );
			}
			if ( handled )
				TracePlayerBBox( start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, *trace );
			else if ( ( !charger || !slow ) && IsLowHealthBossBreakable( other ) )
			{
				CTakeDamageInfo info( player, player, 10.0f, DMG_CRUSH );
				CalculateExplosiveDamageForce( &info, direction, trace->endpos, 1.0f );
				CDisablePredictionFiltering disablePredictionFiltering;
				other->TakeDamage( info );
				TracePlayerBBox( start, end, PlayerSolidMask(), COLLISION_GROUP_PLAYER_MOVEMENT, *trace );
			}
		}
	}
#endif
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
// S2D2BC0: base punch/rumble effects precede the forced landing footstep.
void CTerrorGameMovement::PlayerRoughLandingEffects( float volume )
{
	if ( !m_pTerrorPlayer->ShouldPlayTerrorFootstepSound() )
		return;
	BaseClass::PlayerRoughLandingEffects( volume );
	if ( volume >= 0.5f )
	{
		// Match the native masked world/brush-volume query without introducing
		// the SDK unmasked point query's solid-prop occlusion of water.
		class CFootstepWaterFilter : public CTraceFilter
		{
		public:
			virtual bool ShouldHitEntity( IHandleEntity *handle, int contentsMask ) OVERRIDE
			{
				CBaseEntity *entity = EntityFromEntityHandle( handle );
				if ( !entity || !( entity->GetSolidFlags() & FSOLID_VOLUME_CONTENTS ) )
					return false;
				const model_t *model = entity->GetModel();
				return model && modelinfo->GetModelType( model ) == mod_brush;
			}
			virtual TraceType_t GetTraceType() const OVERRIDE { return TRACE_EVERYTHING_FILTER_PROPS; }
		} waterFilter;
		const Vector position = m_pTerrorPlayer->GetAbsOrigin();
		Ray_t waterRay;
		trace_t waterTrace;
		waterRay.Init( position, position );
		enginetrace->TraceRay( waterRay, CONTENTS_WATER, &waterFilter, &waterTrace );
		const bool wading = ( waterTrace.contents & CONTENTS_WATER ) != 0;
		bool shallow = wading;
		if ( !shallow )
		{
			const Vector &point = m_pTerrorPlayer->GetAbsOrigin();
			waterRay.Init( point, point );
			enginetrace->TraceRay( waterRay, CONTENTS_WATER, &waterFilter, &waterTrace );
			shallow = ( waterTrace.contents & CONTENTS_WATER ) != 0;
		}
		m_pTerrorPlayer->PlayTerrorFootstepSound( m_pTerrorPlayer->GetAbsOrigin(), true, shallow, wading, true );
	}
	if ( volume > 0.0f )
	{
		if ( IsNativeSurvivorTeam( m_pTerrorPlayer->GetTeamNumber() ) )
		{
			if ( volume > 0.85f )
				m_pTerrorPlayer->EmitSound( "Player.JumpLand" );
		}
		else
		{
			m_pTerrorPlayer->EmitSound( "PlayerZombie.JumpLand" );
		}
		if ( volume > 0.25f && m_pTerrorPlayer->GetTeamNumber() == TEAM_INFECTED &&
			m_pTerrorPlayer->GetZombieClass() == 3 && m_pTerrorPlayer->IsLunging() )
		{
			mv->m_vecVelocity.Init();
			m_pTerrorPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_LAND, 1 );
		}
		else
		{
			m_pTerrorPlayer->DoTerrorAnimationEvent( TERROR_ANIMEVENT_LAND, 0 );
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
