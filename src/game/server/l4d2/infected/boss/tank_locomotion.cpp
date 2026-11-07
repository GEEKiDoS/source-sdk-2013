//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/boss/tank.h"
#include "l4d2/infected/boss/tank_locomotion.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/zombie_spawn.h"
#include "props.h"
#include "NextBot/NextBotUtil.h"
#include "NextBot/NextBotBodyInterface.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

static const TerrorOriginalActivity s_climbActivities[] = // S:7C2BC8, original719/720/722/724/729/731/734/736
{
	TERROR_ORIGINAL_ACT_TERROR_CLIMB_36_FROM_STAND, TERROR_ORIGINAL_ACT_TERROR_CLIMB_38_FROM_STAND,
	TERROR_ORIGINAL_ACT_TERROR_CLIMB_50_FROM_STAND, TERROR_ORIGINAL_ACT_TERROR_CLIMB_70_FROM_STAND,
	TERROR_ORIGINAL_ACT_TERROR_CLIMB_115_FROM_STAND, TERROR_ORIGINAL_ACT_TERROR_CLIMB_130_FROM_STAND,
	TERROR_ORIGINAL_ACT_TERROR_CLIMB_150_FROM_STAND, TERROR_ORIGINAL_ACT_TERROR_CLIMB_166_FROM_STAND
};
static bool IsTankStepClimb( TerrorOriginalActivity original )
{
	for ( int i = 0; i < ARRAYSIZE( s_climbActivities ); ++i ) if ( original == s_climbActivities[i] ) return true;
	return false;
}
bool IsTankClimbingActivity( Activity activity ) // S:2C76C0
{
	const TerrorOriginalActivity original = LookupTerrorOriginalActivity( activity );
	return original == TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB || original == TERROR_ORIGINAL_ACT_TERROR_LEDGE_CLIMB_TO_CROUCH || IsTankStepClimb( original );
}
bool IsTankMotionControlledActivity( Activity activity ) // S:2C75D0
{
	const TerrorOriginalActivity original = LookupTerrorOriginalActivity( activity );
	return ( original >= TERROR_ORIGINAL_ACT_TERROR_SHOVED_FORWARD && original <= TERROR_ORIGINAL_ACT_TERROR_SHOVED_RIGHTWARD_MELEE ) ||
		( original >= TERROR_ORIGINAL_ACT_TERROR_TUGGED_FORWARD && original <= TERROR_ORIGINAL_ACT_TERROR_TUGGED_RIGHTWARD ) ||
		original == TERROR_ORIGINAL_ACT_TERROR_DIE_FROM_STAND || original == TERROR_ORIGINAL_ACT_TERROR_DIE_WHILE_RUNNING ||
		IsTankClimbingActivity( activity ) ||
		( original >= TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_L && original <= TERROR_ORIGINAL_ACT_TERROR_HUNTER_POUNCE_KNOCKOFF_FORWARD ) ||
		( original >= TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY && original <= TERROR_ORIGINAL_ACT_TERROR_RAGE_AT_KNOCKDOWN );
}
TankLocomotion::TankLocomotion( INextBot *bot ) : PlayerLocomotion( bot ) // S:472110 ->4BCC10/4BC120
{
}
void TankLocomotion::Reset() // S:472170, deliberately does not call PlayerLocomotion::Reset
{
	m_bodyAimEaseInTime = 0.08f;
}
bool TankLocomotion::IsAreaTraversable( const CNavArea *area ) const // S:4721A0
{
	if ( area->HasAttributes( 0x10000 ) ) return true;
	if ( area->HasAttributes( 0x20000 ) ) return false;
	return !area->IsBlocked( TEAM_INFECTED, false );
}
bool TankLocomotion::IsOnGround() const // S:4721E0
{
	return !IsTankClimbingActivity( static_cast< Tank * >( GetBot()->GetEntity() )->GetTerrorActivity() ) && PlayerLocomotion::IsOnGround();
}
bool TankLocomotion::IsEntityTraversable( CBaseEntity *obstacle, TraverseWhenType when ) const // S:4724E0
{
	if ( obstacle->HasSpawnFlags( 0x1000 ) ) return true;
	IPhysicsObject *physics = obstacle->VPhysicsGetObject();
	if ( physics && obstacle->GetMoveType() == MOVETYPE_VPHYSICS && physics->IsMoveable() ) return true;
	if ( obstacle->HasSpawnFlags( 0x2000 ) ) return true;
	// Native351C10, narrower than generic12A440 (see existing SurvivorLocomotion).
	const int group = obstacle->GetCollisionGroup();
	if ( ( group == COLLISION_GROUP_BREAKABLE_GLASS || group == COLLISION_GROUP_NONE ) && obstacle->m_takedamage == DAMAGE_YES && static_cast< float >( obstacle->GetHealth() ) <= 10.0f &&
		( FClassnameIs( obstacle, "func_breakable_surf" ) || FClassnameIs( obstacle, "func_breakable" ) || dynamic_cast< CBreakableProp * >( obstacle ) ) ) return true;
	CBaseEntity *parent = obstacle->GetParent(); // native100549D0 cached parent364, NOT moveparent372
	if ( parent && parent->GetMoveType() == MOVETYPE_VPHYSICS && parent->VPhysicsGetObject() && parent->VPhysicsGetObject()->IsMoveable() ) return true;
	return PlayerLocomotion::IsEntityTraversable( obstacle, when );
}
void TankLocomotion::OnAnimationActivityComplete( int activity ) // S:472230
{
	if ( !IsTankStepClimb( LookupTerrorOriginalActivity( static_cast< Activity >( activity ) ) ) ) return;
	Tank *me = static_cast< Tank * >( GetBot()->GetEntity() );
	me->SetAbsOrigin( m_landingGoal );
	NextBotTraceFilterIgnoreActors filter( me, COLLISION_GROUP_PLAYER_MOVEMENT );
	trace_t trace;
	UTIL_TraceHull( m_landingGoal, m_landingGoal, me->WorldAlignMins(), me->WorldAlignMaxs(), me->PhysicsSolidMaskForEntity(), &filter, &trace );
	CBaseEntity *obstacle = trace.m_pEnt;
	if ( obstacle && IsEntityTraversable( obstacle, IMMEDIATELY ) )
	{
		static ConVarRef witchHealth( "z_witch_health" );
		const float damage = obstacle->MyCombatCharacterPointer() ? witchHealth.GetFloat() : 999.90002f; //266050; no target-player due to trace filter
		if ( damage > 0.0f )
		{
			CTakeDamageInfo info( me, me, damage, DMG_CLUB );
			CalculateMeleeDamageForce( &info, me->GetAbsVelocity(), me->EyePosition() );
			ClearMultiDamage();
			obstacle->DispatchTraceAttack( info, me->GetAbsVelocity(), &trace );
			ApplyMultiDamage();
		}
	}
}
void TankLocomotion::OnAnimationActivityInterrupted( int activity ) // S:472400, excludes first (original719) animation
{
	const TerrorOriginalActivity original = LookupTerrorOriginalActivity( static_cast< Activity >( activity ) );
	if ( original == s_climbActivities[0] || !IsTankStepClimb( original ) ) return;
	Tank *me = static_cast< Tank * >( GetBot()->GetEntity() );
	trace_t trace;
	UTIL_TraceEntity( me, me->GetAbsOrigin(), me->GetAbsOrigin(), me->PhysicsSolidMaskForEntity(), &trace );
	if ( trace.startsolid )
	{
		Vector clear;
		if ( FindPassableTerrorSpawnPosition( me, me->GetAbsOrigin(), &clear, false, MASK_PLAYERSOLID ) ) me->SetAbsOrigin( clear );
	}
}
bool TankLocomotion::ClimbUpToLedge( const Vector &landingGoal, const Vector &landingForward, const CBaseEntity *obstacle ) // S:472570
{
	if ( !IsOnGround() || IsClimbingOrJumping() || !IsClimbPossible( GetBot(), obstacle ) ) return false;
	Tank *me = static_cast< Tank * >( GetBot()->GetEntity() );
	CBaseAbility *ability = me->m_customAbility.Get();
	if ( ability && ability->IsActive() ) return false;
	const float height = landingGoal.z - GetFeet().z;
	GetStepHeight(); // native virtual query retained before fallback
	GetFeet();
	if ( height < 30.0f ) return PlayerLocomotion::ClimbUpToLedge( landingGoal, landingForward, obstacle );
	// First Tank/model reaching this point initializes the shared native height
	// cache for all Tanks. Do not re-evaluate on model changes (native omission).
	static bool initialized = false;
	static float heights[ ARRAYSIZE( s_climbActivities ) ];
	if ( !initialized )
	{
		initialized = true;
		for ( int i = 0; i < ARRAYSIZE( s_climbActivities ); ++i )
		{
			Vector movement;
			me->GetSequenceLinearMotion( me->SelectWeightedSequence( TerrorActivityFromOriginal( s_climbActivities[i] ) ), &movement );
			heights[i] = movement.z;
		}
	}
	int selected = 1;
	bool found = false;
	for ( int i = ARRAYSIZE( heights ) - 1; i > 1; --i ) if ( height >= heights[i] ) { selected = i; found = true; break; }
	if ( !found && heights[0] > height ) selected = 0;
	Vector forward( landingForward.x, landingForward.y, 0.0f );
	VectorNormalize( forward );
	QAngle angles;
	VectorAngles( forward, angles );
	m_isClimbingUpToLedge = true;
	m_landingGoal = landingGoal;
	m_hasLeftTheGround = false;
	me->m_Local.m_bDucked = true;
	me->m_Local.m_bDucking = false;
	me->AddFlag( FL_DUCKING );
	me->UpdateReplacementCollisionBounds(); // native1460=2C9810, SAME existing M6 actor/bounds owner
	me->GetTerrorAnimState()->ResetGestureSlots(); //123B70, genuine seven-slot owner
	me->DoTerrorAnimationEvent( TERROR_ANIMEVENT_CLIMB, selected ); // native94, compact semantic owner, not PlayerAnimEvent/ordinal cast
	me->m_hangNormal = -forward; //316890
	me->SetAbsVelocity( vec3_origin );
	me->SetLocalAngles( QAngle( 0.0f, angles.y, 0.0f ) );
	if ( selected != 0 )
	{
		Vector movement, rotated;
		me->GetSequenceLinearMotion( me->SelectWeightedSequence( TerrorActivityFromOriginal( s_climbActivities[selected] ) ), &movement );
		VectorYawRotate( movement, angles.y, rotated );
		me->SetMoveType( MOVETYPE_CUSTOM );
		me->SetAbsOrigin( landingGoal - rotated );
	}
	if ( GetBot()->IsDebugging( NEXTBOT_LOCOMOTION ) )
	{
		NDebugOverlay::HorzArrow( landingGoal, landingGoal - 50.0f * forward, 10.0f, 255, 255, 0, 255, true, 10.0f );
		NDebugOverlay::Box( landingGoal, VEC_DUCK_HULL_MIN, VEC_DUCK_HULL_MAX, 0, 255, 0, 10, 10.0f );
	}
	me->EmitSound( "HulkZombie.StartLedgeClimb" );
	return true;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
