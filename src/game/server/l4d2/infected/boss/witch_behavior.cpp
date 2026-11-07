//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "npcevent.h"
#include "l4d2/infected/boss/witch_behavior.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/infected/boss/witch_locomotion.h"
#include "l4d2/infected/boss/witch_body.h"
#include "l4d2/infected/boss/witch_vision.h"
#include "l4d2/infected/common/infected_action_helpers.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav.h"
#include "l4d2/nav/terror_nav_search.h"
#include "l4d2/director/zombie_spawn.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "EntityFlame.h"
#include "soundent.h"
#include "obstacle_pushaway.h"
#include "world.h"

#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

ConVar z_witch_anger_rate( "z_witch_anger_rate", "0.2", 0x00004004 );
ConVar z_witch_relax_rate( "z_witch_relax_rate", "0.05", 0x00004004 );
ConVar z_witch_threat_hostile_range( "z_witch_threat_hostile_range", "600", 0x00004004 );
ConVar z_witch_hostile_at_me_anger( "z_witch_hostile_at_me_anger", "2", 0x00004004 );
ConVar z_witch_allow_change_victim( "z_witch_allow_change_victim", "1", 0x00004004 );
ConVar witch_force_wander( "witch_force_wander", "0", 0x00004004 );
ConVar z_witch_burn_time( "z_witch_burn_time", "15", 0x00004004 );
ConVar z_witch_damage_per_kill_hit( "z_witch_damage_per_kill_hit", "30", 0x00004004 );
ConVar z_witch_min_retreat_range( "z_witch_min_retreat_range", "750", 0x00004004 );
ConVar z_witch_max_retreat_range( "z_witch_max_retreat_range", "2000", 0x00004004 );
ConVar z_witch_retreat_exit_range( "z_witch_retreat_exit_range", "1000", 0x00004004 );
ConVar z_witch_retreat_exit_hidden_duration( "z_witch_retreat_exit_hidden_duration", "10", 0x00004004 );
ConVar z_witch_retreat_min_duration( "z_witch_retreat_min_duration", "10", 0x00004004 );
ConVar z_witch_wander_hear_radius( "z_witch_wander_hear_radius", "72", 0x00004004 );
ConVar z_witch_wander_personal_space( "z_witch_wander_personal_space", "240", 0x00004004 );
ConVar z_witch_wander_personal_time( "z_witch_wander_personal_time", "10", 0x00004004 );
ConVar z_witch_wander_music_min_interval( "z_witch_wander_music_min_interval", "3.0", 0x00004004 );
ConVar z_witch_wander_music_max_interval( "z_witch_wander_music_max_interval", "20.0", 0x00004004 );
ConVar z_witch_wander_music_min_dist( "z_witch_wander_music_min_dist", "90000", 0x00004004 );
ConVar z_witch_wander_music_max_dist( "z_witch_wander_music_max_dist", "2000000", 0x00004004 );
extern ConVar z_witch_threat_normal_range;
extern ConVar z_witch_personal_space;
extern ConVar z_witch_attack_range;
extern ConVar z_witch_speed;
extern ConVar z_witch_speed_inured;
extern ConVar survivor_incap_health;
extern bool IsInCommentaryMode();

namespace
{
inline Witch *AsWitch( Infected *me ) { return static_cast< Witch * >( me ); }
inline Activity WitchActivity( int original ) { return TerrorActivityFromOriginal( original ); }
inline bool TimerElapsed( const CountdownTimer &timer ) { return timer.IsElapsed(); }

// S41E130 deliberately admits TEAM2 only, unlike several TEAM2-or4 callers.
bool IsWitchVictim( CBaseEntity *entity )
{
	return entity && entity->IsPlayer() && entity->IsAlive() && entity->GetTeamNumber() == TEAM_SURVIVOR;
}

bool CanWitchReactTo( CBaseEntity *entity )
{
	return IsWitchVictim( entity ) && ( !IsInCommentaryMode() || !entity->IsPlayer() || static_cast< CBasePlayer * >( entity )->IsBot() );
}

void SetWitchFlex( Infected *me, const char *name, float value )
{
	me->SetFlexWeight( me->FindFlexController( name ), value );
}

void QueueWitchConcept( Infected *me, AIConcept_t concept, const AI_CriteriaSet &criteria )
{
	// S02A570 stores this literal absolute zero, not curtime plus a delay.
	ResponseQueueManager().Queue( concept, criteria, 0.0f, ResponseTarget_t( ResponseTarget_t::ANY ), me );
}
}

Action< Infected > *WitchExecAction::InitialContainedAction( Infected *me ) { return new WitchBehavior; }
Action< Infected > *WitchBehavior::InitialContainedAction( Infected *me )
{
	return witch_force_wander.GetBool() || IsMapTimeOfDay( 2, 4 ) ?
		static_cast< Action< Infected > * >( new WitchWander ) : new WitchIdle;
}
ActionResult< Infected > WitchBehavior::Update( Infected *me, float interval ) { return Continue(); }

ActionResult< Infected > WitchExecAction::Update( Infected *me, float interval )
{
	if ( me && me->IsOnFire() && me->IsAlive() && AsWitch( me )->GetTimeSinceIgnited() > z_witch_burn_time.GetFloat() )
		me->TakeDamage( CTakeDamageInfo( me, me, 9999.9004f, DMG_BURN ) );
	return Continue();
}

EventDesiredResult< Infected > WitchExecAction::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
	if ( info.GetDamageType() & 0x20000187 ) me->AddGesture( WitchActivity( 693 ), true );
	return TryContinue();
}

EventDesiredResult< Infected > WitchBehavior::OnIgnite( Infected *me )
{
	CBaseEntity *arsonist = NULL;
	CEntityFlame *flame = dynamic_cast< CEntityFlame * >( me->GetEffectEntity() );
	if ( flame && IsWitchVictim( flame->GetAttacker() ) ) arsonist = flame->GetAttacker();
	else arsonist = AsWitch( me )->GetHarasser();
	me->GetLocomotionInterface()->SetSpeedLimit( z_witch_speed_inured.GetFloat() );
	return TrySuspendFor( new WitchBurn( arsonist ), RESULT_CRITICAL, "Caught on fire!" );
}

EventDesiredResult< Infected > WitchBehavior::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
	if ( ( info.GetDamageType() & DMG_BLAST ) && info.GetDamage() / ( info.GetMaxDamage() + 0.001f ) > 0.5f )
	{
		ILocomotion *move = me->GetLocomotionInterface();
		if ( move ) move->SetSpeedLimit( z_witch_speed_inured.GetFloat() );
		if ( info.GetInflictor() ) me->GetBodyInterface()->AimHeadTowards( info.GetInflictor(), IBody::IMPORTANT, 0.0f );
		me->EmitSound( "WitchZombie.Rage" );
		return TrySuspendFor( new InfectedShoved( info.GetInflictor() ), RESULT_TRY );
	}
	return TryContinue();
}

EventDesiredResult< Infected > WitchBehavior::OnKilled( Infected *me, const CTakeDamageInfo &info )
{
	// Original priority4 outranks the ordinary CRITICAL3 transitions.
	return TryChangeTo( new WitchDying( info ), static_cast< EventResultPriorityType >( 4 ) );
}

WitchAngry::WitchAngry( float growlDelay ) { m_growlTimer.Start( growlDelay ); }
ActionResult< Infected > WitchAngry::OnStart( Infected *me, Action< Infected > *prior )
{
	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->SetArousal( IBody::ALERT );
		body->StartActivity( WitchActivity( 682 ), 1 );
		body->SetDesiredPosture( IBody::STAND );
	}
	m_initialAngerTimer.Start( 1.0f );
	m_previousRage = 0.0f;
	SetWitchFlex( me, "wrinkler", 1.0f );
	AI_CriteriaSet criteria;
	QueueWitchConcept( me, "WitchStartAngry", criteria );
	return Continue();
}
void WitchAngry::OnEnd( Infected *me, Action< Infected > *next ) { SetWitchFlex( me, "wrinkler", 0.0f ); }
ActionResult< Infected > WitchAngry::OnSuspend( Infected *me, Action< Infected > *interrupt )
{
	SetWitchFlex( me, "wrinkler", 0.0f ); return Continue();
}
ActionResult< Infected > WitchAngry::OnResume( Infected *me, Action< Infected > *interrupt )
{
	SetWitchFlex( me, "wrinkler", 1.0f ); return Continue();
}
void WitchAngry::UpdateGrowl( Witch *me )
{
	const float rage = me->GetRage();
	const char *sound = "WitchZombie.GrowlLow";
	if ( rage >= 0.3f )
	{
		if ( rage >= 0.6f )
		{
			sound = "WitchZombie.GrowlHigh";
			if ( m_previousRage < 0.6f ) m_growlTimer.Invalidate();
		}
		else
		{
			sound = "WitchZombie.GrowlMedium";
			if ( m_previousRage < 0.3f ) m_growlTimer.Invalidate();
		}
	}
	if ( TimerElapsed( m_growlTimer ) )
	{
		me->EmitSound( sound );
		m_growlTimer.Start( rage >= 0.6f ? RandomFloat( 1.5f, 2.0f ) : RandomFloat( 0.75f, 1.5f ) );
	}
	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		if ( m_previousRage < 0.54f && rage >= 0.54f )
		{
			if ( !body->IsActivity( WitchActivity( 683 ) ) ) body->StartActivity( WitchActivity( 683 ), 1 );
		}
		else if ( m_previousRage >= 0.54f && rage < 0.54f )
		{
			if ( !body->IsActivity( WitchActivity( 682 ) ) ) body->StartActivity( WitchActivity( 682 ), 1 );
		}
	}
	m_previousRage = rage;
}
ActionResult< Infected > WitchAngry::Update( Infected *actor, float interval )
{
	Witch *me = AsWitch( actor );
	UpdateGrowl( me );
	CBaseEntity *attacker = me->GetLastAttacker();
	if ( me->GetTimeSinceLastInjury( TEAM_SURVIVOR ) < 5.0f && CanWitchReactTo( attacker ) )
		return SuspendFor( new WitchAttack( attacker ), "Resumed Angry when recently attacked" );
	CBaseEntity *threat = me->GetVisionInterface()->GetClosestKnown();
	if ( threat )
	{
		float range = me->IsHostileTo( threat ) ? z_witch_threat_hostile_range.GetFloat() : z_witch_threat_normal_range.GetFloat();
		if ( me->IsRangeGreaterThan( threat, range + 50.0f ) ) threat = NULL;
		else me->GetBodyInterface()->AimHeadTowards( threat, IBody::IMPORTANT, 1.0f );
	}
	if ( !TimerElapsed( m_initialAngerTimer ) || threat )
	{
		float anger = 1.0f;
		if ( threat )
		{
			if ( me->IsHostileTo( threat ) )
			{
				CBaseCombatCharacter *character = threat->MyCombatCharacterPointer();
				anger = character && character->IsLookingTowards( me, 0.98f ) ?
					z_witch_hostile_at_me_anger.GetFloat() : z_witch_hostile_at_me_anger.GetFloat() * 0.5f;
			}
			const float distance = me->GetRangeTo( threat );
			if ( distance > z_witch_threat_normal_range.GetFloat() )
				anger = ( 1.0f - ( distance - z_witch_threat_normal_range.GetFloat() ) /
					( z_witch_threat_hostile_range.GetFloat() - z_witch_threat_normal_range.GetFloat() ) ) * anger;
			if ( TerrorGameRules()->GetDifficultyLevel() == 0 ) anger *= 0.5f;
		}
		me->ChangeRage( ( z_witch_anger_rate.GetFloat() * anger ) * interval );
	}
	else
	{
		me->ChangeRage( -( z_witch_relax_rate.GetFloat() * interval ) );
		if ( me->GetRage() <= 0.0f ) return ChangeTo( new WitchIdle );
	}
	if ( TimerElapsed( m_initialAngerTimer ) && me->GetRage() >= 1.0f && IsWitchVictim( threat ) )
		return SuspendFor( new WitchAttack( threat ) );
	return Continue();
}
EventDesiredResult< Infected > WitchAngry::OnContact( Infected *actor, CBaseEntity *other, CGameTrace *trace )
{
	if ( other && other->IsAlive() && other->IsPlayer() )
		AsWitch( actor )->ChangeRage( ( z_witch_anger_rate.GetFloat() * z_witch_hostile_at_me_anger.GetFloat() ) * gpGlobals->frametime );
	return TryContinue();
}
EventDesiredResult< Infected > WitchAngry::OnInjured( Infected *actor, const CTakeDamageInfo &info )
{
	AsWitch( actor )->ChangeRage( 1.0f );
	return CanWitchReactTo( info.GetAttacker() ) ? TrySuspendFor( new WitchAttack( info.GetAttacker() ), RESULT_TRY ) : TryContinue();
}
EventDesiredResult< Infected > WitchAngry::OnShoved( Infected *actor, CBaseEntity *pusher )
{
	AsWitch( actor )->ChangeRage( 1.0f );
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( pusher );
	if ( CanWitchReactTo( pusher ) && ( !player || player->GetTeamNumber() != TEAM_INFECTED ) )
		return TrySuspendFor( new WitchAttack( pusher ), RESULT_TRY );
	return TryContinue();
}
EventDesiredResult< Infected > WitchAngry::OnBlinded( Infected *actor, CBaseEntity *blinder )
{
	AsWitch( actor )->ChangeRage( 1.0f );
	return TryChangeTo( new InfectedBlinded( new WitchAngry( 0.0f ), "WitchZombie.GrowlHigh" ), RESULT_CRITICAL );
}
EventDesiredResult< Infected > WitchAngry::OnHitByVomitJar( Infected *actor, CBaseEntity *attacker )
{
	AsWitch( actor )->ChangeRage( 1.0f );
	if ( CanWitchReactTo( attacker ) ) return TrySuspendFor( new WitchAttack( attacker ), RESULT_IMPORTANT, "Vomit thrown on me" );
	return TryContinue();
}
EventDesiredResult< Infected > WitchAngry::OnCommandAttack( Infected *actor, CBaseEntity *victim )
{
	if ( AsWitch( actor )->GetRage() < 1.0f ) return TryContinue();
	return TrySuspendFor( new WitchAttack( victim ), RESULT_IMPORTANT, "I was commanded to attack" );
}

WitchIdle::WitchIdle() : m_heardFireSource( NULL ) {}
ActionResult< Infected > WitchIdle::OnStart( Infected *actor, Action< Infected > *prior )
{
	Witch *me = AsWitch( actor );
	if ( me->GetRage() > 0.0f ) return ChangeTo( new WitchAngry );
	me->SetHarasser( NULL );
	IBody *body = me->GetBodyInterface();
	if ( body )
	{
		body->SetArousal( IBody::NEUTRAL );
		if ( !body->StartActivity( WitchActivity( 684 ), 0 ) ) return ChangeTo( new WitchAngry );
		body->SetDesiredPosture( IBody::SIT );
	}
	m_despairTimer.Start( 2.0f );
	SetWitchFlex( me, "presser", 1.0f );
	m_heardFireTimer.Invalidate();
	m_heardFireSource = NULL;
	return Continue();
}
void WitchIdle::OnEnd( Infected *me, Action< Infected > *next ) { SetWitchFlex( me, "presser", 0.0f ); }
ActionResult< Infected > WitchIdle::OnResume( Infected *me, Action< Infected > *interrupt )
{
	if ( AsWitch( me )->GetRage() > 0.0f ) return ChangeTo( new WitchAngry, "Resumed Idle while still mad" );
	return Continue();
}
ActionResult< Infected > WitchIdle::Update( Infected *actor, float interval )
{
	Witch *me = AsWitch( actor );
	me->GetLocomotionInterface()->Stop();
	me->GetLocomotionInterface()->SetVelocity( vec3_origin );
	me->SetAbsVelocity( vec3_origin );
	if ( TimerElapsed( m_despairTimer ) )
	{
		me->EmitSound( "WitchZombie.Despair" );
		const float duration = RandomFloat( 4.0f, 6.0f );
		m_despairTimer.Start( duration );
		CSoundEnt::InsertSound( 8, me->GetAbsOrigin(), 2048, duration, me, 10 );
	}
	CBaseEntity *threat = me->GetVisionInterface()->GetClosestKnown();
	float growlDelay = 0.0f;
	if ( !threat )
	{
		if ( m_heardFireTimer.HasStarted() && TimerElapsed( m_heardFireTimer ) )
		{
			threat = m_heardFireSource.Get();
			m_heardFireTimer.Invalidate();
		}
		if ( !threat )
		{
			threat = FindClosestSurvivorPlayer( me->GetPosition(), true, true );
			if ( !me->IsRangeLessThan( threat, z_witch_personal_space.GetFloat() ) ||
				!me->GetVisionInterface()->IsLineOfSightClearToEntity( threat ) || !threat )
			{
				if ( me->GetRage() > 0.0f ) return ChangeTo( new WitchAngry, "Increased Rage made me angry" );
				return Continue();
			}
		}
		me->GetBodyInterface()->AimHeadTowards( threat, IBody::IMPORTANT, 0.0f );
		me->EmitSound( "WitchZombie.Surprised" );
		growlDelay = 1.0f;
	}
	const float range = me->IsHostileTo( threat ) ? z_witch_threat_hostile_range.GetFloat() : z_witch_threat_normal_range.GetFloat();
	if ( me->IsRangeLessThan( threat, range ) ) return ChangeTo( new WitchAngry( growlDelay ) );
	if ( me->GetRage() > 0.0f ) return ChangeTo( new WitchAngry, "Increased Rage made me angry" );
	return Continue();
}
EventDesiredResult< Infected > WitchIdle::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) { return TryContinue(); }
EventDesiredResult< Infected > WitchIdle::OnShoved( Infected *me, CBaseEntity *pusher )
{
	if ( CanWitchReactTo( pusher ) ) return TrySuspendFor( new WitchAttack( pusher ), RESULT_IMPORTANT, "Someone shoved me!" );
	return TryContinue();
}
EventDesiredResult< Infected > WitchIdle::OnBlinded( Infected *me, CBaseEntity *blinder )
{
	AsWitch( me )->ChangeRage( 1.0f );
	return TryChangeTo( new InfectedBlinded( new WitchAngry, "WitchZombie.GrowlHigh" ), RESULT_CRITICAL );
}
EventDesiredResult< Infected > WitchIdle::OnHitByVomitJar( Infected *me, CBaseEntity *attacker )
{
	AsWitch( me )->ChangeRage( 1.0f );
	if ( CanWitchReactTo( attacker ) ) return TrySuspendFor( new WitchAttack( attacker ), RESULT_IMPORTANT, "Vomit thrown on me" );
	return TryContinue();
}
EventDesiredResult< Infected > WitchIdle::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	if ( AsWitch( me )->GetRage() < 1.0f ) return TryContinue();
	return TrySuspendFor( new WitchAttack( victim ), RESULT_IMPORTANT, "I was commanded to attack" );
}
EventDesiredResult< Infected > WitchIdle::OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	if ( source && source->IsPlayer() && source->IsAlive() &&
		( source->GetTeamNumber() == TEAM_SURVIVOR || source->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		!m_heardFireTimer.HasStarted() && static_cast< CTerrorPlayer * >( source )->GetTimeSinceWeaponFired() < 1.0 &&
		z_witch_threat_normal_range.GetFloat() > me->GetRangeTo( source ) )
	{
		m_heardFireTimer.Start( 0.4f );
		m_heardFireSource = source;
	}
	return TryContinue();
}
EventDesiredResult< Infected > WitchIdle::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	if ( info.GetDamage() > 50.0f && ( info.GetDamageType() & DMG_BULLET ) &&
		me->LastHitGroup() == HITGROUP_HEAD && IsWitchVictim( attacker ) )
	{
		AsWitch( me )->ChangeRage( 1.0f );
		me->EmitSound( "WitchZombie.HeadShot" );
		return TrySuspendFor( new InfectedShoved( attacker, new WitchAttack( attacker ) ),
			RESULT_IMPORTANT, "Reacting to high-powered rifle headshot" );
	}
	if ( CanWitchReactTo( attacker ) ) return TrySuspendFor( new WitchAttack( attacker ), RESULT_IMPORTANT, "Attacking Survivor that injured me" );
	return TryContinue();
}

WitchAttack::WitchAttack( CBaseEntity *victim ) : m_path( ChasePath::LEAD_SUBJECT ) { SetVictim( victim ); }
void WitchAttack::SetVictim( CBaseEntity *victim )
{
	m_victim = victim;
	if ( victim && victim->IsPlayer() && ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		m_victimCharacter = static_cast< CTerrorPlayer * >( victim )->GetSurvivorCharacter();
	else m_victimCharacter = SurvivorCharacter_Unknown;
}
CBaseEntity *WitchAttack::GetVictim() const
{
	return m_victimCharacter != SurvivorCharacter_Unknown ?
		FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( m_victimCharacter ) ) : m_victim.Get();
}
ActionResult< Infected > WitchAttack::OnStart( Infected *actor, Action< Infected > *prior )
{
	Witch *me = AsWitch( actor );
	CTerrorPlayer *harasser = ToTerrorPlayer( me->GetHarasser() );
	if ( !me->GetHarasser() )
	{
		CTerrorPlayer *victim = ToTerrorPlayer( GetVictim() );
		if ( victim )
		{
			if ( victim->GetDisplacingEntity() ) victim = ToTerrorPlayer( victim->GetDisplacingEntity() );
			harasser = victim;
		}
		me->SetHarasser( harasser );
	}
	IBody *body = me->GetBodyInterface();
	body->SetArousal( IBody::INTENSE );
	body->SetDesiredPosture( IBody::STAND );
	me->EmitSound( "WitchZombie.Scream" );
	m_rageSoundTimer.Start( 1.0f );
	m_started.Start();
	SetWitchFlex( me, "bite", 1.0f );
	AI_CriteriaSet criteria;
	if ( harasser ) criteria.AppendCriteria( "witch_aggro_on", GetSurvivorCharacterDisplayName( harasser->GetSurvivorCharacter() ), 1.0f );
	QueueWitchConcept( me, "WitchStartAttack", criteria );
	if ( me->GetInfectedFlags() & 0x1000 ) TheDirector->OnScriptedPanicEvent( NULL, 2 );
	return Continue();
}
void WitchAttack::OnEnd( Infected *me, Action< Infected > *next )
{
	me->RemoveGesture( WitchActivity( 613 ) );
	SetWitchFlex( me, "bite", 0.0f );
}
EventDesiredResult< Infected > WitchAttack::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == 53 ) AsWitch( me )->Slash( GetVictim() );
	return TryContinue();
}
EventDesiredResult< Infected > WitchAttack::OnMoveToSuccess( Infected *me, const Path *path )
{
	IBody *body = me->GetBodyInterface();
	if ( !body->IsActivity( WitchActivity( 597 ) ) ) body->StartActivity( WitchActivity( 597 ), 0 );
	return TryContinue();
}
EventDesiredResult< Infected > WitchAttack::OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason )
{
	if ( reason != FAIL_NO_PATH_EXISTS )
	{
		if ( reason == FAIL_FELL_OFF ) m_path.Invalidate();
		return TryContinue();
	}
	if ( me->IsOnFire() ) return TryDone( RESULT_IMPORTANT, "Attack path failed while we're on fire" );
	return TryChangeTo( new WitchRetreat, RESULT_CRITICAL, "Attacker out of reach" );
}
ActionResult< Infected > WitchAttack::Update( Infected *actor, float interval )
{
	Witch *me = AsWitch( actor );
	ILocomotion *move = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	CBaseEntity *victim = GetVictim();
	CTerrorPlayer *player = ToTerrorPlayer( victim );
	if ( !victim || !player )
	{
		if ( me->IsOnFire() ) return Done( "Lost our victim" );
		return SuspendFor( new WitchRetreat, "Lost our victim" );
	}
	body->AimHeadTowards( victim, IBody::IMPORTANT, 1.0f, NULL, NULL, true, 0.25f );
	if ( !victim->IsAlive() )
	{
		if ( victim->GetTeamNumber() == TEAM_SURVIVOR || victim->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		{
			if ( me->IsOnFire() ) return Done( "Killed our Survivor victim" );
			return ChangeTo( new WitchRetreat, "Retreating in horror from killing a Survivor" );
		}
		return Done( "Killed Infected that was harrassing me" );
	}
	me->ChangeRage( 1.0f );
	if ( TimerElapsed( m_rageSoundTimer ) )
	{
		me->EmitSound( "WitchZombie.Rage" );
		m_rageSoundTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	}
	if ( !player->IsStill( true ) || move->IsScrambling() || me->IsRangeGreaterThan( victim, 30.0f ) )
	{
		if ( player->IsIncapacitated() ) move->Walk(); else move->Run();
		InfectedPathCost cost( me );
		m_path.Update( me, victim, cost, NULL );
	}
	else
	{
		me->SetAbsVelocity( vec3_origin );
		move->FaceTowards( player->GetAbsOrigin() );
		if ( player->IsIncapacitated() )
		{
			me->EmitSound( "WitchZombie.RageBeforeKillingFrenzy" );
			return SuspendFor( new InfectedStandingActivity( WitchActivity( 687 ), -1.0f, new WitchKillIncapVictim( victim ) ),
				"Victim is incapacitated - going in for the kill" );
		}
	}
	if ( !move->IsScrambling() )
	{
		if ( me->IsRangeLessThan( victim, z_witch_attack_range.GetFloat() * 1.5f ) &&
			!me->IsPlayingGesture( WitchActivity( 613 ) ) && me->GetVisionInterface()->IsLineOfSightClearToEntity( victim ) )
		{
			me->AddGesture( WitchActivity( 613 ), true );
			m_shovedPlayers.RemoveAll();
		}
		Activity activity = !body->IsPostureMobile() ? WitchActivity( 608 ) : WitchActivity( me->IsOnFire() ? 598 : 597 );
		if ( !body->IsActivity( activity ) ) body->StartActivity( activity, 0 );
	}
	return Continue();
}
EventDesiredResult< Infected > WitchAttack::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	if ( !other ) return TryToSustain();
	if ( IsBreakableEntity( other, false, true, NULL ) || other->ClassMatches( "prop_door*" ) || other->ClassMatches( "func_door*" ) )
	{
		if ( trace && trace->plane.normal.z >= 0.8f ) return TryToSustain();
	}
	else if ( other->IsPlayer() )
	{
		CTerrorPlayer *player = ToTerrorPlayer( other );
		if ( me->GetLocomotionInterface()->IsAttemptingToMove() )
		{
			CBaseEntity *victim = GetVictim();
			if ( victim && victim->entindex() != other->entindex() && !player->IsStaggeringOrPending() )
			{
				EHANDLE handle = other;
				if ( m_shovedPlayers.Find( handle ) == m_shovedPlayers.InvalidIndex() )
				{
					player->StaggerPlayer( me, NULL );
					m_shovedPlayers.AddToTail( handle );
				}
				else if ( z_witch_allow_change_victim.GetBool() && ( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_victimChangeTimer.GetStartTime() ) > 5.0 ) && IsWitchVictim( victim ) )
				{
					m_shovedPlayers.RemoveAll();
					m_victimChangeTimer.Start();
					SetVictim( other );
					m_path.Invalidate();
				}
			}
		}
		return TryToSustain();
	}
	else if ( !other->MyCombatCharacterPointer() || other->GetTeamNumber() == TEAM_SURVIVOR || other->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		return TryToSustain();
	if ( !me->IsPlayingGesture( WitchActivity( 613 ) ) ) me->AddGesture( WitchActivity( 613 ), true );
	return TryToSustain();
}

WitchKillIncapVictim::WitchKillIncapVictim( CBaseEntity *victim ) : m_victim( victim ) {}
ActionResult< Infected > WitchKillIncapVictim::OnStart( Infected *me, Action< Infected > *prior )
{
	IBody *body = me->GetBodyInterface();
	body->SetArousal( IBody::INTENSE );
	body->SetDesiredPosture( IBody::STAND );
	body->StartActivity( WitchActivity( 688 ), 1 );
	me->EmitSound( "WitchZombie.Scream" );
	m_voiceTimer.Start( 1.0f );
	SetWitchFlex( me, "bite", 1.0f );
	return Continue();
}
void WitchKillIncapVictim::OnEnd( Infected *me, Action< Infected > *next )
{
	me->RemoveGesture( WitchActivity( 613 ) );
	SetWitchFlex( me, "bite", 0.0f );
}
ActionResult< Infected > WitchKillIncapVictim::Update( Infected *me, float interval )
{
	CBaseEntity *victim = m_victim.Get();
	if ( !victim ) return ChangeTo( new WitchRetreat, "Victim became NULL" );
	if ( me->GetRangeSquaredTo( victim ) > 3600.0f ) return Done( "Someone moved our victim" );
	IBody *body = me->GetBodyInterface();
	body->AimHeadTowards( victim, IBody::IMPORTANT, 0.0f, NULL, NULL, true, 0.25f );
	me->GetLocomotionInterface()->FaceTowards( victim->WorldSpaceCenter() );
	if ( victim->IsAlive() )
	{
		AsWitch( me )->ChangeRage( 1.0f );
		if ( TimerElapsed( m_voiceTimer ) )
		{
			me->EmitSound( "WitchZombie.KillingFrenzy" );
			m_voiceTimer.Start( RandomFloat( 0.5f, 1.0f ) );
		}
		me->SetAbsVelocity( vec3_origin );
		if ( !body->IsActivity( WitchActivity( 688 ) ) ) body->StartActivity( WitchActivity( 688 ), 1 );
		return Continue();
	}
	if ( me->IsOnFire() ) return Done( "Victim is dead, and I'm on fire" );
	return ChangeTo( new WitchRetreat, "Retreating in horror from killing a Survivor" );
}
EventDesiredResult< Infected > WitchKillIncapVictim::OnAnimationEvent( Infected *me, animevent_t *event )
{
	CBaseEntity *victim = m_victim.Get();
	if ( event->event == 53 && victim )
	{
		float damage = z_witch_damage_per_kill_hit.GetFloat();
		switch ( TerrorGameRules()->GetDifficultyLevel() )
		{
		case 0: damage *= 0.5f; break;
		case 2: damage *= 2.0f; break;
		case 3: damage = static_cast< float >( survivor_incap_health.GetInt() ); break;
		}
		CTakeDamageInfo info( me, me, damage, DMG_SLASH );
		CBaseEntity *damageVictim = m_victim.Get();
		if ( damageVictim ) damageVictim->TakeDamage( info );
		CBaseEntity *soundVictim = m_victim.Get();
		if ( soundVictim ) soundVictim->EmitSound( "WitchZombie.ShredVictim" );
	}
	return TryContinue();
}
EventDesiredResult< Infected > WitchKillIncapVictim::OnShoved( Infected *me, CBaseEntity *pusher )
{
	CTerrorPlayer *player = ToTerrorPlayer( pusher );
	if ( player && !player->IsStaggeringOrPending() ) player->StaggerPlayer( me, NULL );
	return TryToSustain();
}

namespace
{
struct WitchRetreatGoal
{
	WitchRetreatGoal( Infected *actor ) : me( actor ), selected( ToTerrorNavArea( actor->GetLastKnownArea() ) ) {}
	bool operator()( TerrorNavArea *area )
	{
		Vector point = area->GetCenter() + Vector( 0, 0, 40 );
		if ( IsTerrorSpawnPointVisibleToTeam( point, TEAM_SURVIVOR ) ) return true;
		for ( int i = 0; i < NUM_CORNERS; ++i )
		{
			point = area->GetCorner( static_cast< NavCornerType >( i ) ) + Vector( 0, 0, 40 );
			if ( IsTerrorSpawnPointVisibleToTeam( point, TEAM_SURVIVOR ) ) return true;
		}
		const Vector delta = me->GetPosition() - area->GetCenter();
		const float range = z_witch_min_retreat_range.GetFloat();
		if ( range * range > ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z ) return true;
		InfectedPathCost cost( me );
		if ( cost( area, area->GetParent(), NULL, NULL, 1.0f ) < 0.0f ) return true;
		selected = area;
		return false;
	}
	Infected *me;
	TerrorNavArea *selected;
};
}

bool WitchRetreat::ComputePath( Infected *me )
{
	WitchRetreatGoal goal( me );
	CollectSurroundingTerrorAreas( me->GetLastKnownArea(), me->GetPosition(), goal, z_witch_max_retreat_range.GetFloat(), 0, TEAM_ANY );
	if ( !goal.selected ) return false;
	InfectedPathCost cost( me );
	m_path.Compute( me, goal.selected->GetCenter(), cost, 0.0f );
	// S421E60 deliberately ignores Path::Compute's result.
	return true;
}
ActionResult< Infected > WitchRetreat::OnStart( Infected *me, Action< Infected > *prior )
{
	IBody *body = me->GetBodyInterface();
	body->SetArousal( IBody::INTENSE );
	body->SetDesiredPosture( IBody::STAND );
	body->StartActivity( WitchActivity( 685 ), 0 );
	if ( !ComputePath( me ) ) return Done();
	m_started.Start();
	m_lastVisible.Start();
	return Continue();
}
void WitchRetreat::OnEnd( Infected *me, Action< Infected > *next ) {}
ActionResult< Infected > WitchRetreat::Update( Infected *me, float interval )
{
	ILocomotion *move = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	// Native x87 compares the unspilled clock difference; JBE also admits unordered.
	bool remain = !( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_started.GetStartTime() ) > z_witch_retreat_min_duration.GetFloat() );
	if ( !remain )
	{
		if ( IsTerrorSpawnEntityVisibleToTeam( me, TEAM_SURVIVOR ) ) m_lastVisible.Start();
		CTerrorPlayer *nearest = FindClosestSurvivorPlayer( me->WorldSpaceCenter(), true, true );
		remain = nearest && ( !me->IsRangeGreaterThan( nearest, z_witch_retreat_exit_range.GetFloat() ) ||
			!( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_lastVisible.GetStartTime() ) > 1.0 ) ) &&
			!( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_lastVisible.GetStartTime() ) > z_witch_retreat_exit_hidden_duration.GetFloat() );
	}
	if ( !remain )
	{
		UTIL_Remove( me );
		return Done();
	}
	if ( TimerElapsed( m_voiceTimer ) )
	{
		m_voiceTimer.Start( RandomFloat( 1.5f, 2.5f ) );
		me->EmitSound( "WitchZombie.RetreatHorror" );
	}
	if ( body->IsActivity( WitchActivity( 685 ) ) )
	{
		move->Stop();
		me->GetLocomotionInterface()->SetVelocity( vec3_origin );
		me->SetAbsVelocity( vec3_origin );
	}
	else
	{
		m_path.Update( me );
		if ( !move->IsClimbingOrJumping() && !move->IsUsingLadder() && move->IsOnGround() )
		{
			const Activity activity = WitchActivity( body->IsPostureMobile() ? 686 : 608 );
			if ( !body->IsActivity( activity ) ) body->StartActivity( activity, 0 );
		}
	}
	return Continue();
}
EventDesiredResult< Infected > WitchRetreat::OnMoveToSuccess( Infected *me, const Path *path )
{
	return ComputePath( me ) ? TryContinue() : TryDone();
}
EventDesiredResult< Infected > WitchRetreat::OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason )
{
	return ComputePath( me ) ? TryContinue() : TryDone();
}
EventDesiredResult< Infected > WitchRetreat::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == 53 ) AsWitch( me )->Slash( NULL );
	return TryContinue();
}
EventDesiredResult< Infected > WitchRetreat::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	if ( !other ) return TryContinue();
	if ( IsBreakableEntity( other, false, true, NULL ) || other->ClassMatches( "prop_door*" ) || other->ClassMatches( "func_door*" ) )
	{
		if ( ( !trace || trace->plane.normal.z < 0.8f ) && !me->IsPlayingGesture( WitchActivity( 613 ) ) )
			me->AddGesture( WitchActivity( 613 ), true );
	}
	else
	{
		if ( other->IsPlayer() )
		{
			CTerrorPlayer *player = ToTerrorPlayer( other );
			if ( player->IsStaggeringOrPending() ) return TryContinue();
			EHANDLE handle = other;
			if ( m_shovedPlayers.Find( handle ) == m_shovedPlayers.InvalidIndex() )
			{
				player->StaggerPlayer( me, NULL );
				m_shovedPlayers.AddToTail( handle );
				return TryContinue();
			}
		}
		else if ( !other->MyCombatCharacterPointer() ) return TryContinue();
		if ( !me->IsPlayingGesture( WitchActivity( 613 ) ) )
		{
			me->AddGesture( WitchActivity( 613 ), true );
			m_shovedPlayers.RemoveAll();
		}
	}
	return TryContinue();
}

WitchBurn::WitchBurn( CBaseEntity *arsonist ) : m_arsonist( arsonist ) {}
ActionResult< Infected > WitchBurn::OnStart( Infected *me, Action< Infected > *prior )
{
	m_yawDrift = RandomFloat( -40.0f, 40.0f );
	if ( me->IsAlive() && CanWitchReactTo( m_arsonist.Get() ) )
		return SuspendFor( new WitchAttack( m_arsonist.Get() ), "Attacking person who caught me on fire" );
	return Continue();
}
ActionResult< Infected > WitchBurn::Update( Infected *me, float interval )
{
	IBody *body = me->GetBodyInterface();
	if ( !body->IsActivity( WitchActivity( 612 ) ) ) body->StartActivity( WitchActivity( 612 ), 1 );
	QAngle angles = me->GetLocalAngles();
	angles.y += m_yawDrift * interval;
	me->SetLocalAngles( angles );
	return Continue();
}
EventDesiredResult< Infected > WitchBurn::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	if ( other && other->IsPlayer() ) static_cast< CTerrorPlayer * >( other )->StaggerPlayer( me, NULL );
	return TryToSustain();
}
EventDesiredResult< Infected > WitchBurn::OnShoved( Infected *me, CBaseEntity *pusher )
{
	return TrySuspendFor( new InfectedShoved( pusher ), RESULT_TRY );
}
EventDesiredResult< Infected > WitchBurn::OnHitByVomitJar( Infected *me, CBaseEntity *attacker )
{
	return TryToSustain( RESULT_TRY );
}

WitchDying::WitchDying( const CTakeDamageInfo &info ) : InfectedDying( info )
{
	CTerrorPlayer *attacker = dynamic_cast< CTerrorPlayer * >( info.GetAttacker() );
	m_attackerUserID = attacker ? attacker->GetUserID() : -1;
}
bool WitchDying::SelectDeathActivity( Infected *me )
{
	CBaseEntity *attacker = m_info.GetAttacker();
	if ( attacker && attacker->IsPlayer() && attacker->GetTeamNumber() == TEAM_INFECTED &&
		attacker->MyCombatCharacterPointer()->GetZombieClass() == 8 ) return false;
	if ( me->IsOnFire() || ( m_info.GetDamageType() & DMG_BLAST ) ) return false;
	IBody *body = me->GetBodyInterface();
	if ( !body || !body->IsArousal( IBody::INTENSE ) ) return false;
	const Vector velocity = me->GetAbsVelocity();
	Activity activity;
	if ( ( velocity.x * velocity.x + velocity.y * velocity.y ) + velocity.z * velocity.z <= 22500.0f )
	{
		activity = WitchActivity( 667 );
		m_yawDrift = 0.0f;
	}
	else
	{
		activity = WitchActivity( 669 );
		m_yawDrift = RandomFloat( -70.0f, 70.0f );
	}
	if ( !body->StartActivity( activity, 5 ) ) return false;
	m_info.SetDamageForce( vec3_origin );
	return true;
}
ActionResult< Infected > WitchDying::OnStart( Infected *me, Action< Infected > *prior )
{
	if ( me->IsAlive() ) me->Event_Killed( m_info );
	if ( !SelectDeathActivity( me ) ) me->Retire( m_info );
	me->EmitSound( "WitchZombie.Die" );
	return Continue();
}

namespace
{
class WanderingWitchPathCost : public IPathCost
{
public:
	explicit WanderingWitchPathCost( INextBot *me ) : m_move( me->GetLocomotionInterface() ) {}
	virtual float operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
		const CFuncElevator *elevator, float length ) const OVERRIDE
	{
		if ( area->HasAttributes( 0x80000 ) || ToTerrorNavArea( area )->HasSpawnAttributes( TERROR_NAV_NOTHREAT ) ||
			!m_move->IsAreaTraversable( area ) ) return -1.0f;
		if ( !from ) return 0.0f;
		if ( ladder ) return -1.0f;
		double height = from->ComputeAdjacentConnectionHeightChange( area );
		const float heightSpill = static_cast< float >( height );
		if ( fabs( height ) > 16.0 ) return -1.0f;
		if ( !( length > 0.0f ) )
		{
			const Vector delta = area->GetCenter() - from->GetCenter();
			length = sqrtf( ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x );
		}
		float adjusted = static_cast< float >( height );
		if ( heightSpill < 0.0f )
		{
			adjusted = heightSpill * 5.0f;
			height = adjusted;
		}
		const float multiplier = adjusted - 24.0f < 0.0f ? 1.0f : 4.0f;
		const double penalty = fabs( height * multiplier );
		if ( penalty > 3.0 ) length += static_cast< float >( penalty );
		return from->GetCostSoFar() + length;
	}
private:
	ILocomotion *m_move;
};

float WitchTimerRemainingRatio( const CountdownTimer &timer )
{
	float ratio = 0.0f;
	if ( timer.HasStarted() )
	{
		const float remaining = timer.GetRemainingTime();
		ratio = remaining / timer.m_duration.Get();
		if ( ratio < 0.0f ) ratio = 0.0f;
		else if ( ratio > 1.0f ) ratio = 1.0f;
	}
	return ratio;
}

// S422B60/422E00: one collector owns BOTH AI candidates and Music membership.
struct WitchAttentionCollector
{
	explicit WitchAttentionCollector( Witch *me ) : origin( me->GetAbsOrigin() ), nearest( NULL ),
		nearestRangeSquared( 100000000.0f ), anyRangeSquared( 100000000.0f )
	{
		const float range = z_witch_wander_personal_space.GetFloat();
		rangeSquared = range * range;
	}
	void Collect()
	{
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CBasePlayer *base = UTIL_PlayerByIndex( i );
			if ( !base || !base->edict() || !base->entindex() || !base->IsPlayer() ||
				!static_cast< const CBasePlayer * >( base )->IsConnected() || base->GetTeamNumber() != TEAM_SURVIVOR ) continue;
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
			// Native invocation filter32=false: no incap/pin rejection here.
			if ( !player->IsAlive() || ( area && area->HasSpawnAttributes( TERROR_NAV_RESCUE_VEHICLE ) ) )
			{
				other.AddToTail( player );
				continue;
			}
			const Vector delta = player->WorldSpaceCenter() - origin;
			const float distance = ( delta.x * delta.x + delta.y * delta.y ) + delta.z * delta.z;
			if ( anyRangeSquared > distance ) anyRangeSquared = distance;
			trace_t trace;
			bool visible = false;
			if ( rangeSquared > distance )
			{
				NextBotTraceFilterIgnoreActors filter( NULL, COLLISION_GROUP_NONE );
				UTIL_TraceLine( origin, player->WorldSpaceCenter(), 33579073, &filter, &trace );
				visible = trace.fraction >= 1.0f && !trace.startsolid;
			}
			if ( !visible ) other.AddToTail( player );
			else
			{
				nearPlayers.AddToTail( player );
				if ( nearestRangeSquared > distance )
				{
					nearestRangeSquared = distance;
					nearest = player;
				}
			}
		}
	}
	float rangeSquared;
	Vector origin;
	CTerrorPlayer *nearest;
	float nearestRangeSquared;
	float anyRangeSquared;
	CUtlVector< CTerrorPlayer * > nearPlayers;
	CUtlVector< CTerrorPlayer * > other;
};

struct CollectWitchWanderAreas
{
	bool operator()( TerrorNavArea *area ) { areas.AddToTail( area ); return true; }
	CUtlVectorFixedGrowable< TerrorNavArea *, 1000 > areas;
};

TerrorNavArea *SelectWitchWanderArea( Infected *me )
{
	CollectWitchWanderAreas collect;
	CollectSurroundingTerrorAreas( me->GetLastKnownArea(), me->GetAbsOrigin(), collect, 960.0f, 0, TEAM_ANY );
	const int count = collect.areas.Count();
	for ( int i = 0; i < count; ++i )
	{
		const int selected = RandomInt( 0, count - 1 );
		V_swap( collect.areas[selected], collect.areas[i] );
	}
	FOR_EACH_VEC( collect.areas, i )
	{
		TerrorNavArea *area = collect.areas[i];
		if ( area && area != me->GetLastKnownArea() && !area->IsDamaging() &&
			area->GetSizeX() >= 36.0f && area->GetSizeY() >= 36.0f &&
			fabsf( area->GetCenter().z - me->GetAbsOrigin().z ) <= 120.0f &&
			!area->HasSpawnAttributes( TERROR_NAV_NOTHREAT ) ) return area;
	}
	return NULL;
}

void PlayWanderingWitchCue( Witch *me )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( !base || !base->edict() || !base->entindex() || !base->IsPlayer() ||
			!static_cast< const CBasePlayer * >( base )->IsConnected() ) continue;
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
		if ( player->IsAlive() && ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
			!player->GetMusic()->IsWitchChoirPlaying() )
			player->GetMusic()->Play( "Event.WanderingWitch", me->entindex(), -1.0f, false, true );
	}
}
}

WitchWander::WitchWander() : m_pendingVictim( NULL ) {}
ActionResult< Infected > WitchWander::OnStart( Infected *me, Action< Infected > *prior )
{
	IBody *body = me->GetBodyInterface();
	body->SetArousal( IBody::NEUTRAL );
	body->SetDesiredPosture( IBody::STAND );
	m_voiceTimer.Start( 2.0f );
	m_moveTimer.Start( RandomFloat( 0.0f, 1.0f ) );
	m_musicTimer.Start();
	m_attentionTimer.Invalidate();
	m_failedPaths = 0;
	m_pendingVictim = NULL;
	body->StartActivity( WitchActivity( 690 ), 0 );
	me->GetLocomotionInterface()->Stop();
	me->SetInfectedFlags( 0x200 );
	return Continue();
}
void WitchWander::OnEnd( Infected *me, Action< Infected > *next )
{
	me->GetLocomotionInterface()->SetSpeedLimit( z_witch_speed.GetFloat() );
}
ActionResult< Infected > WitchWander::OnSuspend( Infected *me, Action< Infected > *interrupt )
{
	me->GetLocomotionInterface()->SetSpeedLimit( z_witch_speed.GetFloat() );
	return Continue();
}
ActionResult< Infected > WitchWander::OnResume( Infected *me, Action< Infected > *interrupt )
{
	me->SetInfectedFlags( 0x200 );
	m_path.Invalidate();
	m_moveTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	return Continue();
}
bool WitchWander::ComputePath( Infected *me )
{
	TerrorNavArea *area = SelectWitchWanderArea( me );
	if ( area )
	{
		WanderingWitchPathCost cost( me );
		const float x = RandomFloat( area->GetSizeX() * -0.25f, area->GetSizeX() * 0.25f );
		const float y = RandomFloat( area->GetSizeY() * -0.25f, area->GetSizeY() * 0.25f );
		const Vector goal = area->GetCenter() + Vector( x, y, 0.0f );
		if ( m_path.Compute( me, goal, cost, 0.0f ) )
		{
			m_failedPaths = 0;
			return true;
		}
	}
	++m_failedPaths;
	m_path.Invalidate();
	if ( m_failedPaths > 10 ) Msg( "Witch %d failing excessively!\n", me->entindex() );
	return false;
}
ActionResult< Infected > WitchWander::Update( Infected *actor, float interval )
{
	static CountdownTimer musicCooldown;
	Witch *me = AsWitch( actor );
	ILocomotion *move = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	if ( m_pendingVictim.IsValid() )
	{
		if ( body->IsActivity( WitchActivity( 691 ) ) )
		{
			if ( me->IsSequenceFinished() && IsWitchVictim( m_pendingVictim.Get() ) )
			{
				me->ClearInfectedFlags( 0x200 );
				return SuspendFor( new WitchAttack( m_pendingVictim.Get() ), "Attacking Survivor that stayed close for too long" );
			}
			return Continue();
		}
		if ( body->StartActivity( WitchActivity( 691 ), 0 ) )
		{
			m_path.Invalidate();
			move->Stop();
			move->Reset();
			me->SetAbsVelocity( vec3_origin );
			me->EmitSound( "WitchZombie.Scream" );
			return Continue();
		}
	}
	WitchAttentionCollector attention( me );
	attention.Collect();
	const bool hasNearbyPlayer = attention.nearest && z_witch_wander_personal_space.GetFloat() >= me->GetRangeTo( attention.nearest );
	if ( hasNearbyPlayer )
	{
		if ( m_attentionTimer.HasStarted() )
		{
			if ( TimerElapsed( m_attentionTimer ) ) m_pendingVictim = attention.nearest;
			else
			{
				body->AimHeadTowards( attention.nearest, IBody::CRITICAL, 0.1f, NULL, NULL, true, 0.25f );
				me->SetWanderRage( 1.0f - WitchTimerRemainingRatio( m_attentionTimer ) );
				if ( TimerElapsed( m_voiceTimer ) )
				{
					const float remaining = WitchTimerRemainingRatio( m_attentionTimer );
					me->EmitSound( remaining >= 0.5f ? "WitchZombie.GrowlLow" : remaining >= 0.25f ? "WitchZombie.GrowlMedium" : "WitchZombie.GrowlHigh" );
					m_voiceTimer.Start( RandomFloat( 1.0f, 2.0f ) );
				}
			}
		}
		else m_attentionTimer.Start( z_witch_wander_personal_time.GetFloat() );
		me->SetWitchAttentionRemainingRatio( WitchTimerRemainingRatio( m_attentionTimer ) );
	}
	else
	{
		m_attentionTimer.Invalidate();
		float rage = me->GetWanderRage();
		if ( rage > 0.0f ) rage -= 0.1f;
		if ( rage < 0.0f ) rage = 0.0f;
		me->SetWanderRage( rage );
		me->SetWitchAttentionRemainingRatio( 1.0f );
		if ( TimerElapsed( m_voiceTimer ) )
		{
			me->EmitSound( "WanderWitchZombie.Despair" );
			const float duration = RandomFloat( 4.0f, 6.0f );
			m_voiceTimer.Start( duration );
			CSoundEnt::InsertSound( 8, me->GetAbsOrigin(), 2048, duration, me, 10 );
		}
		const float minimum = z_witch_wander_music_min_dist.GetFloat();
		const float maximum = z_witch_wander_music_max_dist.GetFloat();
		float cueInterval;
		if ( minimum == maximum )
			cueInterval = attention.anyRangeSquared < maximum ? z_witch_wander_music_min_interval.GetFloat() : z_witch_wander_music_max_interval.GetFloat();
		else
			cueInterval = ( ( attention.anyRangeSquared - minimum ) *
				( z_witch_wander_music_max_interval.GetFloat() - z_witch_wander_music_min_interval.GetFloat() ) ) /
				( maximum - minimum ) + z_witch_wander_music_min_interval.GetFloat();
		if ( static_cast< double >( gpGlobals->curtime ) - static_cast< double >( m_musicTimer.GetStartTime() ) > cueInterval && TimerElapsed( musicCooldown ) && !m_attentionTimer.HasStarted() )
		{
			PlayWanderingWitchCue( me );
			m_musicTimer.Start();
			musicCooldown.Start( RandomFloat( 0.25f, 1.0f ) );
		}
	}
	// S42433B..424447: the SAME collector vectors, add first then ordered remove.
	FOR_EACH_VEC( attention.nearPlayers, i )
		if ( attention.nearPlayers[i] ) attention.nearPlayers[i]->GetMusic()->AddWanderingWitch( me );
	FOR_EACH_VEC( attention.other, i )
		if ( attention.other[i] ) attention.other[i]->GetMusic()->RemoveWanderingWitch( me );

	if ( m_path.IsValid() )
	{
		m_path.Update( me );
		if ( !move->IsClimbingOrJumping() && !move->IsUsingLadder() && move->IsOnGround() )
		{
			const Vector &position = me->GetAbsOrigin();
			if ( m_lastOrigin == position )
			{
				m_path.Invalidate();
				m_moveTimer.Start( RandomFloat( 1.0f, 2.0f ) );
			}
			else m_lastOrigin = position;
			if ( body->IsActivity( WitchActivity( 689 ) ) ) return Continue();
			body->StartActivity( WitchActivity( 689 ), 0 );
			move->SetSpeedLimit( 35.0f );
		}
	}
	else
	{
		if ( !body->IsActivity( WitchActivity( 690 ) ) )
		{
			body->StartActivity( WitchActivity( 690 ), 0 );
			move->Stop();
			me->SetAbsVelocity( vec3_origin );
		}
		if ( !TimerElapsed( m_moveTimer ) ) return Continue();
		if ( !ComputePath( me ) )
		{
			m_moveTimer.Start( static_cast< float >( RandomInt( 3, 6 ) ) );
			return Continue();
		}
	}
	m_lastOrigin = vec3_origin;
	return Continue();
}
EventDesiredResult< Infected > WitchWander::OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	if ( source && source->IsPlayer() && source->IsAlive() &&
		( source->GetTeamNumber() == TEAM_SURVIVOR || source->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
	{
		const float range = z_witch_wander_hear_radius.GetFloat();
		if ( static_cast< double >( range ) * static_cast< double >( range ) >= me->GetRangeSquaredTo( source ) && me->IsLineOfSightClear( source->EyePosition() ) &&
			static_cast< CTerrorPlayer * >( source )->GetTimeSinceWeaponFired() < 0.2 )
			m_pendingVictim = source;
	}
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
	CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
	if ( attacker && ( attacker->GetTeamNumber() == TEAM_SURVIVOR || attacker->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) &&
		!IsInCommentaryMode() && !attacker->IsBot() ) m_pendingVictim = attacker;
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnHitByVomitJar( Infected *me, CBaseEntity *attacker )
{
	AsWitch( me )->ChangeRage( 1.0f );
	if ( CanWitchReactTo( attacker ) ) return TrySuspendFor( new WitchAttack( attacker ), RESULT_IMPORTANT, "Vomit thrown on me" );
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	if ( AsWitch( me )->GetRage() < 1.0f ) return TryContinue();
	return TrySuspendFor( new WitchAttack( victim ), RESULT_IMPORTANT, "I was commanded to attack" );
}
EventDesiredResult< Infected > WitchWander::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == 53 ) AsWitch( me )->Slash( NULL );
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnMoveToSuccess( Infected *me, const Path *path )
{
	m_moveTimer.Start( RandomFloat( 6.0f, 10.0f ) );
	m_path.Invalidate();
	me->GetLocomotionInterface()->Stop();
	me->GetBodyInterface()->StartActivity( WitchActivity( 690 ), 0 );
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnMoveToFailure( Infected *me, const Path *path, MoveToFailureType reason )
{
	if ( !ComputePath( me ) )
	{
		m_path.Invalidate();
		me->GetLocomotionInterface()->Stop();
		me->GetBodyInterface()->StartActivity( WitchActivity( 690 ), 0 );
		m_moveTimer.Start( RandomFloat( 3.0f, 6.0f ) );
	}
	return TryContinue();
}
EventDesiredResult< Infected > WitchWander::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	if ( other )
	{
		if ( other->IsPlayer() && ( other->GetTeamNumber() == TEAM_SURVIVOR || other->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
			m_pendingVictim = other;
		if ( IsBreakableEntity( other, false, true, NULL ) || other->ClassMatches( "prop_door*" ) || other->ClassMatches( "func_door*" ) )
			ComputePath( me );
	}
	return TryContinue();
}

#pragma float_control( pop )
