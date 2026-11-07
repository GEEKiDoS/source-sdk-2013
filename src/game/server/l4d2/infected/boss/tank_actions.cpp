//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/terror_nav_area.h"
#include "obstacle_pushaway.h"
#include "l4d2/infected/boss/tank.h"
#include "l4d2/infected/boss/tank_actions.h"
#include "l4d2/infected/boss/tank_locomotion.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/player/terror_playeranimstate.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/infected/infected_activities.h"
#include "l4d2/director/zombie_spawn.h"
#include "props.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "NextBot/Player/NextBotPlayerBody.h"
#include "igameevents.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

extern ConVar tank_windup_time, tank_swing_duration, tank_ground_pound_duration;
extern ConVar z_tank_throw_force, z_allow_ai_to_use_abilities;
ConVar tank_throw_loft_rate( "tank_throw_loft_rate", ".01", 0x00004004, "Beyond no-loft range, Tank adds this angle/distance when throwing" ); // S:5A9A10
ConVar tank_throw_max_loft_angle( "tank_throw_max_loft_angle", "30", 0x00004004, "Maximum loft angle for Tank throw angle adjustment" ); //5A9A40
ConVar tank_throw_lead_time_factor( "tank_throw_lead_time_factor", "0.5", 0x00004004, "How much to lead a moving target" ); //5A9A70
ConVar tank_throw_allow_range( "tank_throw_allow_range", "250", 0x00004004, "How far away our victim must be before we'll try to throw debris at them" ); //5A9AA0
ConVar tank_throw_min_interval( "tank_throw_min_interval", "8", 0x00004004, "Minimum interval between Tank rock throws" ); //5A9AD0
ConVar tank_throw_aim_error( "tank_throw_aim_error", "100", 0x00004004, "Margin of error for Easy mode Tank rock throws" ); //5A9B00
ConVar tank_stuck_failsafe( "tank_stuck_failsafe", "1", 0x00004004 ); //5A9B30
ConVar tank_stuck_time_choose_new_target( "tank_stuck_time_choose_new_target", "2", 0x00004004 ); //5A9B60
ConVar tank_stuck_visibility_tolerance_choose_new_target( "tank_stuck_visibility_tolerance_choose_new_target", "5", 0x00004004 ); //5A9B90
ConVar tank_stuck_time_suicide( "tank_stuck_time_suicide", "10", 0x00004004 ); //5A9BC0
ConVar tank_stuck_visibility_tolerance_suicide( "tank_stuck_visibility_tolerance_suicide", "15", 0x00004004 ); //5A9BF0
ConVar tank_visibility_tolerance_suicide( "tank_visibility_tolerance_suicide", "60", 0x00004004 ); //5A9C20
ConVar tank_stasis_time_suicide( "tank_stasis_time_suicide", "30", 0x00004004 ); //5A9C50

static bool IsTankSurvivor( CBaseEntity *entity )
{
	return entity && ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}
static void FireTankIgnitedEvent( Tank *me, const CTakeDamageInfo &info )
{
	// S:473B30/474970, actual injury event only when a player supplies DMG_BURN.
	CBasePlayer *attacker = ToBasePlayer( info.GetAttacker() );
	if ( !( info.GetDamageType() & DMG_BURN ) || !attacker ) return;
	IGameEvent *event = gameeventmanager->CreateEvent( "zombie_ignited" );
	if ( event )
	{
		event->SetInt( "userid", attacker->GetUserID() );
		event->SetInt( "entityid", me->entindex() );
		event->SetString( "victimname", "Tank" );
		gameeventmanager->FireEvent( event );
	}
}

Action< Tank > *TankBehavior::InitialContainedAction( Tank *me ) // S:474220
{
	return TheDirector->IsFinale() || me->IsAggressive() ? static_cast< Action< Tank > * >( new TankAttack ) : static_cast< Action< Tank > * >( new TankIdle );
}
ActionResult< Tank > TankBehavior::Update( Tank *me, float interval ) // S:474280
{
	if ( !me->IsInStasis() )
	{
		IVision *vision = me->GetVisionInterface();
		IBody *body = me->GetBodyInterface();
		CBaseEntity *threat = vision->GetPrimaryKnownThreat();
		if ( !me->GetLocomotionInterface()->IsUsingLadder() && threat && me->IsRangeLessThan( threat, 50.0f ) && vision->IsAbleToSee( threat, IVision::DISREGARD_FOV ) )
		{
			CTerrorPlayer *player = ToTerrorPlayer( threat );
			const float duration = player && player->IsIncapacitated() ? tank_ground_pound_duration.GetFloat() : tank_swing_duration.GetFloat() + tank_windup_time.GetFloat();
			body->AimHeadTowards( threat, IBody::IMPORTANT, duration, NULL, NULL );
			me->PressFireButton();
		}
	}
	return Continue();
}
EventDesiredResult< Tank > TankBehavior::OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) // S:4743D0
{
	static PressFireButtonReply reply;
	if ( other )
	{
		if ( IsBreakableEntity( other, me->IsZombieClass( 8 ), true, NULL ) || FClassnameIs( other, "prop_door*" ) || FClassnameIs( other, "func_door*" ) )
		{
			if ( !trace || trace->plane.normal.z < 0.8f ) me->GetBodyInterface()->AimHeadTowards( other->WorldSpaceCenter(), IBody::CRITICAL, 1.0f, &reply, "Looking at door I'm trying to break", true, 0.25f );
		}
		else
		{
			CPhysicsProp *prop = dynamic_cast< CPhysicsProp * >( other );
			if ( !prop && dynamic_cast< CDynamicProp * >( other ) ) prop = dynamic_cast< CPhysicsProp * >( other->GetParent() );
			if ( me->IsZombieClass( 8 ) && prop )
			{
				const float mass = prop->GetMass();
				if ( mass >= 25.0f ) me->PressFireButton();
				else if ( IPhysicsObject *physics = prop->VPhysicsGetObject() )
				{
					Vector away = other->GetAbsOrigin() - me->GetAbsOrigin();
					away.z = 0.0f;
					VectorNormalize( away );
					away *= mass * 4000.0f;
					away.z = ( mass * 4000.0f ) * 0.4f;
					physics->ApplyForceCenter( away );
					Vector spin;
					spin.Random( -1.0f, 1.0f );
					VectorNormalize( spin );
					spin *= 1000.0f;
					prop->SetLocalAngularVelocity( QAngle( spin.x, spin.y, spin.z ) );
				}
			}
			else if ( me->GetMoveType() == MOVETYPE_LADDER )
			{
				CTerrorPlayer *player = ToTerrorPlayer( other );
				if ( player && !player->IsDominated() && !( player->GetFlags() & ( FL_FROZEN | FL_ATCONTROLS ) ) )
				{
					Vector away = other->GetAbsOrigin() - me->GetAbsOrigin();
					if ( player->GetMoveType() == MOVETYPE_LADDER ) { player->SetMoveType( MOVETYPE_WALK ); away.x = player->GetLadderNormal().x; away.y = player->GetLadderNormal().y; }
					away.z = 0.0f;
					VectorNormalize( away );
					away *= 400.0f;
					away.z = 160.0f;
					player->SetAbsVelocity( away );
				}
			}
			else if ( other->MyCombatCharacterPointer() ) me->GetBodyInterface()->AimHeadTowards( other->WorldSpaceCenter(), IBody::IMPORTANT, 1.0f, &reply, "Looking at actor I'm trying to attack", true, 0.25f );
		}
	}
	return TryContinue();
}

ActionResult< Tank > TankIdle::OnStart( Tank *me, Action< Tank > *prior ) // S:474820
{
	me->GetBodyInterface()->SetArousal( IBody::NEUTRAL );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	return Continue();
}
ActionResult< Tank > TankIdle::Update( Tank *me, float interval ) // S:474880
{
	if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 7, 0.0f, NULL ) ) return ChangeTo( new TankAttack, "We see a victim!" );
	CTerrorPlayer *leader = FindHighestFlowSurvivor( FLOW_TOWARD_GOAL );
	if ( leader )
	{
		const float leaderFlow = static_cast< float >( leader->GetFlowDistance( FLOW_TOWARD_GOAL ) ); // S4748FE spills ONLY leader tofloat
		if ( static_cast< double >( leaderFlow ) > me->GetFlowDistance( FLOW_TOWARD_GOAL ) + 500.0 ) return ChangeTo( new TankAttack, "The Survivors have passed me by!" );
	}
	return Continue();
}
EventDesiredResult< Tank > TankIdle::OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) // S:474B10
{
	return other && other->IsPlayer() && IsTankSurvivor( other ) ? TryChangeTo( new TankAttack, RESULT_CRITICAL, "Don't touch me!" ) : TryToSustain();
}
EventDesiredResult< Tank > TankIdle::OnInjured( Tank *me, const CTakeDamageInfo &info ) // S:474970
{
	FireTankIgnitedEvent( me, info );
	return IsTankSurvivor( info.GetAttacker() ) ? TryChangeTo( new TankAttack, RESULT_CRITICAL, "Ouch!" ) : TryToSustain();
}
EventDesiredResult< Tank > TankIdle::OnShoved( Tank *me, CBaseEntity *pusher ) // S:474BA0
{
	return TryChangeTo( new TankAttack, RESULT_CRITICAL, "Don't push me!" );
}
EventDesiredResult< Tank > TankIdle::OnCommandApproach( Tank *me, const Vector &position, float range ) // S:474CA0
{
	return TrySuspendFor( new TankMoveTo( position ), RESULT_CRITICAL, "Debug move to" );
}

TankAttack::TankAttack() : m_path( ChasePath::LEAD_SUBJECT ), m_attackFlag( false ) // S:473FB0
{
	m_victim = NULL;
	// Native's remaining timers initialize invalid. m_throwError has no ctor write.
}
void TankAttack::ChooseThrowError() // S:472A50
{
	m_throwError = TerrorGameRules()->GetDifficultyLevel() != 0 ? 0.0f : random->RandomFloat( -M_PI_F, M_PI_F ) * 0.5f;
}
ActionResult< Tank > TankAttack::OnStart( Tank *me, Action< Tank > *prior ) // S:472BC0
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_attackStartTimer.Start();
	m_stuckTimer.Invalidate();
	m_throwTimer.Invalidate();
	ChooseThrowError();
	// S:472AA0: PZ modes notify all connected players; otherwise only team2.
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->edict() && player->IsConnected() && ( TerrorGameRules()->IsPlayerControlledZombies() || player->GetTeamNumber() == TEAM_SURVIVOR ) ) player->GetMusic()->OnBossApproaching();
	}
	return Continue();
}

void TankAttack::UpdateThrow( Tank *me ) // S:472D00
{
	const int difficulty = TerrorGameRules()->GetDifficultyLevel();
	if ( !z_allow_ai_to_use_abilities.GetBool() ) return;
	if ( !TheDirector->IsFinale() && ( difficulty == 0 || difficulty == 1 ) &&
		static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_attackStartTimer.GetStartTime() ) < 10.0 ) return;
	CBaseAbility *ability = me->m_customAbility.Get();
	if ( !ability ) return;
	CTerrorPlayer *victim = m_victim.Get();
	if ( gpGlobals->curtime < m_throwTimer.m_timestamp )
	{
		if ( !ability->IsActive() ) return;
		const float distance = me->GetRangeTo( victim );
		float loft = tank_throw_loft_rate.GetFloat() * distance;
		if ( loft > tank_throw_max_loft_angle.GetFloat() ) loft = tank_throw_max_loft_angle.GetFloat();
		Vector lead;
		if ( difficulty != 0 )
		{
			const float travelTime = distance / z_tank_throw_force.GetFloat() + tank_throw_lead_time_factor.GetFloat();
			lead = victim->GetAbsVelocity() * travelTime;
			lead.z = 0.0f;
		}
		else
		{
			Vector direction = victim->GetAbsOrigin() - me->GetAbsOrigin();
			Vector side, up;
			VectorVectors( direction, side, up );
			const double throwError = static_cast< double >( m_throwError );
			const float cosine = static_cast< float >( cos( throwError ) ), sine = static_cast< float >( sin( throwError ) );
			lead = ( side * sine + up * cosine ) * tank_throw_aim_error.GetFloat();
		}
		// Double approximates native x87; preserve the F32 constant and final spill.
		const double radians = static_cast< double >( loft ) * static_cast< double >( 0.017453292f );
		lead.z = static_cast< float >( tan( radians ) * static_cast< double >( distance ) + static_cast< double >( lead.z ) );
		me->GetBodyInterface()->AimHeadTowards( victim->WorldSpaceCenter() + lead, IBody::CRITICAL, 4.0f, NULL, "Throw loft angle", true, 0.25f );
		me->GetLocomotionInterface()->ClearStuckStatus( "Throwing rock" );
		return;
	}
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( me->m_Local.m_bDucked || !locomotion->IsOnGround() || locomotion->IsClimbingOrJumping() || victim->IsIncapacitated() || me->IsRangeLessThan( victim, tank_throw_allow_range.GetFloat() ) ) return;
	IVision *vision = me->GetVisionInterface();
	if ( vision->IsAbleToSee( victim, IVision::DISREGARD_FOV ) && ability->CanActivate() && ability->IsReadyToActivate() && vision->IsLookingAt( victim, 0.95f ) )
	{
		me->PressAltFireButton();
		m_throwTimer.Start( tank_throw_min_interval.GetFloat() );
		ChooseThrowError();
		locomotion->ClearStuckStatus( "Throwing rock" );
	}
}

ActionResult< Tank > TankAttack::Update( Tank *me, float interval ) // S:4731C0
{
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( tank_stuck_failsafe.GetBool() )
	{
		if ( IsTankMotionControlledActivity( me->GetTerrorActivity() ) || IsTankClimbingActivity( me->GetTerrorActivity() ) )
		{
			locomotion->ClearStuckStatus( "Motion controlled animation" );
			m_stuckTimer.Invalidate();
		}
		bool kill = false;
		if ( me->IsInStasis() )
		{
			locomotion->ClearStuckStatus( "In Stasis" );
			m_stuckTimer.Invalidate();
			if ( m_stasisTimer.GetStartTime() > 0.0f )
			{
				if ( m_stasisTimer.GetElapsedTime() > tank_stasis_time_suicide.GetFloat() ) { m_stasisTimer.Invalidate(); kill = true; }
			}
			else m_stasisTimer.Start();
		}
		const float unseen = me->GetVisionInterface()->GetTimeSinceVisible( TEAM_SURVIVOR );
		if ( m_stuckTimer.GetStartTime() > 0.0f )
		{
			if ( m_stuckTimer.GetElapsedTime() <= tank_stuck_time_choose_new_target.GetFloat() || tank_stuck_visibility_tolerance_choose_new_target.GetFloat() <= unseen )
			{
				if ( m_stuckTimer.GetElapsedTime() > tank_stuck_time_suicide.GetFloat() && ( unseen == FLT_MAX || unseen > tank_stuck_visibility_tolerance_suicide.GetFloat() ) ) kill = true;
			}
			else { m_stuckTimer.Invalidate(); m_path.Invalidate(); m_victim = NULL; }
		}
		if ( ( unseen != FLT_MAX && unseen > tank_visibility_tolerance_suicide.GetFloat() ) || kill )
		{
			me->EndControlOffer();
			Msg( "Tank committing suicide at %f %f %f\n", me->GetAbsOrigin().x, me->GetAbsOrigin().y, me->GetAbsOrigin().z );
			CTakeDamageInfo damage( me, me, static_cast< float >( me->GetHealth() ), DMG_BULLET );
			me->TakeDamage( damage );
			m_stuckTimer.Invalidate();
			return Done( "Killing self" );
		}
	}
	if ( me->IsInStasis() ) return Continue();
	if ( locomotion->IsUsingLadder() && m_victim.Get() && m_path.IsValid() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, m_victim.Get(), cost );
		return Continue();
	}
	if ( m_victim.Get() && ( LookupTerrorOriginalActivity( me->GetTerrorActivity() ) == TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY || LookupTerrorOriginalActivity( me->GetTerrorActivity() ) == TERROR_ORIGINAL_ACT_TERROR_HULK_VICTORY_B ) )
	{
		QAngle angles;
		VectorAngles( m_victim->GetAbsOrigin() - me->GetPosition(), angles );
		me->SetAbsAngles( angles );
	}
	CBaseAbility *ability = me->m_customAbility.Get();
	if ( !ability || !ability->IsActive() )
	{
		CTerrorPlayer *mounted = NULL;
		// S:472B30: deliberately last matching team2 mounted-weapon user wins.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->edict() && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR && player->m_usingMountedWeapon ) mounted = player;
		}
		m_victim = mounted ? mounted : me->SelectVictim( m_victim.Get(), 4, NULL );
	}
	CTerrorPlayer *victim = m_victim.Get();
	if ( victim && victim->IsAlive() )
	{
		UpdateThrow( me );
		me->GetBodyInterface()->AimHeadTowards( victim, IBody::IMPORTANT, 0.2f, NULL, "Looking at my victim", false, 0.08f );
		if ( me->IsRangeGreaterThan( victim, 30.0f ) || !locomotion->IsPotentiallyTraversable( me->GetAbsOrigin(), victim->GetAbsOrigin(), ILocomotion::EVENTUALLY ) )
		{
			InfectedPathCost cost( me );
			m_path.Update( me, victim, cost );
			if ( !m_path.IsValid() ) locomotion->Approach( victim->GetAbsOrigin(), 1.0f );
		}
	}
	return Continue();
}
EventDesiredResult< Tank > TankAttack::OnMoveToSuccess( Tank *me, const Path *path ) // S:473980
{
	m_stuckTimer.Invalidate();
	return TryContinue();
}
EventDesiredResult< Tank > TankAttack::OnMoveToFailure( Tank *me, const Path *path, MoveToFailureType reason ) // S:4739D0
{
	if ( m_stuckTimer.GetStartTime() <= 0.0f ) m_stuckTimer.Start();
	if ( reason == FAIL_NO_PATH_EXISTS ) TerrorGameRules()->OnInfectedBotPathFailed( 3.2f, m_victim.Get() ); // S:2DC5D0
	return TryContinue();
}
EventDesiredResult< Tank > TankAttack::OnStuck( Tank *me ) // S:473A70
{
	if ( m_stuckTimer.GetStartTime() <= 0.0f ) m_stuckTimer.Start();
	return TryContinue();
}
EventDesiredResult< Tank > TankAttack::OnUnStuck( Tank *me ) // S:473AE0
{
	m_stuckTimer.Invalidate();
	return TryContinue();
}
EventDesiredResult< Tank > TankAttack::OnInjured( Tank *me, const CTakeDamageInfo &info ) // S:473B30
{
	if ( ( info.GetDamageType() & DMG_BURN ) && ToBasePlayer( info.GetAttacker() ) ) m_stuckTimer.Invalidate();
	FireTankIgnitedEvent( me, info );
	return TryToSustain();
}
EventDesiredResult< Tank > TankAttack::OnContact( Tank *me, CBaseEntity *other, CGameTrace *trace ) // S:473C70
{
	CTerrorPlayer *player = ToTerrorPlayer( other );
	if ( player )
	{
		m_stuckTimer.Invalidate();
		if ( player->GetTeamNumber() == TEAM_SURVIVOR && !player->IsIncapacitated() && ( !m_victim.Get() || !m_victim->IsIT() ) ) m_victim = player;
	}
	return TryToSustain();
}
EventDesiredResult< Tank > TankAttack::OnShoved( Tank *me, CBaseEntity *pusher ) // S:473D60
{
	m_stuckTimer.Invalidate();
	return TryToSustain();
}
EventDesiredResult< Tank > TankAttack::OnCommandAttack( Tank *me, CBaseEntity *target ) // S:473DC0
{
	CTerrorPlayer *player = ToTerrorPlayer( target );
	if ( player && player->GetTeamNumber() != TEAM_L4D1_SURVIVOR && !( player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 ) ) m_victim = player;
	return TryContinue();
}
EventDesiredResult< Tank > TankAttack::OnCommandApproach( Tank *me, const Vector &position, float range ) // S:474170
{
	return TrySuspendFor( new TankMoveTo( position ), RESULT_CRITICAL, "Debug move to" );
}
TankMoveTo::TankMoveTo( const Vector &position ) : m_goal( position ) { m_path.Invalidate(); } // S:473E40
ActionResult< Tank > TankMoveTo::OnStart( Tank *me, Action< Tank > *prior ) // S:473EE0
{
	InfectedPathCost cost( me );
	return m_path.Compute( me, m_goal, cost ) ? Continue() : Done( "No path to goal" );
}
ActionResult< Tank > TankMoveTo::Update( Tank *me, float interval ) // S:473F30
{
	m_path.Update( me );
	return m_path.IsValid() ? Continue() : Done();
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
