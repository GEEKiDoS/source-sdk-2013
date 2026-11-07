//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/special/spitter_actions.h"
#include "l4d2/infected/boss_zombie_contact.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/director/zombie_spawn.h"
#include "nav_pathfind.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
ConVar z_spitter_range( "z_spitter_range", "850", 0x00004004 ); // S5A9860
ConVar z_spitter_high_chance( "z_spitter_high_chance", "10", 0x00004004 ); // S5A9890; original4694E0 uses20, NOT this cvar.
ConVar z_spitter_max_wait_time( "z_spitter_max_wait_time", "30", 0x00004004 ); // S5A98C0

// S208540/208620: the reference origin is the actor's absolute origin, not
// eye/center. The forward vector still comes from EyeVectors.
static bool SpitterFacingPoint( Spitter *me, const Vector &point, float tolerance )
{
	Vector forward;
	me->EyeVectors( &forward );
	Vector direction = point - me->GetAbsOrigin();
	VectorNormalize( direction );
	return DotProduct( direction, forward ) >= tolerance;
}
static bool SpitterAbilityReady( Spitter *me ) // S305790 calls CanActivate, not IsReadyToActivate.
{
	CBaseAbility *ability = me->GetCustomAbility();
	return ability && ability->CanActivate();
}
static void SpitterChase( Spitter *me, CTerrorPlayer *victim, TerrorDirectChasePath &path )
{
	InfectedPathCost cost( me );
	path.Update( me, victim, cost );
	ILocomotion *mover = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	if ( path.IsValid() )
	{
		if ( !mover->IsClimbingOrJumping() && !mover->IsUsingLadder() && mover->IsOnGround() && !body->IsActivity( ACT_RUN ) )
			body->StartActivity( ACT_RUN );
	}
	else mover->Approach( victim->GetAbsOrigin(), 1.0f );
}

Action< Spitter > *SpitterBehavior::InitialContainedAction( Spitter *me ) // S467BC0
{
	return TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "SpecialInfectedAssault", 0 ) > 0 ?
		static_cast< Action< Spitter > * >( new SpitterAssault ) : static_cast< Action< Spitter > * >( new SpitterPrepareAmbush );
}
EventDesiredResult< Spitter > SpitterBehavior::OnContact( Spitter *me, CBaseEntity *other, CGameTrace *trace ) // S467C60
{
	BossZombieContact( me, other, trace, m_lastStuckTime, true );
	return TryContinue();
}
EventDesiredResult< Spitter > SpitterBehavior::OnStuck( Spitter *me ) // S467C30
{
	m_lastStuckTime = gpGlobals->curtime;
	return TryContinue();
}
SpitterAssault::SpitterAssault() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ) {} // S467AA0
ActionResult< Spitter > SpitterAssault::OnStart( Spitter *me, Action< Spitter > *prior ) // S4674C0
{
	m_victim = NULL;
	m_path.Invalidate();
	m_spitTimer.Invalidate();
	m_isSpitting = false;
	me->SetSpitTarget( NULL );
	return Continue();
}
ActionResult< Spitter > SpitterAssault::Update( Spitter *me, float interval ) // S467530
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	IVision *vision = me->GetVisionInterface();
	if ( m_isSpitting && !( m_spitTimer.m_timestamp > 0.0f ) && ( !me->GetSpitTarget() || !me->GetSpitTarget()->IsAlive() ) )
	{
		m_isSpitting = false;
		me->SetSpitTarget( NULL );
	}
	if ( !m_isSpitting ) m_victim = me->SelectVictim( m_victim );
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim ) return Continue();
	me->SetSpitTarget( victim );
	body->AimHeadTowards( victim, IBody::CRITICAL, 0.5f, NULL, NULL, true, 0.25f );
	if ( !m_isSpitting ) SpitterChase( me, victim, m_path );
	else if ( m_spitTimer.m_timestamp > 0.0f )
	{
		if ( gpGlobals->curtime >= m_spitTimer.m_timestamp )
		{
			m_spitTimer.Invalidate();
			m_isSpitting = false;
		}
	}
	else
	{
		Vector aim = me->GetSpitTarget()->GetAbsOrigin();
		aim.z = me->GetAbsOrigin().z;
		if ( SpitterFacingPoint( me, aim, 0.98f ) && SpitterAbilityReady( me ) )
		{
			me->PressFireButton();
			if ( !( m_spitTimer.m_timestamp > 0.0f ) ) m_spitTimer.Start( 1.0f );
		}
	}
	if ( vision->IsLineOfSightClearToEntity( victim ) && SpitterFacingPoint( me, victim->GetAbsOrigin(), 0.8f ) && ( me->GetFlags() & FL_ONGROUND ) )
	{
		if ( SpitterAbilityReady( me ) )
		{
			const float range = z_spitter_range.GetFloat();
			const double distanceSquared = me->GetRangeSquaredTo( victim );
			if ( static_cast< double >( range ) * range > distanceSquared )
			{
				mover->Stop();
				m_isSpitting = true;
				return Continue();
			}
		}
		if ( me->GetRangeSquaredTo( victim ) < 1764.0f ) me->PressAltFireButton();
	}
	return Continue();
}

// S468F40/4694E0: own genuine ambush collector, not the retreat cover
// collector. The front survivor is selected again by callers where native does.
class CollectSpitterAmbushAreas
{
public:
	explicit CollectSpitterAmbushAreas( TerrorNavArea *front ) : m_rejectDuplicates( false ), m_front( front ), m_frontFlow( front->GetFlowDistance() ) { m_areas.EnsureCapacity( 512 ); }
	bool operator()( CNavArea *base )
	{
		TerrorNavArea *area = dynamic_cast< TerrorNavArea * >( base );
		if ( !area || ( m_rejectDuplicates && m_areas.Find( area ) != m_areas.InvalidIndex() ) ) return true;
		if ( area->HasSpawnAttributes( TERROR_NAV_ESCAPE_ROUTE ) &&
			( !area->GetPlayerCount( TEAM_SURVIVOR ) || (float)area->GetPlayerCount( TEAM_SURVIVOR ) <= 18.0f ) &&
			area->GetFlowDistance() > m_frontFlow && !area->IsCompletelyVisible( m_front ) &&
			area->GetSizeX() >= 30.0f && area->GetSizeY() >= 30.0f && !area->HasAttributes( NAV_MESH_CROUCH ) && !area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			m_areas.AddToTail( area );
		return true;
	}
	TerrorNavArea *Select()
	{
		if ( !m_areas.Count() ) return NULL;
		if ( RandomInt( 1, 100 ) <= 20 )
		{
			float highest = -99999.0f;
			int selected = -1;
			FOR_EACH_VEC( m_areas, i )
				if ( m_areas[i]->GetCenter().z > highest ) { selected = i; highest = m_areas[i]->GetCenter().z; }
			if ( selected > -1 ) return m_areas[selected];
		}
		return m_areas[RandomInt( 0, m_areas.Count() - 1 )];
	}
private:
	bool m_rejectDuplicates;
	TerrorNavArea *m_front;
	float m_frontFlow;
	CUtlVector< TerrorNavArea * > m_areas;
};
static CNavArea *FindSpitterAmbushArea( Spitter *me, float range )
{
	CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
	if ( !front || !front->GetLastKnownArea() ) return NULL;
	CollectSpitterAmbushAreas collect( ToTerrorNavArea( front->GetLastKnownArea() ) );
	SearchSurroundingAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), collect, range, 0, TEAM_ANY );
	return collect.Select();
}
ActionResult< Spitter > SpitterPrepareAmbush::OnStart( Spitter *me, Action< Spitter > *prior ) // S467FB0
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_ambushArea = NULL;
	m_path.Invalidate();
	m_failureCount = 0;
	return Continue();
}
ActionResult< Spitter > SpitterPrepareAmbush::Update( Spitter *me, float interval ) // S469630
{
	ILocomotion *mover = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	if ( ( TheDirector->IsFinale() || me->IsAggressive() ) && SpitterAbilityReady( me ) ) return ChangeTo( new SpitterAttack, "Finale started" );
	if ( !m_ambushArea ) m_ambushArea = FindSpitterAmbushArea( me, z_spitter_range.GetFloat() * 1.5f );
	if ( m_ambushArea )
	{
		if ( me->GetLastKnownArea() == m_ambushArea )
		{
			m_path.Invalidate();
			if ( !body->IsActivity( ACT_IDLE ) ) body->StartActivity( ACT_IDLE );
			return ChangeTo( new SpitterWaitInAmbush, "Made it to my ambush spot. Waiting." );
		}
		if ( !m_path.IsValid() )
		{
			InfectedPathCost cost( me );
			if ( !m_path.Compute( me, m_ambushArea->GetCenter(), cost ) ) { ++m_failureCount; m_ambushArea = NULL; }
		}
		if ( m_path.IsValid() )
		{
			m_failureCount = 0;
			m_path.Update( me );
			if ( !mover->IsClimbingOrJumping() && !mover->IsUsingLadder() && mover->IsOnGround() && !body->IsActivity( ACT_RUN ) ) body->StartActivity( ACT_RUN );
			if ( mover->IsStuck() )
			{
				Msg( "Spitter %d mover is STUCK\n", me->entindex() );
				return ChangeTo( new SpitterAttack, "Mover got stuck - attacking" );
			}
		}
		if ( m_failureCount > 10 ) Msg( "Spitter %d is failing excessively\n", me->entindex() );
		return Continue();
	}
	if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 0, 0.0f, NULL ) ) return ChangeTo( new SpitterAttack, "Did not locate an ambush spot - EXPOSED" );
	return ChangeTo( new SpitterWaitInAmbush, "Did not locate an ambush spot but still hidden" );
}
EventDesiredResult< Spitter > SpitterPrepareAmbush::OnInjured( Spitter *me, const CTakeDamageInfo &info ) // S468B40
{
	return SpitterAbilityReady( me ) ? TryChangeTo( new SpitterAttack ) : TryToSustain();
}
EventDesiredResult< Spitter > SpitterPrepareAmbush::OnCommandAssault( Spitter *me ) // S468020
{
	return TryChangeTo( new SpitterAssault, RESULT_CRITICAL, "Assault!" );
}
ActionResult< Spitter > SpitterWaitInAmbush::OnStart( Spitter *me, Action< Spitter > *prior ) // S468060
{
	me->GetBodyInterface()->SetArousal( IBody::INTENSE );
	me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	m_ambushArea = NULL;
	m_waitTimer.Start( z_spitter_max_wait_time.GetFloat() );
	return Continue();
}
ActionResult< Spitter > SpitterWaitInAmbush::Update( Spitter *me, float interval ) // S468C60
{
	IBody *body = me->GetBodyInterface();
	CTerrorPlayer *front = FindHighestFlowSurvivor( 0 );
	body->AimHeadTowards( front, IBody::BORING, 0.0f, NULL, NULL, true, 0.25f );
	if ( ( TheDirector->IsFinale() || me->IsAggressive() ) && SpitterAbilityReady( me ) ) return ChangeTo( new SpitterAttack, "Finale started" );
	if ( !m_ambushArea ) m_ambushArea = me->GetLastKnownArea();
	if ( m_ambushArea )
	{
		if ( m_waitTimer.m_timestamp > 0.0f && gpGlobals->curtime >= m_waitTimer.m_timestamp ) return ChangeTo( new SpitterAttack, "I have been hiding for too long." );
		CTerrorPlayer *leader = FindHighestFlowSurvivor( 0 );
		if ( leader )
		{
			const float ownFlow = (float)me->GetFlowDistance( 0 );
			if ( leader->GetFlowDistance( 0 ) > ownFlow ) return ChangeTo( new SpitterAttack, "Survivors passed me on the flow" );
		}
		if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR, 0, 0.0f, ToTerrorNavArea( m_ambushArea ) ) )
		{
			if ( SpitterAbilityReady( me ) ) return ChangeTo( new SpitterAttack, "Survivors revealed my position" );
			return ChangeTo( new SpitterPrepareAmbush, "I can't spit so I have to move" );
		}
		if ( front && front->GetLastKnownArea() && ToTerrorNavArea( m_ambushArea )->GetFlowDistance() - ToTerrorNavArea( front->GetLastKnownArea() )->GetFlowDistance() <= 720.0f && SpitterAbilityReady( me ) )
			return ChangeTo( new SpitterAttack, "Springing my ambush!" );
	}
	return Continue();
}
EventDesiredResult< Spitter > SpitterWaitInAmbush::OnInjured( Spitter *me, const CTakeDamageInfo &info ) // S468BD0
{
	return SpitterAbilityReady( me ) ? TryChangeTo( new SpitterAttack ) : TryToSustain();
}
EventDesiredResult< Spitter > SpitterWaitInAmbush::OnCommandAssault( Spitter *me ) // S468130
{
	return TryChangeTo( new SpitterAssault, RESULT_CRITICAL, "Assault!" );
}
SpitterAttack::SpitterAttack() : m_victim( NULL ), m_path( ChasePath::DONT_LEAD_SUBJECT ) {} // S4683C0
ActionResult< Spitter > SpitterAttack::OnStart( Spitter *me, Action< Spitter > *prior ) // S468170
{
	m_victim = NULL;
	m_path.Invalidate();
	m_spitTimer.Invalidate();
	m_isSpitting = false;
	me->SetSpitTarget( NULL );
	return Continue();
}
ActionResult< Spitter > SpitterAttack::Update( Spitter *me, float interval ) // S4684D0
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	IVision *vision = me->GetVisionInterface();
	if ( m_isSpitting && !( m_spitTimer.m_timestamp > 0.0f ) && ( !me->GetSpitTarget() || !me->GetSpitTarget()->IsAlive() ) )
	{
		m_isSpitting = false;
		me->SetSpitTarget( NULL );
	}
	(void)me->ShouldRetreatToCover(); // Native4685A8 invokes this virtual even though its result is unused.
	if ( !m_isSpitting ) m_victim = me->SelectVictim( m_victim );
	CTerrorPlayer *victim = m_victim.Get();
	if ( !victim )
	{
		mover->Stop();
		return ChangeTo( new SpitterPrepareAmbush );
	}
	if ( m_path.IsValid() && mover->IsUsingLadder() )
	{
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost );
		return Continue();
	}
	me->SetSpitTarget( victim );
	body->AimHeadTowards( victim, m_isSpitting ? IBody::CRITICAL : IBody::IMPORTANT, 0.5f, NULL, "Track Victim", true, 0.25f );
	if ( !m_isSpitting ) SpitterChase( me, victim, m_path );
	else if ( !( m_spitTimer.m_timestamp > 0.0f ) )
	{
		Vector aim = me->GetSpitTarget()->GetAbsOrigin();
		aim.z = me->GetAbsOrigin().z;
		if ( SpitterFacingPoint( me, aim, 0.98f ) && SpitterAbilityReady( me ) )
		{
			me->PressFireButton();
			if ( !( m_spitTimer.m_timestamp > 0.0f ) ) m_spitTimer.Start( 1.0f );
		}
	}
	else if ( gpGlobals->curtime >= m_spitTimer.m_timestamp )
	{
		if ( RandomInt( 0, 100 ) < 50 ) return ChangeTo( new SpitterPrepareAmbush, "Done spitting" );
		m_spitTimer.Invalidate();
		m_isSpitting = false;
	}
	if ( vision->IsLineOfSightClearToEntity( victim ) && SpitterFacingPoint( me, victim->GetAbsOrigin(), 0.8f ) && ( me->GetFlags() & FL_ONGROUND ) )
	{
		bool inSpitRange = false;
		if ( SpitterAbilityReady( me ) )
		{
			const float range = z_spitter_range.GetFloat();
			const double distanceSquared = me->GetRangeSquaredTo( victim );
			inSpitRange = static_cast< double >( range ) * range > distanceSquared;
		}
		if ( inSpitRange )
		{
			mover->Stop();
			m_isSpitting = true;
		}
		else if ( me->GetRangeSquaredTo( victim ) < 1764.0f ) me->PressAltFireButton();
	}
	return Continue();
}
EventDesiredResult< Spitter > SpitterAttack::OnCommandAssault( Spitter *me ) // S4681E0
{
	return TryChangeTo( new SpitterAssault, RESULT_CRITICAL, "Assault!" );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
