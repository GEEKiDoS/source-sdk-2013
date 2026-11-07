//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/player/bots/survivor_intention.h"
#include "l4d2/player/bots/survivor_behavior.h"
#include "l4d2/player/bots/survivor_legs.h"
#include "l4d2/player/bots/survivor_team_situation.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/infected/special/ability_vomit.h"
#include "l4d2/infected/special/ability_spit.h"
#include "l4d2/infected/special/ability_tongue.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_scenario.h"
#include "l4d2/nav/terror_nav_area.h"
#include "NextBot/NextBotBodyInterface.h"
#include "NextBot/NextBotLocomotionInterface.h"
#include "NextBot/NextBotVisionInterface.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "NextBot/NextBotUtil.h"
#include "l4d2/player/bots/survivor_intention_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar bot_mimic;
extern ConVar z_exploding_outer_radius;
extern ConVar z_jockey_leap_range;
extern ConVar tongue_range;
extern bool IsInCommentaryMode();
ConVar sb_hold_position( "sb_hold_position", "0", 0x00004004, "Force SurvivorBots to stand still" );
ConVar sb_l4d1_survivor_behavior( "sb_l4d1_survivor_behavior", "1", 0x00004004 );
ConVar sb_threat_medium_range( "sb_threat_medium_range", "300", 0x00004004, "Too close for comfort, even when neutral" );
ConVar sb_threat_far_range( "sb_threat_far_range", "600", 0x00004004, "Close enough to be a threat if near several other threats" );
ConVar sb_threat_very_far_range( "sb_threat_very_far_range", "1500", 0x00004004, "Too far to be a threat, even for boss infected" );
ConVar sb_far_hearing_range( "sb_far_hearing_range", "1500", 0x00004004 );
ConVar sb_near_hearing_range( "sb_near_hearing_range", "500", 0x00004004 );

SurvivorKnownEntity::SurvivorKnownEntity( CBaseCombatCharacter *entity )
	: m_entity( entity ), m_lastArea( entity->GetLastKnownArea() ), m_actorID( -1 ), m_visible( false )
{
	m_lastPosition = ( entity->EyePosition() + entity->WorldSpaceCenter() ) * 0.5f;
	m_lastSeen.Invalidate();
	m_lastHeard.Start();
}

CBaseCombatCharacter *SurvivorKnownEntity::GetEntity() const { return m_entity.Get(); }

void SurvivorKnownEntity::UpdateHeardPosition()
{
	CBaseCombatCharacter *entity = m_entity.Get();
	// S39FD40 only tracks a grounded actor. It does not refresh the hearing
	// clock and it does not reveal the current position of an airborne actor.
	if ( entity && entity->GetGroundEntity() )
	{
		m_lastPosition = ( entity->EyePosition() + entity->WorldSpaceCenter() ) * 0.5f;
		m_lastArea = entity->GetLastKnownArea();
	}
}

void SurvivorKnownEntity::MarkSeen()
{
	CBaseCombatCharacter *entity = m_entity.Get();
	if ( !entity ) return;
	UpdateHeardPosition();
	if ( entity->GetGroundEntity() )
	{
		CTerrorPlayer *hunter = ToTerrorPlayer( entity );
		if ( hunter && hunter->GetTeamNumber() == TEAM_INFECTED && hunter->GetZombieClass() == 3 && ( hunter->m_Local.m_bDucked || hunter->GetPounceVictim() ) ) m_lastPosition = hunter->WorldSpaceCenter();
	}
	m_lastSeen.Start();
	m_visible = true;
}

SurvivorIntention::SurvivorIntention( SurvivorBot *bot ) : IIntention( bot ), m_actor( NULL ), m_behavior( NULL ), m_currentLegsBehavior( 0 )
{
	// The actor constructor resets the complete INextBot after all four
	// components are constructed; constructing intention must not draw RNG.
	m_legsBehavior[0] = NULL;
}

SurvivorIntention::~SurvivorIntention()
{
	delete m_behavior;
	ClearLegsActions();
}

void SurvivorIntention::ClearLegsActions()
{
	for ( ; m_currentLegsBehavior >= 0; --m_currentLegsBehavior ) delete m_legsBehavior[m_currentLegsBehavior];
	m_legsBehavior[0] = NULL;
	m_currentLegsBehavior = 0;
}

void SurvivorIntention::ResetLegsAction( Action< SurvivorBot > *action )
{
	ClearLegsActions();
	m_legsBehavior[0] = new Behavior< SurvivorBot >( action );
}

void SurvivorIntention::Reset()
{
	m_knownThreats.RemoveAll();
	m_pendingSounds.RemoveAll();
	m_actor = static_cast< SurvivorBot * >( GetBot()->GetEntity() );
	delete m_behavior;
	const bool l4d1 = m_actor->GetTeamNumber() == 4 && sb_l4d1_survivor_behavior.GetBool();
	if ( l4d1 )
	{
		m_behavior = new Behavior< SurvivorBot >( new L4D1SurvivorBehavior );
		ResetLegsAction( new L4D1SurvivorLegsBattleStations );
	}
	else
	{
		m_behavior = new Behavior< SurvivorBot >( new SurvivorBehavior );
		ResetLegsAction( new SurvivorLegsStayClose );
	}
	IIntention::Reset();
}

void SurvivorIntention::PushLegsAction( Action< SurvivorBot > *action )
{
	if ( m_currentLegsBehavior >= 15 ) { delete action; return; }
	m_legsBehavior[++m_currentLegsBehavior] = new Behavior< SurvivorBot >( action );
}

void SurvivorIntention::PopLegsAction()
{
	if ( m_currentLegsBehavior <= 0 ) return;
	delete m_legsBehavior[m_currentLegsBehavior--];
	if ( m_legsBehavior[m_currentLegsBehavior] ) m_legsBehavior[m_currentLegsBehavior]->Resume( m_actor );
}

void SurvivorIntention::ChangeLegsAction( Action< SurvivorBot > *action )
{
	// S3A9DE0 allocates before destroying the old behavior.
	Behavior< SurvivorBot > *replacement = new Behavior< SurvivorBot >( action );
	delete m_legsBehavior[m_currentLegsBehavior];
	m_legsBehavior[m_currentLegsBehavior] = replacement;
}

INextBotEventResponder *SurvivorIntention::FirstContainedResponder() const { return m_behavior; }
INextBotEventResponder *SurvivorIntention::NextContainedResponder( INextBotEventResponder *current ) const
{
	return current == m_behavior ? m_legsBehavior[m_currentLegsBehavior] : NULL;
}

void SurvivorIntention::Update()
{
	VPROF_BUDGET( "SurvivorIntention::Update", "NextBotExpensive" );
	if ( bot_mimic.GetBool() ) return;
	if ( m_behavior ) m_behavior->Update( m_actor, GetUpdateInterval() );
	if ( m_legsBehavior[m_currentLegsBehavior] && !sb_hold_position.GetBool() ) m_legsBehavior[m_currentLegsBehavior]->Update( m_actor, GetUpdateInterval() );
	UpdateKnownThreats();
}

SurvivorKnownEntity *SurvivorIntention::FindKnownThreat( CBaseCombatCharacter *entity )
{
	for ( int i = 0; i < m_knownThreats.Count(); ++i )
		if ( m_knownThreats[i].GetEntity() && m_knownThreats[i].GetEntity()->entindex() == entity->entindex() ) return &m_knownThreats[i];
	return NULL;
}

const SurvivorKnownEntity *SurvivorIntention::GetPrimaryKnownThreat() const
{
	return m_knownThreats.Count() > 0 && m_knownThreats[0].GetEntity() ? &m_knownThreats[0] : NULL;
}

// S3A21B0, used by behavior injury/concept reports. Unlike sight and delayed
// hearing, this producer deliberately does not look for an existing record.
void SurvivorIntention::AddKnownThreat( CBaseCombatCharacter *entity )
{
	SurvivorKnownEntity record( entity );
	record.UpdateHeardPosition();
	m_knownThreats.AddToTail( record );
}

void SurvivorIntention::OnInjured( const CTakeDamageInfo &info )
{
	CBaseEntity *attacker = info.GetAttacker();
	CBaseCombatCharacter *actor = attacker ? attacker->MyCombatCharacterPointer() : NULL;
	if ( actor && actor->GetTeamNumber() == TEAM_INFECTED )
	{
		SurvivorKnownEntity *known = FindKnownThreat( actor );
		if ( known ) known->MarkSeen();
		else { SurvivorKnownEntity record( actor ); record.MarkSeen(); m_knownThreats.AddToTail( record ); }
	}
	IIntention::OnInjured( info );
}

void SurvivorIntention::OnSound( CBaseEntity *source, const Vector &position, KeyValues *keys )
{
	CBaseCombatCharacter *actor = source ? source->MyCombatCharacterPointer() : NULL;
	CTerrorPlayer *player = actor ? ToTerrorPlayer( actor ) : NULL;
	if ( actor && !TerrorGameRules()->IsPlayerControlledZombies() && actor->GetTeamNumber() == TEAM_INFECTED && ( !player || !player->IsGhost() ) && GetBot()->IsRangeLessThan( source, sb_far_hearing_range.GetFloat() ) )
	{
		float chance = 100.0f;
		if ( !GetBot()->IsRangeLessThan( source, sb_near_hearing_range.GetFloat() ) ) chance = ( 1.0f - ( GetBot()->GetRangeTo( actor ) - sb_near_hearing_range.GetFloat() ) / ( sb_far_hearing_range.GetFloat() - sb_near_hearing_range.GetFloat() ) ) * 100.0f;
		if ( chance > RandomFloat( 0.0f, 100.0f ) )
		{
			bool pending = false;
			for ( int i = 0; i < m_pendingSounds.Count(); ++i ) if ( m_pendingSounds[i].entity.Get() && m_pendingSounds[i].entity->entindex() == actor->entindex() ) { pending = true; break; }
			if ( !pending ) { PendingSound sound; sound.entity = actor; sound.recognizeTime = gpGlobals->curtime + RandomFloat( 0.3f, 1.0f ); m_pendingSounds.AddToTail( sound ); }
		}
	}
	IIntention::OnSound( source, position, keys );
}

class SurvivorVisibleThreatCollector : public IVision::IForEachKnownEntity
{
public:
	SurvivorVisibleThreatCollector( SurvivorIntention *intention, INextBot *bot ) : m_intention( intention ), m_bot( bot ) {}
	virtual bool Inspect( const VisionKnownEntity &known ) OVERRIDE
	{
		CBaseCombatCharacter *actor = known.entity.Get()->MyCombatCharacterPointer();
		if ( !actor || !actor->IsAlive() || !m_bot->IsEnemy( actor ) ) return true;
		SurvivorKnownEntity *record = m_intention->FindKnownThreat( actor );
		if ( record ) record->MarkSeen();
		else { SurvivorKnownEntity created( actor ); created.MarkSeen(); m_records.AddToTail( created ); }
		return true;
	}
	CUtlVector< SurvivorKnownEntity > m_records;
private:
	SurvivorIntention *m_intention;
	INextBot *m_bot;
};

void SurvivorIntention::UpdateKnownThreats()
{
	for ( int i = 0; i < m_knownThreats.Count(); ++i )
	{
		SurvivorKnownEntity &record = m_knownThreats[i];
		record.m_visible = false;
		CBaseCombatCharacter *actor = record.GetEntity();
		CTerrorPlayer *player = actor ? ToTerrorPlayer( actor ) : NULL;
		const bool obsolete = record.m_lastSeen.GetStartTime() > 0.0f ? record.GetTimeSinceLastSeen() > 30.0f : record.m_lastHeard.GetStartTime() <= 0.0f || record.GetTimeSinceLastHeard() > 5.0f;
		if ( !actor || !actor->IsAlive() || ( player && player->IsGhost() ) || obsolete ) m_knownThreats.Remove( i-- );
	}
	SurvivorVisibleThreatCollector collector( this, GetBot() );
	GetBot()->GetVisionInterface()->ForEachKnownEntity( collector );
	m_knownThreats.AddVectorToTail( collector.m_records );
	for ( int i = 0; i < m_pendingSounds.Count(); ++i )
	{
		CBaseCombatCharacter *actor = m_pendingSounds[i].entity.Get();
		if ( actor && gpGlobals->curtime >= m_pendingSounds[i].recognizeTime )
		{
			SurvivorKnownEntity *record = FindKnownThreat( actor );
			if ( record ) record->UpdateHeardPosition();
			else { SurvivorKnownEntity created( actor ); created.UpdateHeardPosition(); m_knownThreats.AddToTail( created ); }
			m_pendingSounds.Remove( i-- );
		}
	}
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( player && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR && player->IsAlive() && player->IsDominated() )
		{
			// Native appends unconditionally, including duplicate attackers.
			SurvivorKnownEntity created( static_cast< CBaseCombatCharacter * >( player->GetDominatingAttacker() ) );
			created.UpdateHeardPosition();
			m_knownThreats.AddToTail( created );
		}
	}
	for ( int i = 1; i < m_knownThreats.Count(); ++i )
	{
		SurvivorKnownEntity record = m_knownThreats[i];
		int j = i - 1;
		for ( ; j >= 0; --j )
		{
			CBaseEntity *winner = SelectMoreDangerousThreat( GetBot(), m_actor, record.GetEntity(), m_knownThreats[j].GetEntity() );
			if ( winner && winner->entindex() == m_knownThreats[j].GetEntity()->entindex() ) break;
			m_knownThreats[j + 1] = m_knownThreats[j];
		}
		m_knownThreats[j + 1] = record;
	}
	if ( GetBot()->IsDebugging( NEXTBOT_VISION ) )
	{
		for ( int i = 0; i < m_knownThreats.Count(); ++i )
		{
			CBaseCombatCharacter *entity = m_knownThreats[i].GetEntity();
			if ( !entity ) continue;
			const int red = i < 3 ? 255 : 0, green = i == 0 ? 0 : ( i == 1 ? 100 : 255 );
			NDebugOverlay::HorzArrow( entity->GetAbsOrigin(), entity->GetAbsOrigin() + Vector( -50, 0, 0 ), 10, red, green, 0, 255, true, 0.01023f );
			NDebugOverlay::Text( entity->GetAbsOrigin(), CFmtStr( m_knownThreats[i].IsVisibleNow() ? "%d" : "(%d)", i ), false, 0.01023f );
		}
	}
}

QueryResultType SurvivorIntention::ShouldHurry( const INextBot *me ) const
{
	if ( ( TheDirector->IsFinale() && TheDirector->IsRescueVehicleReady() ) || TheDirector->GetTankCount() > 0 || TheDirector->GetScenario()->IsPanicEventActive() || ( TheDirector->GetScriptedMode()->IsEnabled() && TheDirector->GetDirectorOptionInt( "cm_ShouldHurry", 0 ) != 0 ) || m_actor->GetTeamSituation().IsTeamNearCheckpoint() || m_actor->IsRespondingToSurvivorDistress() ) return ANSWER_YES;
	return IIntention::ShouldHurry( me );
}

QueryResultType SurvivorIntention::IsPositionAllowed( const INextBot *me, const Vector &position ) const
{
	ILocomotion *locomotion = me->GetLocomotionInterface();
	if ( locomotion->IsClimbingOrJumping() || m_actor->IsJockeyVictim() ) return ANSWER_YES;
	const Path *path = GetCurrentPath( me );
	if ( path && path->IsValid() )
	{
		const Path::Segment *goal = path->GetCurrentGoal();
		if ( goal && ( goal->type == Path::DROP_DOWN || locomotion->GetFeet().z - goal->pos.z >= locomotion->GetMaxJumpHeight() ) ) return ANSWER_YES;
	}
	NextBotTraceFilterIgnoreActors filter( m_actor, COLLISION_GROUP_PLAYER_MOVEMENT );
	IBody *body = me->GetBodyInterface();
	const float width = body->GetHullWidth() * 0.4f;
	const Vector mins( -width, -width, 0.0f ), maxs( width, width, body->GetHullHeight() );
	Vector top = position, bottom = position;
	top.z += locomotion->GetStepHeight(); bottom.z -= locomotion->GetMaxJumpHeight();
	trace_t trace;
	UTIL_TraceHull( top, bottom, mins, maxs, body->GetSolidMask(), &filter, &trace );
	return trace.fraction < 1.0f || trace.startsolid || trace.allsolid || IIntention::IsPositionAllowed( me, position ) == ANSWER_YES ? ANSWER_YES : ANSWER_NO;
}

bool SurvivorIntention::IsThreatDangerous( CTerrorPlayer *subject, CBaseEntity *threat ) const
{
	if ( !threat || !threat->IsAlive() ) return false;
	const int zombieClass = GetTerrorEntityZombieClass( threat );
	if ( zombieClass == 0 )
	{
		if ( TheDirector->GetActivePipeBombCount() > 0 ) return false;
		const float range = ( threat->GetAbsOrigin() - subject->GetAbsOrigin() ).Length();
		if ( subject != m_actor && ( TheDirector->GetTankCount() > 0 || m_actor->IsRespondingToSurvivorDistress() ) && range <= 48.0f && ( subject->IsIncapacitated() || subject->IsDominated() ) ) return true;
		Infected *infected = dynamic_cast< Infected * >( threat );
		if ( ( infected->GetInfectedFlags() & 0x20 ) && !infected->IsShoved() && infected->IsInFieldOfView( m_actor ) && range > 48.0f ) return false;
		INextBot *bot = infected->MyNextBotPointer();
		if ( bot && bot->GetBodyInterface()->IsArousal( IBody::NEUTRAL ) && range >= sb_threat_medium_range.GetFloat() )
		{
			if ( range >= sb_threat_far_range.GetFloat() ) return false;
			int neighbors = 0;
			for ( int i = 0; i < m_knownThreats.Count() && neighbors < 3; ++i )
			{
				CBaseCombatCharacter *other = m_knownThreats[i].GetEntity();
				if ( other != threat && ( !other || ( threat->GetAbsOrigin() - other->GetAbsOrigin() ).Length() < 250.0f ) ) ++neighbors;
			}
			if ( neighbors < 3 ) return false;
		}
		return subject->IsLineOfSightClear( threat, CBaseCombatCharacter::IGNORE_ACTORS );
	}
	if ( zombieClass >= 1 && zombieClass <= 6 )
	{
		if ( IsInCommentaryMode() && !TheDirector->IsFinale() && ( subject->IsBot() || ( !subject->IsIncapacitated() && !subject->IsIT() && !subject->IsTongueVictim() && !subject->IsBeingPounced() ) ) ) return false;
	}
	if ( zombieClass >= 1 && zombieClass <= 7 ) return ( threat->GetAbsOrigin() - subject->GetAbsOrigin() ).Length() < sb_threat_very_far_range.GetFloat();
	return zombieClass == 8;
}

bool SurvivorIntention::IsImmediateThreat( CTerrorPlayer *subject, CBaseEntity *threat ) const
{
	if ( !threat || !threat->IsAlive() ) return false;
	const int zombieClass = GetTerrorEntityZombieClass( threat );
	const float rangeSquared = ( subject->GetAbsOrigin() - threat->GetAbsOrigin() ).LengthSqr();
	if ( zombieClass == 0 )
	{
		if ( ( TheDirector->GetTankCount() > 0 || subject->IsRespondingToSurvivorDistress() ) && rangeSquared > 1296.0f ) return false;
		if ( rangeSquared > 10000.0f ) return false;
		INextBot *bot = threat->MyNextBotPointer();
		return bot && bot->GetBodyInterface()->IsArousal( IBody::INTENSE ) && bot->GetVisionInterface()->IsLookingAt( subject, 0.5f ) && subject->IsLineOfSightClear( threat, CBaseCombatCharacter::IGNORE_ACTORS );
	}
	CTerrorPlayer *player = ToTerrorPlayer( threat );
	if ( zombieClass == 7 )
	{
		INextBot *bot = threat->MyNextBotPointer();
		if ( !bot ) return true;
		if ( bot->GetBodyInterface()->IsArousal( IBody::NEUTRAL ) ) return false;
		return !bot->GetBodyInterface()->IsArousal( IBody::ALERT ) || bot->GetVisionInterface()->IsLookingAt( subject, 0.70709997f );
	}
	if ( zombieClass == 1 )
	{
		if ( subject->GetTongueOwner() ) return true;
		if ( !player || !player->IsAlive() ) return false;
		if ( player->GetTongueVictim() ) return player->GetTongueVictim()->entindex() == subject->entindex();
		CBaseAbility *ability = player->GetCustomAbility();
		if ( !ability ) return false;
		if ( ( ability->CanActivate() || ability->IsActive() ) && rangeSquared < Square( tongue_range.GetFloat() * 1.25f ) && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS ) ) return true;
		return rangeSquared < Square( sb_threat_medium_range.GetFloat() ) && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS );
	}
	if ( zombieClass == 2 )
	{
		if ( !player || !player->GetCustomAbility() ) return false;
		const float range = sqrtf( rangeSquared );
		if ( range < z_exploding_outer_radius.GetFloat() * 1.2f && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS ) ) return true;
		CBaseAbility *ability = player->GetCustomAbility();
		return ( ability->CanActivate() || ability->IsActive() ) && range < z_vomit_range.GetFloat() * 2.0f && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS );
	}
	if ( zombieClass >= 3 && zombieClass <= 6 )
	{
		if ( !player ) return false;
		CTerrorPlayer *victim = zombieClass == 3 ? player->GetPounceVictim() : ( zombieClass == 5 ? player->GetJockeyVictim() : ( zombieClass == 6 ? player->GetPummelVictim() : NULL ) );
		if ( victim ) return victim->entindex() == subject->entindex();
		if ( zombieClass == 3 && player->IsAttemptingToPounce() )
		{
			Vector toward = subject->GetAbsOrigin() - player->GetAbsOrigin(), velocity = player->GetAbsVelocity();
			toward.NormalizeInPlace(); velocity.NormalizeInPlace();
			if ( DotProduct( toward, velocity ) > 0.8f ) return true;
		}
		const float range = zombieClass == 4 ? z_spit_range.GetFloat() : ( zombieClass == 5 ? z_jockey_leap_range.GetFloat() : sb_threat_medium_range.GetFloat() );
		return rangeSquared < range * range && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS );
	}
	if ( zombieClass == 8 )
	{
		CBaseAbility *ability = player->GetCustomAbility();
		if ( ability && ability->IsActive() && subject->IsLineOfSightClear( player, CBaseCombatCharacter::IGNORE_ACTORS ) ) return true;
		if ( rangeSquared < 90000.0f ) return true;
		if ( !player->IsLookingTowards( subject, 0.7f ) ) return false;
		CNavArea *area = player->GetLastKnownArea();
		return rangeSquared <= 562500.0f && area && ToTerrorNavArea( area )->IsPotentiallyVisibleByCharacter( subject->GetSurvivorCharacter() );
	}
	return false;
}


