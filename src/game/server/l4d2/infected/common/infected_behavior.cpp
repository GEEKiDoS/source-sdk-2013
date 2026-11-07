//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/common/infected_behavior.h"
#include "l4d2/infected/common/infected_path_cost.h"
#include "NextBot/NextBotManager.h"
#include "l4d2/infected/infected_activities.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/terror_shareddefs.h"
#include "NextBot/NextBotKnownEntity.h"
#include "NextBot/NextBotUtil.h"
#include "recipientfilter.h"
#include "KeyValues.h"
#include "npcevent.h"
#include "activitylist.h"
#include <float.h>

#include "NextBot/Path/NextBotPathFollow.h"
#include "nav_ladder.h"
#include "l4d2/nav/escape_route.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/nav/terror_nav_area.h"
#include "l4d2/infected/boss/witch.h"
#include "particle_parse.h"
#include "util_shared.h"
#include "l4d2/infected/common/infected_action_helpers.h"
#include "l4d2/infected/common/zombiebot_body.h"
#include "BasePropDoor.h"
#include "doors.h"
#include "collisionproperty.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/zombiemanager.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/vscript/terror_script_globals.h"
#include "l4d2/infected/common/infected_behavior_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// Pinned app550 server RVAs (image base 10000000), not target enum values:
// Intention root 416670; Behavior ctor 4148C0, initial child 4141D0,
// killed 414250, injured 414EF0, shoved 414440. 414940 is its destructor.
// Wander ctor 419BA0, start 4197C0, update 419D60, initial 41AC50,
// recognition 419A60, contact 41A1E0, injury 41A050, sound 41A770, shove 41A4B0.
// StandDazed 4193A0/4195D0; SitDown 418150/4182D0; LieDown 417EF0/417C80;
// ChangePosture 40E0E0/40E120; StandingActivity 4188B0/418900/418970;
// StaggerAround 418540/4185C0/419100/4192A0; LeanOnWall 418AC0/418FD0.
// Alert start 40EE30, update 407350, orient 4070E0, resume 4078B0,
// completion 407990, sound 407C60, shove 40F040.
// Shoved ctor 40E280, start 40EAD0, direction 40E3F0, completion 40E800,
// contact 40E910, repeated shove 40E8C0.
// Attack ctor 412220, start 412370, update 411100, initial 412340,
// sound 411470, shove 413AD0, punch 413BD0, punch LOS 4108D0.
// Chase start 412810, update 4128C0, contact 411AF0, event 4140E0;
// Punch start 410F30, update 413430, event 414150. LostVictim 410D30/410E30.
// Dying ctor 415C40, start 416490, chooser 415D80, update 415A00,
// completion 415A60, optional end 415AC0, contact 415B10, shove 415BD0.
// 415BD0 is NOT Update or animation interruption (vtable slot 292=OnShoved).
// Death-animation completion deliberately excludes original DIE_FROM_CROUCH668.
// Original convar object RVAs/defaults are in contracts/server_console.json.
// Actor vocalization gate 4003F0; calm/enraged/dead expressions
// 3FEF00/4036D0/403950; expression activity registration 402F40.
// [VERIFY] Historical S1 did not capture model/attack overlay/cycle/playback
// or recognition state. The native event/perception mechanisms below are not
// a claim that those missing historical samples have been reproduced.

static ConVar z_acquire_near_range( "z_acquire_near_range", "200", 0x00004004 );
static ConVar z_acquire_far_range( "z_acquire_far_range", "2500", 0x00004004 );
static ConVar z_acquire_near_time( "z_acquire_near_time", "0.5", 0x00004004 );
static ConVar z_acquire_far_time( "z_acquire_far_time", "5.0", 0x00004004 );
static ConVar z_acquire_time_variance_factor( "z_acquire_time_variance_factor", "0.25", 0x00004004 );
static ConVar z_attack_min_range( "z_attack_min_range", "10", 0x00002006 );
static ConVar z_attack_max_range( "z_attack_max_range", "20", 0x00002006 );
static ConVar z_attack_on_the_run_range( "z_attack_on_the_run_range", "35", 0x00002006 );
static ConVar z_attack_interval( "z_attack_interval", "1", 0x00002006 );
static ConVar z_attack_change_target_range( "z_attack_change_target_range", "100", 0x00002006 );
static ConVar z_force_attack_from_sound_range( "z_force_attack_from_sound_range", "750", 0x00004004 );
static ConVar z_stand_still( "z_stand_still", "0", 0x00004004, "For testing.  0: default.  1: unalerted common infected will stand still instead of wandering, turning, sitting, etc." );
static ConVar z_must_wander( "z_must_wander", "0", 0x00004004, "For testing.  0: default.  1: wandering zombies don't sit/lie down.  -1: wandering zombies always sit/lie down." );
static ConVar z_stumble_max_curve_rate( "z_stumble_max_curve_rate", "10", 0x00004004, "Maximum degrees/sec turning while stumbling forward" );
static ConVar z_stumble_max_curve_accel( "z_stumble_max_curve_accel", "5", 0x00004004, "Rate of turn increase per second" );
static ConVar z_lean_wall_align_speed( "z_lean_wall_align_speed", "300", 0x00004004 );
static ConVar z_hear_gunfire_range( "z_hear_gunfire_range", "200", 0x00004004 );
static ConVar z_hear_runner_near_range( "z_hear_runner_near_range", "500", 0x00004004 );
static ConVar z_hear_runner_far_range( "z_hear_runner_far_range", "750", 0x00004004 );
static ConVar z_attention_range_sitting_modifier( "z_attention_range_sitting_modifier", "0.35", 0x00004004 );
static ConVar z_attention_range_lying_modifier( "z_attention_range_lying_modifier", "0.15", 0x00004004 );
static ConVar z_shove_friend_speed( "z_shove_friend_speed", "130", 0x00004004 );
static ConVar z_breakable_damage( "z_breakable_damage", "4", 0x00004004 );
static ConVar z_expressions( "z_expressions", "1", 0x00004004 );
static ConVar z_minigun_atomize( "z_minigun_atomize", "0", 0x00000006, "minigun should atomize zombies" );
static ConVar z_Infected_debug( "z_Infected_debug", "0", 0x00004004 );
static ConVar z_brawl_chance( "z_brawl_chance", "2", 0x00004004 );

extern ConVar z_door_pound_damage;
extern ConVar z_prevent_burrowing;
extern ConVar god;
static ConVar z_attack_infected_it_damage( "z_attack_infected_it_damage", "10", 0x00004004,
    "Damage done per claw hit by infected on other infected who are IT" );
static ConVar z_destroy_on_attack( "z_destroy_on_attack", "0", 0x00004004,
    "When an infected starts punching its victim, destroy it. Useful for demos." );

extern bool IsMapTimeOfDay( int first, int last );
static ConVar nav_lying_down_percent( "nav_lying_down_percent", "50", 0x00004004,
    "Chance that wandering infected will be lying down in a SPAWN_LYINGDOWN area." );

namespace
{
#pragma float_control( precise, on, push )
static bool IsCommonAttackingUnderDisplacement( Infected *me )
{
    if ( !z_prevent_burrowing.GetBool() )
        return false;
    IBody *body = me->GetBodyInterface();
    ILocomotion *mover = me->GetLocomotionInterface();
    if ( !body || !mover )
        return false;
    Vector start = me->GetAbsOrigin();
    Vector end = start + Vector( 0.0f, 0.0f, 500.0f );
    Vector mins = body->GetHullMins();
    Vector maxs = body->GetHullMaxs();
    // Native stores the rounded minimum but compares the retained sum.
    const double raisedMin = (double)mover->GetStepHeight() + (double)0.1f + (double)mins.z;
    mins.z = (float)raisedMin;
    if ( raisedMin > (double)maxs.z )
        maxs.z = mins.z;
    CTraceFilterNoNPCsOrPlayer filter( NULL, COLLISION_GROUP_PLAYER_MOVEMENT );
    trace_t trace;
    UTIL_TraceHull( start, end, mins, maxs, 147979, &filter, &trace );
    if ( !( trace.fraction > 0.0f ) )
        return false;
    end = start;
    start = trace.endpos;
    UTIL_TraceHull( start, end, mins, maxs, 147979, &filter, &trace );
    if ( !( trace.fraction < 1.0f ) || !trace.IsDispSurface() )
        return false;
    CNavArea *area = me->GetLastKnownArea();
    Warning( "Zombie tried to attack from under displacement in nav area %d, setpos %.1f %.1f %.1f\n",
        area ? area->GetID() : -1, start.x, start.y, start.z );
    return true;
}
#pragma float_control( pop )
#pragma float_control( precise, on, push )
void SetUncommonMusicVariant( const char *variant )
{
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
        if ( player && player->edict() && player->entindex() != 0 && player->IsConnected() && player->GetTeamNumber() == TEAM_SURVIVOR )
            player->GetMusic()->SetMusicVariant( variant );
    }
}

static void CollectUncommonDescendants( CBaseEntity *first, CUtlVector< CBaseEntity * > &out )
{
    for ( CBaseEntity *child = first; child; child = child->NextMovePeer() )
    {
        out.AddToTail( child );
        CollectUncommonDescendants( child->FirstMoveChild(), out );
    }
}

class InfectedFleePathCost : public IPathCost
{
public:
    explicit InfectedFleePathCost( Infected *me ) : m_mover( me->GetLocomotionInterface() ) {}
    virtual float operator()( CNavArea *baseArea, CNavArea *baseFrom, const CNavLadder *ladder,
        const CFuncElevator *elevator, float length ) const OVERRIDE
    {
        if ( !m_mover->IsAreaTraversable( baseArea ) )
            return -1.0f;
        if ( !baseFrom )
            return 0.0f;
        TerrorNavArea *area = ToTerrorNavArea( baseArea );
        if ( ladder )
        {
            const float multiplier = baseFrom->GetCenter().z > area->GetCenter().z ? 3.0f : 20.0f;
            return (float)( (double)ladder->m_length * multiplier + baseFrom->GetCostSoFar() );
        }
        if ( area->GetDoor() || area->HasAttributes( 1 ) )
            return -1.0f;
        const float height = baseFrom->ComputeAdjacentConnectionHeightChange( area );
        if ( height > m_mover->GetMaxJumpHeight() )
            return -1.0f;
        float multiplier = height > m_mover->GetStepHeight() ? 20.0f : 1.0f;
        if ( area->IsCompletelyVisibleToAnyCharacter() ) multiplier *= 10.0f;
        else if ( area->IsPotentiallyVisibleToAnyCharacter() ) multiplier *= 5.0f;
        if ( area->HasAvoidanceObstacle( 18.0f ) ) multiplier *= 20.0f;
        if ( area->HasAttributes( 0x80000 ) ) multiplier *= 10.0f;
        if ( area->HasAttributes( 0x80 ) ) multiplier *= 10.0f;
        if ( !( length > 0.0f ) )
            length = ( area->GetCenter() - baseFrom->GetCenter() ).Length();
        return (float)( ( fabs( (double)height ) + (double)length ) * multiplier + baseFrom->GetCostSoFar() );
    }
private:
    ILocomotion *m_mover;
};

static float GetNativeFleeSurvivorAverageFlow()
{
    float average = -9999.0f;
    int count = 0;
    for ( int i = 1; i <= gpGlobals->maxClients; ++i )
    {
        CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
        if ( !player || !player->edict() || player->entindex() == 0 || !player->IsConnected() ||
             player->GetTeamNumber() != TEAM_SURVIVOR || !player->IsAlive() || player->IsIncapacitated() )
            continue;
        const TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
        if ( area && area->GetFlowDistance( FLOW_TOWARD_GOAL ) > -9999.0f )
        {
            average = ( (float)count * average + area->GetFlowDistance( FLOW_TOWARD_GOAL ) ) / (float)( count + 1 );
            ++count;
        }
    }
    return average;
}

class InfectedBurn : public Action< Infected >
{
public:
    virtual const char *GetName() const OVERRIDE { return "InfectedBurn"; }
    virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
    {
        m_yawRate = RandomFloat( -40.0f, 40.0f );
        m_deathTimer.Start( RandomFloat( 5.0f, 8.0f ) );
        m_turnPauseTimer.Invalidate();
        return Continue();
    }
    virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE
    {
        IBody *body = me->GetBodyInterface();
        if ( body && !body->IsActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_RUN_ON_FIRE ) ) && !body->IsActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_LADDER_DISMOUNT ) ) )
            body->StartActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_RUN_ON_FIRE ), IBody::MOTION_CONTROLLED_XY );
        if ( (double)gpGlobals->curtime >= (double)m_turnPauseTimer.m_timestamp )
        {
            QAngle angles = me->GetLocalAngles();
            angles.y += m_yawRate * interval;
            me->SetLocalAngles( angles );
        }
        if ( (double)gpGlobals->curtime >= (double)m_voiceTimer.m_timestamp )
        {
            if ( !UTIL_IsLowViolence() )
                me->EmitVocalization( "Zombie.IgniteScream", true );
            m_voiceTimer.Start( RandomFloat( 1.0f, 3.0f ) );
        }
        if ( !( (double)gpGlobals->curtime >= (double)m_deathTimer.m_timestamp ) )
            return Continue();
        CTakeDamageInfo info;
        me->Retire( info );
        return Done();
    }
    virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE
    {
        CTakeDamageInfo info;
        me->Retire( info );
        return TryDone();
    }
    virtual EventDesiredResult< Infected > OnShoved( Infected *me, CBaseEntity *pusher ) OVERRIDE
    {
        CTakeDamageInfo info;
        me->Retire( info );
        return TryDone();
    }
    virtual EventDesiredResult< Infected > OnInjured( Infected *me, const CTakeDamageInfo &info ) OVERRIDE
    {
        if ( info.GetDamageType() & DMG_BURN )
            m_turnPauseTimer.Start( RandomFloat( 0.3f, 0.6f ) );
        if ( me->LastHitGroup() == HITGROUP_HEAD )
            return TryChangeTo( me->CreateDyingAction( info ), RESULT_CRITICAL );
        return TryContinue();
    }
private:
    float m_yawRate; // NO native ctor store: omit manual initialization; OnStart's draw publishes before Update.
    CountdownTimer m_voiceTimer;
    CountdownTimer m_deathTimer;
    CountdownTimer m_turnPauseTimer;
};
#pragma float_control( pop )
const unsigned int infectedTraceMask = 33570827; // original MASK_SOLID

// 4036D0/403950: a matching anatomical part, not a replacement head model.
// Keep candidate order and the single original RandomInt draw.
bool SetBodygroupExpression( Infected *me, const char *groupName, const char *partName, char expression )
{
	const int group = me->FindBodygroupByName( groupName );
	if ( group < 0 || group >= me->GetNumBodyGroups() )
		return false;
	const char *current = me->GetBodygroupPartName( group, me->GetBodygroup( group ) );
	if ( current[0] == 'D' || ( expression == 'E' && current[0] == 'E' ) )
		return false;
	if ( expression == 'D' && current[0] == 'E' )
		current += 3;
	char kind;
	int part;
	if ( sscanf( current, "%c%d", &kind, &part ) != 2 )
		return false;
	const int prefixLength = Q_strlen( partName );
	CUtlVectorFixedGrowable< int, 16 > candidates;
	for ( int i = 0; i < me->GetBodygroupCount( group ); ++i )
	{
		const char *candidate = me->GetBodygroupPartName( group, i );
		char candidateExpression, candidateKind;
		int number, candidatePart, suffix = 0;
		if ( sscanf( candidate, "%c%d_%c%d_%n", &candidateExpression, &number, &candidateKind, &candidatePart, &suffix ) == 4 &&
			candidateExpression == expression && candidatePart == part && suffix > 0 && candidate[suffix] &&
			!Q_strncmp( candidate + suffix, partName, prefixLength ) )
			candidates.AddToTail( i );
	}
	if ( candidates.IsEmpty() )
		return false;
	me->SetBodygroup( group, candidates[ RandomInt( 0, candidates.Count() - 1 ) ] );
	return true;
}

void SetEnragedExpression( Infected *me )
{
	if ( !z_expressions.GetBool() )
	{
		SetBodygroupExpression( me, "Head", "Head", 'E' );
		return;
	}
	if ( !me->IsPlayingGesture( ACT_EXP_ANGRY ) )
	{
		const int calm = me->FindGestureLayer( ACT_EXP_IDLE );
		if ( me->IsValidLayer( calm ) )
			me->RemoveLayer( calm, 0.2f, 0.0f );
		const int angry = me->AddGesture( ACT_EXP_ANGRY, false );
		if ( me->IsValidLayer( angry ) )
			me->SetLayerLooping( angry, true );
	}
}


bool IsSurvivor( const CBaseEntity *entity )
{
	return entity && entity->IsPlayer() && ( entity->GetTeamNumber() == TEAM_SURVIVOR || entity->GetTeamNumber() == TEAM_L4D1_SURVIVOR );
}

bool IsValidVictim( CBaseEntity *entity )
{
	return entity && entity->IsAlive() && ( !entity->IsPlayer() || entity->GetTeamNumber() != TEAM_L4D1_SURVIVOR );
}

Vector ForwardVector( Infected *me )
{
	return me->BodyDirection2D();
}

float FacingDot( Infected *me, CBaseEntity *subject )
{
	Vector to = subject->GetAbsOrigin() - me->GetAbsOrigin();
	to.z = 0.0f;
	to.NormalizeInPlace();
	return DotProduct( to, ForwardVector( me ) );
}

enum ForcedSoundReaction { NO_FORCED_REACTION, REJECT_FORCED_REACTION, ACCEPT_FORCED_REACTION };
static ForcedSoundReaction SoundForcesAttack( Infected *me, CBaseEntity *source, KeyValues *keys, bool isWander )
{
    if ( !keys || ( me->GetInfectedFlags() & 8 ) )
        return NO_FORCED_REACTION;
    KeyValues *reaction = keys->FindKey( "Reaction" );
    if ( !reaction )
        return NO_FORCED_REACTION;
    const char *name = reaction->GetString();
    if ( !( ( !Q_stricmp( name, "SeekAndDestroy" ) && me->IsRangeLessThan( source, z_force_attack_from_sound_range.GetFloat() ) ) ||
        !Q_stricmp( name, "ChaseFromAnywhere" ) ) )
        return NO_FORCED_REACTION;
    // S549D0 reads native move-parent364, NOT owner524. Goal SetParent65860 writes this handle.
    CBaseEntity *parent = source ? source->GetMoveParent() : NULL;
    if ( parent && ( parent->GetGender() == 16 || parent->GetGender() == 17 ) )
    {
        if ( me->GetGender() == 16 || ( !isWander && me->GetGender() == 17 ) )
            return REJECT_FORCED_REACTION;
        parent = source->GetMoveParent(); // Native re-read; cleared parent skips only this target-dependent test.
        if ( parent && !me->IsLineOfSightClear( parent, CBaseCombatCharacter::IGNORE_NOTHING ) )
            return REJECT_FORCED_REACTION;
    }
    return ACCEPT_FORCED_REACTION;
}

bool IsOutside( Infected *me )
{
	trace_t trace;
	Vector start = me->GetPosition();
	UTIL_TraceLine( start, start + Vector( 0.0f, 0.0f, 99999.898f ), infectedTraceMask, me, COLLISION_GROUP_NONE, &trace );
	return ( trace.fraction >= 1.0f && !trace.startsolid && !trace.allsolid ) || ( trace.surface.flags & SURF_SKY );
}

// Original UTIL support test 20A020, used by 3FDDF0. Nine adjoining cells
// test both slope and relative ground height; a single center hull is not enough.
bool HasRoomToRest( Infected *me, float radius )
{
	trace_t ground[9];
	const float cellWidth = ( radius * 2.0f ) * ( 1.0f / 3.0f );
	const Vector &origin = me->GetPosition();
	const float height = me->GetBodyInterface()->GetCrouchHullHeight();
	int index = 0;
	for ( int y = 0; y < 3; ++y )
	{
		for ( int x = 0; x < 3; ++x, ++index )
		{
			Vector mins( -radius + x * cellWidth, -radius + y * cellWidth, 0.0f );
			Vector maxs( mins.x + cellWidth, mins.y + cellWidth, 10.0f );
			UTIL_TraceHull( origin + Vector( 0, 0, height ), origin - Vector( 0, 0, 100 ), mins, maxs,
				0x0202420B, me, COLLISION_GROUP_NPC, &ground[index] );
		}
	}
	for ( int i = 0; i < ARRAYSIZE( ground ); ++i )
	{
		if ( ground[i].allsolid )
			return false;
		if ( ground[i].DidHit() )
		{
			if ( ground[i].plane.normal.z < 0.9f )
				return false;
			for ( int j = 0; j < ARRAYSIZE( ground ); ++j )
				if ( i != j && fabsf( ground[i].endpos.z - ground[j].endpos.z ) > 10.0f )
					return false;
		}
	}
	return true;
}

class InfectedChangePosture : public Action< Infected >
{
public:
	explicit InfectedChangePosture( IBody::PostureType posture ) : m_posture( posture ) {}
	virtual const char *GetName() const OVERRIDE { return "InfectedChangePosture"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		me->GetBodyInterface()->SetDesiredPosture( m_posture );
		return Continue();
	}
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE
	{
		IBody *body = me->GetBodyInterface();
		if ( body->IsInDesiredPosture() )
			return Done();
		if ( !body->IsPostureChanging() )
			body->SetDesiredPosture( m_posture );
		return Continue();
	}
private:
	IBody::PostureType m_posture;
};


class InfectedStandDazed;
class InfectedSitDown;
class InfectedLieDown;
class InfectedStaggerAround;

class InfectedLeanOnWall : public Action< Infected >
{
public:
	explicit InfectedLeanOnWall( const Vector &normal ) : m_normal( normal ) {}
	virtual const char *GetName() const OVERRIDE { return "InfectedLeanOnWall"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *prior ) OVERRIDE { return Done(); }
private:
	Vector m_normal;
	float m_yaw;
	Activity m_activity;
	CountdownTimer m_timer;
};

class InfectedStandDazed : public Action< Infected >
{
public:
	InfectedStandDazed() : m_canRest( true ) {}
	virtual const char *GetName() const OVERRIDE { return "InfectedStandDazed"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		me->GetBodyInterface()->StartActivity( ACT_TERROR_IDLE_NEUTRAL, IBody::MOTION_CONTROLLED_XY );
		return Continue();
	}
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE;
private:
	bool m_canRest;
};

class InfectedSitDown : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "InfectedSitDown"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
private:
	CountdownTimer m_timer;
};

class InfectedLieDown : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "InfectedLieDown"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE;
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
private:
	CountdownTimer m_timer;
};

class InfectedStaggerAround : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "InfectedStaggerAround"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		me->GetBodyInterface()->StartActivity( ACT_TERROR_WALK_NEUTRAL, IBody::MOTION_CONTROLLED_XY );
		m_turnRate = RandomFloat( -z_stumble_max_curve_rate.GetFloat(), z_stumble_max_curve_rate.GetFloat() );
		m_currentTurnRate = 0.0f;
		return Continue();
	}
	virtual ActionResult< Infected > OnResume( Infected *me, Action< Infected > *prior ) OVERRIDE { return OnStart( me, prior ); }
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE
	{
		// Original asymmetric clamp is intentional, even for a negative chosen rate.
		m_currentTurnRate += z_stumble_max_curve_accel.GetFloat() * interval;
		if ( m_currentTurnRate > m_turnRate )
			m_currentTurnRate = m_turnRate;
		QAngle angles = me->GetLocalAngles();
		angles.y += interval * m_currentTurnRate;
		me->SetLocalAngles( angles );
		return Continue(); // animation root motion belongs to ZombieBotBody
	}
	virtual EventDesiredResult< Infected > OnAnimationActivityComplete( Infected *me, int activity ) OVERRIDE
	{
		return activity == ACT_TERROR_WALK_NEUTRAL ? TryChangeTo( new InfectedStandDazed ) : TryContinue();
	}
	virtual EventDesiredResult< Infected > OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace ) OVERRIDE
	{
		if ( !other || ( trace && trace->startsolid ) )
			return TryContinue();
		if ( IsSurvivor( other ) && me->CanBecomeActive() )
			return TryChangeTo( new InfectedAlert( other->WorldSpaceCenter() ) );
		if ( IsSuspended() )
			return TryContinue();
		if ( trace && trace->plane.normal.z < 0.1f && other->IsWorld() )
			return TrySuspendFor( new InfectedLeanOnWall( trace->plane.normal ), RESULT_TRY, "Bumped into a wall" );
		return TrySuspendFor( new InfectedStandingActivity( ACT_TERROR_ABOUT_FACE_NEUTRAL ), RESULT_TRY, "Bumped into something" );
	}
private:
	float m_currentTurnRate;
	float m_turnRate;
};

ActionResult< Infected > InfectedStandDazed::OnStart( Infected *me, Action< Infected > *prior )
{
	IBody *body = me->GetBodyInterface();
	if ( body->IsActualPosture( IBody::SIT ) || body->IsActualPosture( IBody::LIE ) )
	{
		m_canRest = false;
		return SuspendFor( new InfectedChangePosture( IBody::STAND ) );
	}
	m_canRest = true;
	if ( z_stand_still.GetInt() != 0 || RandomFloat( 0.0f, 100.0f ) >= 50.0f )
	{
		body->StartActivity( ACT_TERROR_IDLE_NEUTRAL, IBody::MOTION_CONTROLLED_XY );
		return Continue();
	}
	const int choice = RandomInt( 0, 100 );
	Activity facing = choice < 33 ? ACT_TERROR_FACE_LEFT_NEUTRAL : ( choice < 66 ? ACT_TERROR_FACE_RIGHT_NEUTRAL : ACT_TERROR_ABOUT_FACE_NEUTRAL );
	return SuspendFor( new InfectedStandingActivity( facing, RandomFloat( 3.0f, 15.0f ) ), "Starting with random facing" );
}

EventDesiredResult< Infected > InfectedStandDazed::OnAnimationActivityComplete( Infected *me, int activity )
{
	if ( z_stand_still.GetInt() > 0 )
		return TryChangeTo( new InfectedStandDazed );
	if ( z_must_wander.GetInt() <= 0 )
	{
		if ( z_must_wander.GetInt() < 0 )
			return RandomInt( 0, 10 ) < 5 ? TryChangeTo( new InfectedLieDown ) : TryChangeTo( new InfectedSitDown );
		if ( m_canRest )
		{
			const int choice = RandomInt( 0, 100 );
			const bool outside = IsOutside( me );
			if ( choice < ( outside ? 5 : 15 ) )
				return TryChangeTo( new InfectedLieDown );
			if ( choice < ( outside ? 10 : 30 ) )
				return TryChangeTo( new InfectedSitDown );
		}
	}
	return TryChangeTo( new InfectedStaggerAround );
}

ActionResult< Infected > InfectedSitDown::OnStart( Infected *me, Action< Infected > *prior )
{
	if ( UTIL_PointContents( me->GetPosition() ) & MASK_WATER )
		return ChangeTo( new InfectedStandDazed, "I don't want to sit in the water here - standing instead" );
	if ( !HasRoomToRest( me, 30.0f ) )
		return ChangeTo( new InfectedStandDazed, "No room to sit down - standing instead" );
	m_timer.Start( RandomFloat( 10.0f, 30.0f ) );
	return me->GetBodyInterface()->IsActualPosture( IBody::SIT ) ? Continue() : SuspendFor( new InfectedChangePosture( IBody::SIT ) );
}

ActionResult< Infected > InfectedSitDown::Update( Infected *me, float interval )
{
	if ( z_must_wander.GetInt() > 0 )
		return ChangeTo( new InfectedStandDazed, "Wandering is required" );
	if ( !m_timer.IsElapsed() )
		return Continue();
	if ( z_must_wander.GetInt() < 0 )
		return ChangeTo( new InfectedLieDown );
	const int choice = RandomInt( 0, 100 );
	return choice < ( IsOutside( me ) ? 25 : 50 ) ? ChangeTo( new InfectedLieDown ) : ChangeTo( new InfectedStandDazed );
}

ActionResult< Infected > InfectedLieDown::OnStart( Infected *me, Action< Infected > *prior )
{
	if ( UTIL_PointContents( me->GetPosition() ) & MASK_WATER )
		return ChangeTo( new InfectedStandDazed, "I don't want to lie down in the water here - standing instead" );
	if ( !HasRoomToRest( me, 50.0f ) )
		return ChangeTo( new InfectedSitDown, "No room to lie down - sitting instead" );
	m_timer.Start( RandomFloat( 15.0f, 30.0f ) );
	return me->GetBodyInterface()->IsActualPosture( IBody::LIE ) ? Continue() : SuspendFor( new InfectedChangePosture( IBody::LIE ) );
}

ActionResult< Infected > InfectedLieDown::Update( Infected *me, float interval )
{
	if ( z_must_wander.GetInt() > 0 )
		return ChangeTo( new InfectedStandDazed, "Wandering is required" );
	if ( !m_timer.IsElapsed() )
		return Continue();
	if ( z_must_wander.GetInt() < 0 )
		return ChangeTo( new InfectedSitDown );
	const int choice = RandomInt( 0, 100 );
	return choice < ( IsOutside( me ) ? 25 : 75 ) ? ChangeTo( new InfectedSitDown ) : ChangeTo( new InfectedStandDazed );
}

ActionResult< Infected > InfectedLeanOnWall::OnStart( Infected *me, Action< Infected > *prior )
{
	Vector forward, right, facing;
	AngleVectors( me->GetAbsAngles(), &forward, &right, NULL );
	const float frontDot = DotProduct( m_normal, forward );
	if ( frontDot < -0.70709997f )
	{
		m_activity = ACT_TERROR_LEAN_FORWARD_IDLE;
		facing = -m_normal;
	}
	else if ( frontDot > 0.70709997f )
	{
		m_activity = ACT_TERROR_LEAN_BACKWARD_IDLE;
		facing = m_normal;
	}
	else if ( DotProduct( m_normal, right ) > 0.0f )
	{
		m_activity = ACT_TERROR_LEAN_LEFTWARD_IDLE;
		facing.Init( -m_normal.y, m_normal.x, 0 );
	}
	else
	{
		m_activity = ACT_TERROR_LEAN_RIGHTWARD_IDLE;
		facing.Init( m_normal.y, -m_normal.x, 0 );
	}
	QAngle angles;
	VectorAngles( facing, angles );
	m_yaw = angles.y;
	IBody *body = me->GetBodyInterface();
	const float halfWidth = body->GetHullWidth() * 0.5f;
	const float quarterWidth = halfWidth * 0.5f;
	const float height = body->GetStandHullHeight();
	const bool backward = m_activity == ACT_TERROR_LEAN_BACKWARD_IDLE;
	Vector mins( -quarterWidth, -quarterWidth, height * ( backward ? 0.4f : 0.75f ) );
	Vector maxs( quarterWidth, quarterWidth, height * ( backward ? 0.6f : 1.0f ) );
	trace_t trace;
	NextBotTraversableTraceFilter filter( me, ILocomotion::IMMEDIATELY );
	UTIL_TraceHull( me->GetPosition(), me->GetPosition() - halfWidth * m_normal, mins, maxs, body->GetSolidMask(), &filter, &trace );
	if ( !trace.DidHit() )
		return ChangeTo( new InfectedStandingActivity( ACT_TERROR_ABOUT_FACE_NEUTRAL ), "No support for lean" );
	body->StartActivity( m_activity, IBody::MOTION_CONTROLLED_XY );
	m_timer.Start( RandomFloat( 5.0f, 20.0f ) );
	return Continue();
}

ActionResult< Infected > InfectedLeanOnWall::Update( Infected *me, float interval )
{
	if ( m_timer.IsElapsed() )
		return m_activity == ACT_TERROR_LEAN_BACKWARD_IDLE ? Done() : ChangeTo( new InfectedStandingActivity( ACT_TERROR_ABOUT_FACE_NEUTRAL ), "Finished with lean" );
	QAngle angles = me->GetAbsAngles();
	angles.x = ApproachAngle( angles.x, angles.x, z_lean_wall_align_speed.GetFloat() * interval );
	angles.y = ApproachAngle( m_yaw, angles.y, z_lean_wall_align_speed.GetFloat() * interval );
	angles.z = 0.0f;
	me->SetAbsAngles( angles );
	return Continue();
}

class DebugMoveTo : public Action< Infected >
{
public:
	explicit DebugMoveTo( const Vector &goal ) : m_goal( goal ) {}
	virtual const char *GetName() const OVERRIDE { return "DebugMoveTo"; }
	virtual bool IsAbleToBlockMovementOf( const INextBot *botInMotion ) const OVERRIDE { return false; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		InfectedPathCost cost( me );
		m_path.Compute( me, m_goal, cost, 0.0f );
		me->GetBodyInterface()->SetArousal( IBody::ALERT );
		m_timer.Start( 20.0f );
		me->ClearImmobileStatus();
		return Continue();
	}
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE;
private:
	Vector m_goal;
	PathFollower m_path;
	CountdownTimer m_timer;
};

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
ActionResult< Infected > DebugMoveTo::Update( Infected *me, float interval )
{
	IBody *body = me->GetBodyInterface();
	ILocomotion *mover = me->GetLocomotionInterface();
	if ( body->IsActualPosture( IBody::SIT ) || body->IsActualPosture( IBody::LIE ) )
		return SuspendFor( new InfectedChangePosture( IBody::STAND ) );
	m_path.Update( me );
	if ( m_timer.IsElapsed() || !m_path.IsValid() || ( me->GetAbsOrigin() - m_goal ).LengthSqr() < 400.0f )
		return Done();
	if ( me->IsImmobile() && me->GetImmobileDuration() > 5.0f )
	{
		if ( !body->IsActivity( ACT_TERROR_UNABLE_TO_REACH_TARGET ) )
			body->StartActivity( ACT_TERROR_UNABLE_TO_REACH_TARGET, IBody::MOTION_CONTROLLED_XY );
		return Done();
	}
	if ( me->IsDebugging( NEXTBOT_PATH ) ) m_path.Draw();
	if ( !mover->IsClimbingOrJumping() && !mover->IsUsingLadder() && mover->IsOnGround() )
	{
		const Activity activity = body->IsActualPosture( IBody::STAND ) ? ACT_TERROR_RUN_INTENSE : ACT_TERROR_CROUCH_RUN_INTENSE;
		if ( !body->IsActivity( activity ) ) body->StartActivity( activity, 0 );
	}
	return Continue();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

class LostVictim : public Action< Infected >
{
public:
	virtual const char *GetName() const OVERRIDE { return "LostVictim"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		SetCalmExpression( me );
		if ( !me->GetLocomotionInterface()->IsRunning() || !me->GetBodyInterface()->StartActivity( ACT_TERROR_RUN_INTENSE_TO_STAND_ALERT, IBody::MOTION_CONTROLLED_XY ) )
			return Done();
		m_timer.Start( 5.0f );
		return Continue();
	}
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE
	{
		if ( !me->GetBodyInterface()->IsActivity( ACT_TERROR_RUN_INTENSE_TO_STAND_ALERT ) )
			return Done( "No longer stumbling to a stop" );
		return m_timer.IsElapsed() ? Done( "Spent too long in this state" ) : Continue();
	}
private:
	CountdownTimer m_timer;
};

bool IsMeleeDeathActivity( int activity )
{
	return activity == ACT_BLUDGEON_DEATH_BACK || activity == ACT_BLUDGEON_DEATH_FORWARD ||
		activity == ACT_BLUDGEON_DEATH_LEFT || activity == ACT_BLUDGEON_DEATH_RIGHT ||
		activity == ACT_SLICING_DEATH_BACK || activity == ACT_SLICING_DEATH_FORWARD ||
		activity == ACT_SLICING_DEATH_LEFT || activity == ACT_SLICING_DEATH_RIGHT;
}

bool IsShoveActivity( int activity )
{
	return activity == ACT_TERROR_SHOVED_FORWARD || activity == ACT_TERROR_SHOVED_BACKWARD ||
		activity == ACT_TERROR_SHOVED_LEFTWARD || activity == ACT_TERROR_SHOVED_RIGHTWARD;
}
} // namespace

// S415080: the supplied range is unused, and the request is CRITICAL.
EventDesiredResult< Infected > InfectedBehavior::OnCommandApproach( Infected *me, const Vector &goal, float range )
{
	return TrySuspendFor( new DebugMoveTo( goal ), RESULT_CRITICAL, "Debug move to" );
}

float Script_GetAverageSurvivorFlowDistance()
{
	return GetNativeFleeSurvivorAverageFlow();
}

void SetCalmExpression( Infected *me )
{
	if ( z_expressions.GetBool() )
	{
		if ( !me->IsPlayingGesture( ACT_EXP_IDLE ) )
		{
			const int angry = me->FindGestureLayer( ACT_EXP_ANGRY );
			if ( me->IsValidLayer( angry ) )
				me->RemoveLayer( angry, 0.2f, 0.0f );
			const int calm = me->AddGesture( ACT_EXP_IDLE, false );
			if ( me->IsValidLayer( calm ) )
				me->SetLayerLooping( calm, true );
		}
		return;
	}
	const int group = me->FindBodygroupByName( "Head" );
	if ( group < 0 || group >= me->GetNumBodyGroups() )
		return;
	const char *current = me->GetBodygroupPartName( group, me->GetBodygroup( group ) );
	if ( current[0] != 'E' )
		return;
	for ( int i = 0; i < me->GetBodygroupCount( group ); ++i )
	{
		if ( !Q_stricmp( current + 3, me->GetBodygroupPartName( group, i ) ) )
		{
			me->SetBodygroup( group, i );
			return;
		}
	}
}

InfectedBehavior::InfectedBehavior() : m_shoveTolerance( RandomInt( 2, 5 ) )
{
}

Action< Infected > *InfectedExecAction::InitialContainedAction( Infected *me )
{
	return new InfectedBehavior;
}

Action< Infected > *InfectedBehavior::InitialContainedAction( Infected *me )
{
	return new InfectedWander;
}

EventDesiredResult< Infected > InfectedBehavior::OnKilled( Infected *me, const CTakeDamageInfo &info )
{
	return TryChangeTo( me->CreateDyingAction( info ), RESULT_CRITICAL );
}

#pragma float_control( precise, on, push )
EventDesiredResult< Infected > InfectedBehavior::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
    static ConVarRef invulnerable( "z_infected_invuln" );
    if ( !me->IsAlive() || invulnerable.GetBool() )
        return TryContinue();
    if ( me->LastHitGroup() == HITGROUP_HEAD && me->GetGender() != 17 &&
        ( me->GetGender() != 14 || me->IsAmbush() ) )
        return TryChangeTo( me->CreateDyingAction( info ), RESULT_CRITICAL );
    if ( (unsigned int)info.GetDamageType() & 0x20000000u )
    {
        CBaseEntity *inflictor = info.GetInflictor();
        if ( inflictor && ( inflictor->GetAbsOrigin() - me->GetAbsOrigin() ).LengthSqr() < 62500.0f )
            return TrySuspendFor( new InfectedShoved( inflictor ), RESULT_TRY );
    }
    return TryContinue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > InfectedBehavior::OnShoved( Infected *me, CBaseEntity *pusher )
{
	if ( !me->IsShoveImmune() )
	{
		me->RecordShove();
		me->ClearInfectedFlags( 0x40 );
		if ( --m_shoveTolerance < 0 && !( me->GetInfectedFlags() & 0x20 ) && me->GetGender() != 17 )
			me->ShoveKill( pusher );
	}
	return TryContinue();
}

InfectedWander::InfectedWander( Action< Infected > *initialChild )
{
    // Timers56/68/92/104 already construct in declaration order. Native419BA0
    // then writes optional child52. NO store/Vector::Init to recognitionVector80.
    m_initialChild = initialChild;
}

ActionResult< Infected > InfectedWander::OnStart( Infected *me, Action< Infected > *priorAction )
{
    SetCalmExpression( me );
    IBody *body = me->GetBodyInterface();
    if ( body )
        body->SetArousal( IBody::NEUTRAL );
    m_wanderTimer.Start( RandomFloat( 8.0f, 12.0f ) );
    m_voiceTimer.Start( RandomFloat( 0.0f, 6.0f ) );
    return Continue();
}

Action< Infected > *InfectedWander::InitialContainedAction( Infected *me )
{
    if ( m_initialChild )
        return m_initialChild;
    if ( z_stand_still.GetInt() > 0 )
        return new InfectedStandDazed;
    const int choice = RandomInt( 0, 100 );
    const TerrorNavArea *area = ToTerrorNavArea( me->GetLastKnownArea() );
    if ( area && area->HasSpawnAttributes( TERROR_NAV_LYINGDOWN ) && choice < nav_lying_down_percent.GetInt() )
        return new InfectedLieDown;
    const bool daytime = IsMapTimeOfDay( 2, 4 );
    const int mustWander = z_must_wander.GetInt();
    if ( mustWander <= 0 && !TheDirector->GetSessionManager()->IsLowViolence() && !daytime )
    {
        if ( mustWander < 0 )
            return new InfectedSitDown;
        if ( IsOutside( me ) )
        {
            if ( choice < 5 )
                return new InfectedLieDown;
            if ( choice < 10 )
                return new InfectedSitDown;
        }
        else
        {
            if ( choice < 15 )
                return new InfectedLieDown;
            if ( choice < 30 )
                return new InfectedSitDown;
        }
    }
    return new InfectedStandDazed;
}

#pragma float_control( precise, on, push )
void InfectedWander::BecomeAlert( Infected *me, const Vector &pos )
{
    if ( m_alertTimer.m_timestamp > 0.0f )
        return;
    float scale = 1.0f;
    IBody *body = me->GetBodyInterface();
    if ( body->IsActualPosture( IBody::LIE ) )
        scale = 3.0f;
    else if ( !body->IsActualPosture( IBody::STAND ) )
        scale = 2.0f;
    else if ( IsMapTimeOfDay( 2, 4 ) )
        scale = 0.5f;
    m_alertTimer.Start( RandomFloat( 0.30000001f, 1.0f ) * scale );
    m_recognitionPosition = pos;
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
ActionResult< Infected > InfectedWander::Update( Infected *me, float interval )
{
    IVision *vision = me->GetVisionInterface();
    if ( !( m_alertTimer.m_timestamp > 0.0f ) )
    {
        if ( TheDirector->IsFinale() || TheDirector->GetSurvivalMode()->HasStarted() )
        {
            CTerrorPlayer *survivor = PickLivingSurvivorForForceStart();
            if ( survivor )
                BecomeAlert( me, survivor->WorldSpaceCenter() );
        }
        CBaseEntity *victim = vision->GetPrimaryKnownThreat();
        if ( victim )
            BecomeAlert( me, victim->EyePosition() );
    }
    else if ( gpGlobals->curtime >= m_alertTimer.m_timestamp )
        return ChangeTo( new InfectedAlert( m_recognitionPosition ) );
    if ( gpGlobals->curtime >= m_voiceTimer.m_timestamp )
    {
        m_voiceTimer.Start( RandomFloat( 4.0f, 6.0f ) );
        if ( me->GetGender() == 16 && RandomInt( 1, 100 ) < 50 )
        {
            DispatchParticleEffect( "blood_spray_clown", PATTACH_POINT_FOLLOW, me, me->LookupAttachment( "flower" ) );
            me->EmitSound( "Blood.Spurt" );
        }
        me->EmitVocalization( me->GetBodyInterface()->IsActualPosture( IBody::STAND ) ? "Zombie.Wander" : "Zombie.Sleeping" );
    }
    return Continue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > InfectedWander::OnCommandString( Infected *me, const char *command )
{
    // Native41AAE0: NULL command minimal skip, per Main's standing rule.
    if ( !command )
        return TryContinue();
    if ( FStrEq( command, "z_sit_down" ) )
        return TryChangeTo( new InfectedWander( new InfectedSitDown ) );
    if ( FStrEq( command, "z_lie_down" ) )
        return TryChangeTo( new InfectedWander( new InfectedLieDown ) );
    if ( FStrEq( command, "z_stand" ) )
        return TryChangeTo( new InfectedWander( new InfectedStandDazed ) );
    return TryContinue();
}

ActionResult< Infected > InfectedWander::OnResume( Infected *me, Action< Infected > *interruptingAction )
{
	return ChangeTo( new InfectedWander );
}

#pragma float_control( precise, on, push )
EventDesiredResult< Infected > InfectedWander::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
    if ( !other || !other->IsAlive() )
        return TryContinue();
    if ( !other->IsPlayer() )
    {
        INextBot *bot = dynamic_cast< INextBot * >( other );
        if ( bot )
        {
            IBody *body = bot->GetBodyInterface();
            // Native body flag2 is MOTION_CONTROLLED_Z, NOT uninterruptible4 or arousal.
            if ( bot->GetEntity()->IsOnFire() || ( body && body->HasActivityType( IBody::MOTION_CONTROLLED_Z ) ) )
                return TrySuspendFor( new InfectedShoved( other ) );
            // Native2E480 ordered now>=deadline: unordered does not enter.
            if ( (double)gpGlobals->curtime >= (double)m_brawlTimer.m_timestamp )
            {
                const int chance = z_brawl_chance.GetInt();
                const int roll = RandomInt( 0, 100 );
                if ( roll < chance && dynamic_cast< Witch * >( bot ) == NULL )
                    return TryChangeTo( new InfectedAttack( bot->GetEntity() ) );
                m_brawlTimer.Start( 3.0f );
            }
        }
        return TryContinue();
    }
    if ( IsSurvivor( other ) )
    {
        if ( me->GetInfectedFlags() & 0x400 )
            return TryChangeTo( new InfectedFlee );
        if ( me->CanBecomeActive() )
            return TryChangeTo( new InfectedAttack( other ) );
        return TryContinue();
    }
    // Native non-survivor PLAYER contact is shoved even when not active.
    return TrySuspendFor( new InfectedShoved( other ) );
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
EventDesiredResult< Infected > InfectedWander::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
    CBaseEntity *attacker = info.GetAttacker();
    if ( !attacker )
        return TryContinue();
    if ( attacker->IsPlayer() )
    {
        if ( IsSurvivor( attacker ) )
        {
            if ( me->GetInfectedFlags() & 0x400 )
                return TryChangeTo( new InfectedFlee );
            if ( me->CanBecomeActive() )
                return TryChangeTo( new InfectedAlert( ToTerrorPlayer( attacker )->GetInfectedAlertPosition(), InfectedAlert::INJURY ) );
        }
        return TryContinue();
    }
    INextBot *bot = dynamic_cast< INextBot * >( attacker );
    if ( bot )
        return TryChangeTo( new InfectedAttack( bot->GetEntity() ) );
    return TryContinue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > InfectedWander::OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	const ForcedSoundReaction reaction = SoundForcesAttack( me, source, keys, true );
	if ( reaction == REJECT_FORCED_REACTION )
		return TryToSustain();
	if ( reaction == ACCEPT_FORCED_REACTION )
		return TryChangeTo( new InfectedAttack( source ), RESULT_IMPORTANT, "Heard a sound that forced me to attack its source" );
	CTerrorPlayer *player = IsSurvivor( source ) ? static_cast< CTerrorPlayer * >( source ) : NULL;
	if ( !player || !player->IsAlive() || ( me->GetInfectedFlags() & 0x4000 ) )
		return TryContinue();
	const float range = me->GetRangeTo( player );
	IBody *body = me->GetBodyInterface();
	const float postureScale = body->IsActualPosture( IBody::LIE ) ? z_attention_range_lying_modifier.GetFloat() :
		( body->IsActualPosture( IBody::STAND ) ? 1.0f : z_attention_range_sitting_modifier.GetFloat() );
	if ( player->GetTimeSinceWeaponFired() < 1.0f && range < z_hear_gunfire_range.GetFloat() * postureScale )
		BecomeAlert( me, player->WorldSpaceCenter() );
	if ( player->IsRunning() && player->GetTimeSinceStartedRunning() > 2.0f )
	{
		if ( range < z_hear_runner_near_range.GetFloat() )
			BecomeAlert( me, player->WorldSpaceCenter() );
		else if ( range < z_hear_runner_far_range.GetFloat() )
		{
			Vector to = player->GetAbsOrigin() - me->GetPosition();
			Vector velocity = player->GetAbsVelocity();
			to.NormalizeInPlace();
			velocity.NormalizeInPlace();
			if ( DotProduct( to, velocity ) < -0.93959999f )
				BecomeAlert( me, player->WorldSpaceCenter() );
		}
	}
	return TryContinue();
}

EventDesiredResult< Infected > InfectedWander::OnShoved( Infected *me, CBaseEntity *pusher )
{
	IBody *body = me->GetBodyInterface();
	if ( !me->IsShoveImmune() && IsSurvivor( pusher ) &&
		( body->IsActualPosture( IBody::LIE ) || FacingDot( me, pusher ) < -0.70709997f ) )
	{
		me->ShoveKill( pusher );
		return TryContinue();
	}
	if ( body->IsActualPosture( IBody::LIE ) )
		return TryContinue();
	Action< Infected > *successor = me->CanBecomeActive() && pusher ? new InfectedAlert( pusher->GetAbsOrigin() ) : NULL;
	return TrySuspendFor( new InfectedShoved( pusher, successor ) );
}

InfectedAlert::InfectedAlert( const Vector &pos, Reason reason ) : m_alertPosition( pos ), m_reason( reason )
{
}

bool InfectedAlert::StartAlertActivity( Infected *me )
{
	Vector to = m_alertPosition - me->GetPosition();
	to.z = 0.0f;
	to.NormalizeInPlace();
	const Vector forward = ForwardVector( me );
	const float frontDot = DotProduct( to, forward );
	Activity activity;
	if ( frontDot > 0.70709997f )
		activity = m_reason == INJURY ? ACT_TERROR_IDLE_ALERT_INJURED_AHEAD : ACT_TERROR_IDLE_ALERT_AHEAD;
	else if ( frontDot < -0.70709997f )
		activity = m_reason == INJURY ? ACT_TERROR_IDLE_ALERT_INJURED_BEHIND : ACT_TERROR_IDLE_ALERT_BEHIND;
	else if ( -forward.y * to.x + forward.x * to.y > 0.70709997f )
		activity = m_reason == INJURY ? ACT_TERROR_IDLE_ALERT_INJURED_LEFT : ACT_TERROR_IDLE_ALERT_LEFT;
	else
		activity = m_reason == INJURY ? ACT_TERROR_IDLE_ALERT_INJURED_RIGHT : ACT_TERROR_IDLE_ALERT_RIGHT;
	if ( !me->GetBodyInterface()->StartActivity( activity, IBody::MOTION_CONTROLLED_XY | IBody::ENTINDEX_PLAYBACK_RATE ) )
		return false;
	m_soundTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	m_calmTimer.Start( RandomFloat( 5.0f, 15.0f ) );
	return true;
}

ActionResult< Infected > InfectedAlert::OnStart( Infected *me, Action< Infected > *priorAction )
{
	IBody *body = me->GetBodyInterface();
	body->SetArousal( IBody::ALERT );
	me->EmitSound( "Zombie.BecomeAlert" );
	m_voiceTimer.Start( RandomFloat( 2.0f, 4.0f ) );
	if ( body->IsActualPosture( IBody::SIT ) || body->IsActualPosture( IBody::LIE ) )
		return SuspendFor( new InfectedChangePosture( IBody::STAND ) );
	if ( !StartAlertActivity( me ) )
		return ChangeTo( new InfectedWander );
	m_acquireTimer.Invalidate();
	return Continue();
}

ActionResult< Infected > InfectedAlert::OnResume( Infected *me, Action< Infected > *interruptingAction )
{
	if ( !StartAlertActivity( me ) )
		return ChangeTo( new InfectedWander );
	m_calmTimer.Start( RandomFloat( 8.0f, 12.0f ) );
	return Continue();
}

ActionResult< Infected > InfectedAlert::Update( Infected *me, float interval )
{
	// S4075BA uses the common body's five-argument aim slot, not IBody's stub.
	ZombieBotBody *body = me->GetBodyInterface();
	if ( m_voiceTimer.IsElapsed() )
	{
		me->EmitVocalization( "Zombie.Alert" );
		m_voiceTimer.Start( RandomFloat( 2.0f, 4.0f ) );
	}
	if ( m_reorientTimer.HasStarted() && m_reorientTimer.IsElapsed() )
	{
		StartAlertActivity( me );
		m_reorientTimer.Invalidate();
	}
	CBaseCombatCharacter *victim = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( !victim )
	{
		if ( m_acquireTimer.HasStarted() )
		{
			m_acquireTimer.Invalidate();
			body->StartActivity( ACT_TERROR_IDLE_ALERT, IBody::MOTION_CONTROLLED_XY );
			SetCalmExpression( me );
		}
	}
	else if ( m_acquireTimer.HasStarted() )
	{
		body->AimHeadTowards( victim, IBody::IMPORTANT, 0.1f, NULL, "Watch our victim" );
		if ( m_acquireTimer.IsElapsed() )
			return ChangeTo( new InfectedAttack( victim ) );
	}
	else
	{
		const float fraction = clamp( ( me->GetRangeTo( victim ) - z_acquire_near_range.GetFloat() ) /
			( z_acquire_far_range.GetFloat() - z_acquire_near_range.GetFloat() ), 0.0f, 1.0f );
		const float duration = z_acquire_near_time.GetFloat() + fraction * ( z_acquire_far_time.GetFloat() - z_acquire_near_time.GetFloat() );
		const float variance = z_acquire_time_variance_factor.GetFloat();
		m_acquireTimer.Start( duration * RandomFloat( 1.0f - variance, 1.0f + variance ) );
		if ( !body->IsActivity( ACT_TERROR_IDLE_ACQUIRE ) )
			body->StartActivity( ACT_TERROR_IDLE_ACQUIRE, IBody::MOTION_CONTROLLED_XY );
		SetEnragedExpression( me );
	}
	if ( m_calmTimer.IsElapsed() && !body->IsActivity( ACT_TERROR_ALERT_TO_NEUTRAL ) )
		body->StartActivity( ACT_TERROR_ALERT_TO_NEUTRAL, IBody::MOTION_CONTROLLED_XY );
	return Continue();
}

EventDesiredResult< Infected > InfectedAlert::OnPostureChanged( Infected *me )
{
	me->GetBodyInterface()->StartActivity( ACT_TERROR_IDLE_ALERT, IBody::MOTION_CONTROLLED_XY );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedAlert::OnAnimationActivityComplete( Infected *me, int activity )
{
	if ( activity == ACT_TERROR_ALERT_TO_NEUTRAL )
		return TryChangeTo( new InfectedWander );
	if ( activity == ACT_TERROR_NEUTRAL_TO_ALERT )
		me->GetBodyInterface()->StartActivity( ACT_TERROR_IDLE_ALERT, IBody::MOTION_CONTROLLED_XY );
	else if ( activity == ACT_TERROR_IDLE_ALERT_AHEAD || activity == ACT_TERROR_IDLE_ALERT_BEHIND ||
		activity == ACT_TERROR_IDLE_ALERT_LEFT || activity == ACT_TERROR_IDLE_ALERT_RIGHT )
		me->GetBodyInterface()->StartActivity( ACT_TERROR_ALERT_TO_NEUTRAL, IBody::MOTION_CONTROLLED_XY );
	return TryContinue();
}

#pragma float_control( precise, on, push )
EventDesiredResult< Infected > InfectedAlert::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
    // Native Alert ignores ALL non-player contacts here, unlike Wander's brawl path.
    if ( !other || !other->IsAlive() || !other->IsPlayer() )
        return TryContinue();
    if ( IsSurvivor( other ) )
    {
        if ( me->GetInfectedFlags() & 0x400 )
            return TryChangeTo( new InfectedFlee );
        if ( me->CanBecomeActive() )
            return TryChangeTo( new InfectedAttack( other ) );
        return TryContinue();
    }
    return TrySuspendFor( new InfectedShoved( other ) );
}
#pragma float_control( pop )

#pragma float_control( precise, on, push )
EventDesiredResult< Infected > InfectedAlert::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
    CBaseEntity *attacker = info.GetAttacker();
    if ( !attacker )
        return TryContinue();
    INextBot *bot = dynamic_cast< INextBot * >( attacker );
    if ( bot )
        return TryChangeTo( new InfectedAttack( bot->GetEntity() ) );
    // Valid nonbot attacker ANY team/type: no added survivor or alive test.
    if ( me->GetInfectedFlags() & 0x400 )
        return TryChangeTo( new InfectedFlee );
    return TryContinue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > InfectedAlert::OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	const ForcedSoundReaction reaction = SoundForcesAttack( me, source, keys, false );
	if ( reaction == REJECT_FORCED_REACTION )
		return TryToSustain();
	if ( reaction == ACCEPT_FORCED_REACTION )
		return TryChangeTo( new InfectedAttack( source ), RESULT_IMPORTANT, "Heard a sound that forced me to attack its source" );
	if ( !m_acquireTimer.HasStarted() && m_reorientTimer.IsElapsed() && m_soundTimer.IsElapsed() && IsSurvivor( source ) && source->IsAlive() &&
		( m_alertPosition - pos ).LengthSqr() > 2500.0f )
	{
		m_alertPosition = pos;
		m_reorientTimer.Start( RandomFloat( 0.3f, 2.0f ) );
	}
	return TryToSustain();
}

EventDesiredResult< Infected > InfectedAlert::OnShoved( Infected *me, CBaseEntity *pusher )
{
	if ( me->IsShoveImmune() || !IsSurvivor( pusher ) || FacingDot( me, pusher ) > -0.3f )
		return TrySuspendFor( new InfectedShoved( pusher ) );
	me->ShoveKill( pusher );
	return TryContinue();
}

InfectedShoved::InfectedShoved( CBaseEntity *pusher, Action< Infected > *successor ) :
	m_pusher( pusher ), m_shoveFrom( pusher ? pusher->WorldSpaceCenter() : vec3_origin ), m_successor( successor )
{
}

InfectedShoved::~InfectedShoved()
{
	delete m_successor;
}

bool InfectedShoved::StartShoveActivity( Infected *me )
{
	IBody *body = me->GetBodyInterface();
	Vector to = m_shoveFrom - me->GetAbsOrigin();
	to.z = 0.0f;
	to.NormalizeInPlace();
	const Vector forward = ForwardVector( me );
	const float frontDot = DotProduct( to, forward );
	const bool standing = body->IsActualPosture( IBody::STAND );
	const bool sitting = body->IsActualPosture( IBody::SIT );
	if ( !standing && !sitting )
		return false;
	Activity activity, seated = ACT_INVALID;
	Vector facing;
	if ( frontDot > 0.70709997f )
	{
		activity = ACT_TERROR_SHOVED_BACKWARD;
		seated = ACT_TERROR_SHOVED_BACKWARD_FROM_SIT;
		facing = to;
	}
	else if ( frontDot < -0.70709997f )
	{
		activity = ACT_TERROR_SHOVED_FORWARD;
		facing = -to;
	}
	else if ( -forward.y * to.x + forward.x * to.y > 0.70709997f )
	{
		activity = ACT_TERROR_SHOVED_RIGHTWARD;
		seated = ACT_TERROR_SHOVED_RIGHTWARD_FROM_SIT;
		facing.Init( to.y, -to.x, 0 );
	}
	else
	{
		activity = ACT_TERROR_SHOVED_LEFTWARD;
		seated = ACT_TERROR_SHOVED_LEFTWARD_FROM_SIT;
		facing.Init( -to.y, to.x, 0 );
	}
	if ( standing || seated == ACT_INVALID )
	{
		if ( !body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY ) )
			return false;
		body->SetDesiredPosture( IBody::STAND );
	}
	else
	{
		if ( !body->StartActivity( seated, IBody::MOTION_CONTROLLED_XY ) )
		{
			if ( !body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY ) )
				return false;
			body->SetDesiredPosture( IBody::STAND );
		}
		// Original also requests SIT after a successful standing fallback.
		body->SetDesiredPosture( IBody::SIT );
	}
	QAngle angles;
	VectorAngles( facing, angles );
	me->SetAbsAngles( angles );
	return true;
}

ActionResult< Infected > InfectedShoved::OnStart( Infected *me, Action< Infected > *priorAction )
{
	if ( me->GetInfectedFlags() & 0x400 )
	{
		CBaseEntity *pusher = m_pusher.Get();
		if ( IsSurvivor( pusher ) )
		{
			delete m_successor;
			m_successor = new InfectedFlee;
		}
	}
	if ( me->GetLocomotionInterface()->IsClimbingOrJumping() || !StartShoveActivity( me ) )
	{
		if ( !m_successor )
			return Done();
		Action< Infected > *successor = m_successor;
		m_successor = NULL;
		return ChangeTo( successor );
	}
	me->EmitSound( "Weapon.HitInfected" );
	me->EmitVocalization( "Zombie.Shoved", true );
	return Continue();
}

EventDesiredResult< Infected > InfectedShoved::OnAnimationActivityComplete( Infected *me, int activity )
{
	const bool fromSit = activity == ACT_TERROR_SHOVED_BACKWARD_FROM_SIT || activity == ACT_TERROR_SHOVED_RIGHTWARD_FROM_SIT || activity == ACT_TERROR_SHOVED_LEFTWARD_FROM_SIT;
	const bool intoWall = activity == ACT_TERROR_SHOVED_BACKWARD_INTO_WALL || activity == ACT_TERROR_SHOVED_FORWARD_INTO_WALL ||
		activity == ACT_TERROR_SHOVED_LEFTWARD_INTO_WALL || activity == ACT_TERROR_SHOVED_RIGHTWARD_INTO_WALL;
	if ( !fromSit && !intoWall && !IsShoveActivity( activity ) )
		return TryContinue();
	if ( fromSit )
		me->GetBodyInterface()->SetDesiredPosture( IBody::STAND );
	if ( !m_successor )
		return TryDone();
	Action< Infected > *successor = m_successor;
	m_successor = NULL;
	return TryChangeTo( successor );
}

EventDesiredResult< Infected > InfectedShoved::OnShoved( Infected *me, CBaseEntity *pusher )
{
	StartShoveActivity( me );
	return TryToSustain();
}

EventDesiredResult< Infected > InfectedShoved::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	const float speed = me->GetAbsVelocity().Length();
	if ( speed > z_shove_friend_speed.GetFloat() && other && other->MyNextBotPointer() )
		other->MyNextBotPointer()->OnShoved( me );
	if ( speed > 50.0f && trace && trace->DidHit() && !trace->startsolid && trace->plane.normal.z < 0.5f )
	{
		IBody *body = me->GetBodyInterface();
		Activity activity = body->GetActivity();
		if ( activity == ACT_TERROR_SHOVED_BACKWARD )
			body->StartActivity( ACT_TERROR_SHOVED_BACKWARD_INTO_WALL, IBody::MOTION_CONTROLLED_XY );
		else if ( activity == ACT_TERROR_SHOVED_FORWARD )
			body->StartActivity( ACT_TERROR_SHOVED_FORWARD_INTO_WALL, IBody::MOTION_CONTROLLED_XY );
		else if ( activity == ACT_TERROR_SHOVED_LEFTWARD )
			body->StartActivity( ACT_TERROR_SHOVED_LEFTWARD_INTO_WALL, IBody::MOTION_CONTROLLED_XY );
		else if ( activity == ACT_TERROR_SHOVED_RIGHTWARD )
			body->StartActivity( ACT_TERROR_SHOVED_RIGHTWARD_INTO_WALL, IBody::MOTION_CONTROLLED_XY );
	}
	return TryToSustain();
}

InfectedAttack::InfectedAttack( CBaseEntity *victim ) : m_victim( victim ), m_playerVictim( victim && victim->IsPlayer() )
{
}

ActionResult< Infected > InfectedAttack::OnStart( Infected *me, Action< Infected > *priorAction )
{
	SetEnragedExpression( me );
	IBody *body = me->GetBodyInterface();
	if ( !body->IsArousal( IBody::INTENSE ) )
	{
		body->SetArousal( IBody::INTENSE );
		me->EmitVocalization( "Zombie.BecomeEnraged", true );
	}
	if ( body->IsActualPosture( IBody::SIT ) || body->IsActualPosture( IBody::LIE ) )
		return SuspendFor( new InfectedChangePosture( IBody::STAND ) );
	return Continue();
}

Action< Infected > *InfectedAttack::InitialContainedAction( Infected *me )
{
	return new ChaseVictim( this );
}

ActionResult< Infected > InfectedAttack::Update( Infected *me, float interval )
{
	if ( !GetActiveChildAction() || ( GetVictim() && GetVictim()->IsPlayer() && !IsSurvivor( GetVictim() ) ) )
	{
		if ( m_playerVictim )
			return ChangeTo( new InfectedAlert( me->GetPosition() + 100.0f * me->GetLocomotionInterface()->GetMotionVector() ) );
		me->GetVisionInterface()->ClearRecognizedVictim();
		return ChangeTo( new InfectedWander );
	}
	return Continue();
}

ActionResult< Infected > InfectedAttack::OnSuspend( Infected *me, Action< Infected > *interruptingAction )
{
	me->RemoveGesture( ACT_TERROR_ATTACK );
	return Continue();
}

EventDesiredResult< Infected > InfectedAttack::OnLeaveGround( Infected *me, CBaseEntity *ground )
{
	me->RemoveGesture( ACT_TERROR_ATTACK );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedAttack::OnLandOnGround( Infected *me, CBaseEntity *ground )
{
	if ( ground && ground->IsPlayer() )
		return TrySuspendFor( new InfectedShoved( ground ), RESULT_CRITICAL, "Landed on a player's head" );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedAttack::OnShoved( Infected *me, CBaseEntity *pusher )
{
	me->RemoveUncommonChaseGoals();
	return TrySuspendFor( new InfectedShoved( pusher ), RESULT_IMPORTANT );
}

EventDesiredResult< Infected > InfectedAttack::OnSound( Infected *me, CBaseEntity *source, const Vector &pos, KeyValues *keys )
{
	if ( SoundForcesAttack( me, source, keys, false ) == ACCEPT_FORCED_REACTION )
		SetVictim( source );
	return TryToSustain();
}

void InfectedAttack::OnPunch( Infected *me, CBaseEntity *victim )
{
    if ( !victim )
        return;
    me->RemoveUncommonChaseGoals();
    if ( !me->IsRangeLessThan( victim, z_attack_max_range.GetFloat() ) )
    {
        me->EmitSound( "Zombie.AttackMiss" );
        return;
    }
    Vector direction = me->EyePosition();
    trace_t trace;
    NextBotTraceFilterIgnoreActors filter( victim, COLLISION_GROUP_NONE );
    UTIL_TraceLine( direction, victim->WorldSpaceCenter(), infectedTraceMask, &filter, &trace );
    if ( !( trace.fraction >= 1.0f ) || trace.startsolid )
        return;
    if ( victim->GetGender() == 16 || victim->GetGender() == 17 )
        me->OnShoved( victim );
    AngleVectors( me->EyeAngles(), &direction );
    if ( FClassnameIs( victim, "func_door*" ) || FClassnameIs( victim, "prop_door*" ) )
    {
        CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( victim );
        if ( door )
            door->PlayPoundSound();
        else
            victim->EmitSound( "WoodenDoor.Pound" );
        CTakeDamageInfo info( me, me, z_door_pound_damage.GetFloat(), DMG_CLUB );
        CalculateMeleeDamageForce( &info, direction, me->Weapon_ShootPosition(), 1.0f );
        victim->TakeDamage( info );
        return;
    }
    float damage;
    Infected *infected = victim->MyInfectedPointer();
    if ( infected )
        damage = infected->IsIT() ? z_attack_infected_it_damage.GetFloat() : z_breakable_damage.GetFloat();
    else if ( victim->IsPlayer() )
        damage = TerrorGameRules()->GetCommonAttackDamage( ToBasePlayer( victim ) );
    else
        damage = z_breakable_damage.GetFloat();
    CTakeDamageInfo info( me, me, damage, DMG_CLUB );
    CalculateMeleeDamageForce( &info, direction, me->Weapon_ShootPosition(), 1.0f );
    if ( FClassnameIs( victim, "func_breakable_surf" ) )
        info.SetDamageType( DMG_SLASH );
    victim->TakeDamage( info );
    if ( victim->IsPlayer() )
    {
        if ( IsCommonAttackingUnderDisplacement( me ) )
        {
            TerrorNavArea *area = ToTerrorNavArea( me->GetLastKnownArea() );
            if ( area )
            {
                // Native signed INC then clamp-negative; avoid C++ signed-overflow UB.
                int population = (int)( (unsigned int)area->GetWanderingPopulation() + 1u );
                area->SetWanderingPopulation( population < 0 ? 0 : population );
            }
            UTIL_Remove( me );
        }
        else if ( god.GetInt() == 0 )
        {
            CBasePlayer *player = ToBasePlayer( victim );
            CSoundParameters parameters;
            if ( CBaseEntity::GetParametersForSound( "Zombie.Punch", parameters, NULL ) )
            {
                CRecipientFilter others;
                others.AddAllPlayers();
                others.RemoveRecipient( player );
                CBaseEntity::EmitSound( others, player->entindex(), "Zombie.Punch" );
            }
            // Native2418D0 is the existing CCSPlayer single-user emitter.
            // Main's minimal-skip rule: a non-Terror target cannot supply the
            // reached player subclass methods; do not invent feedback state.
            CTerrorPlayer *terrorPlayer = ToTerrorPlayer( player );
            if ( terrorPlayer )
            {
                terrorPlayer->EmitPrivateSound( "Player.HitInternal" );
                if ( me->GetGender() == 12 || me->GetGender() == 17 )
                    terrorPlayer->AddTerrorSplatter( 1, 0.0f, 3 );
            }
        }
    }
    else
        victim->EmitSound( victim->MyCombatCharacterPointer() ? "Zombie.Punch" : "Zombie.ClawScrape" );
}

InfectedChasePath::InfectedChasePath( SubjectChaseType chaseHow ) :
	TerrorDirectChasePath( chaseHow )
{
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
// S4115B0 uses signed FILD after the wrapping 32-bit sum, and compares the
// unspilled x87 free-width expression before selecting its F32 store.
void InfectedChasePath::ComputeAreaCrossing( INextBot *bot, const CNavArea *from, const Vector &fromPos, const CNavArea *to, NavDirType dir, Vector *crossPos ) const
{
	float halfWidth;
	from->ComputePortal( to, dir, crossPos, &halfWidth );
	if ( halfWidth > 50.0f )
		halfWidth = 50.0f;
	const bool northSouthOnly = to->GetAdjacentCount( NORTH ) > 0 && to->GetAdjacentCount( SOUTH ) > 0 &&
		to->GetAdjacentCount( EAST ) == 0 && to->GetAdjacentCount( WEST ) == 0;
	const bool eastWestOnly = to->GetAdjacentCount( NORTH ) == 0 && to->GetAdjacentCount( SOUTH ) == 0 &&
		to->GetAdjacentCount( EAST ) > 0 && to->GetAdjacentCount( WEST ) > 0;
	if ( ( northSouthOnly && to->GetSizeX() < 52.499996f ) || ( eastWestOnly && to->GetSizeY() < 52.499996f ) )
		return;
	const int seed = (int)( (unsigned int)bot->GetEntity()->entindex() + from->GetID() + to->GetID() );
	const float spread = (float)cos( (double)seed );
	const double freeWidth = (double)halfWidth - (double)bot->GetBodyInterface()->GetHullWidth() * 0.5;
	halfWidth = freeWidth < 0.0 ? 0.0f : (float)freeWidth;
	if ( dir == NORTH || dir == SOUTH )
		crossPos->x += halfWidth * spread;
	else
		crossPos->y += halfWidth * spread;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif


ChaseVictim::ChaseVictim( InfectedAttack *attack ) : m_attack( attack )
{
}

#pragma float_control( precise, on, push )
ActionResult< Infected > ChaseVictim::OnStart( Infected *me, Action< Infected > *priorAction )
{
    if ( me->GetGender() == 16 || me->GetGender() == 17 )
    {
        CBaseEntity *victim = m_attack->GetVictim();
        // Native412884 JBE skips on <= OR unordered; only ordered > creates.
        if ( !victim || (double)me->GetRangeSquaredTo( victim ) > 129600.0 )
            me->CreateUncommonChaseGoal();
    }
    // Native INextBot slot264 ->2999B0, NOT locomotion ClearStuckStatus.
    me->ClearImmobileStatus();
    return Continue();
}
#pragma float_control( pop )

ActionResult< Infected > ChaseVictim::OnSuspend( Infected *me, Action< Infected > *interruptingAction )
{
	me->GetLocomotionInterface()->SetVelocity( vec3_origin );
	return Continue();
}

ActionResult< Infected > ChaseVictim::OnResume( Infected *me, Action< Infected > *interruptingAction )
{
	me->ClearImmobileStatus();
	return Continue();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
ActionResult< Infected > ChaseVictim::Update( Infected *me, float interval )
{
	CBaseEntity *victim = m_attack->GetVictim();
	// S4129D7: bile overrides the chase destination, not the containing
	// attack's handle. An active pipe bomb retains priority over IT.
	if ( !victim || !FClassnameIs( victim, "pipe_bomb*" ) )
	{
		CBaseCombatCharacter *itVictim = FindClosestCharacterWhoIsIT( me );
		if ( itVictim )
			victim = itVictim;
	}
	if ( !victim )
		return ChangeTo( new LostVictim, "Victim pointer is NULL" );
	if ( victim->MyCombatCharacterPointer() && !victim->IsAlive() )
		return ChangeTo( new LostVictim, "Victim died" );
	ZombieBotLocomotion *mover = me->GetLocomotionInterface();
	IBody *body = me->GetBodyInterface();
	body->AimHeadTowards( victim, IBody::IMPORTANT, 0.1f, NULL, "Watch our victim" );
	const bool canMelee = mover->IsOnGround() && !mover->IsClimbingOrJumping() && victim->GetAbsOrigin().z <= me->EyePosition().z;
	if ( canMelee && victim->GetAbsVelocity().LengthSqr() < 2500.0f && me->IsRangeLessThan( victim, z_attack_min_range.GetFloat() ) )
	{
		mover->SetAcceleration( vec3_origin );
		mover->SetVelocity( vec3_origin );
		return SuspendFor( new PunchVictim( m_attack, victim ) );
	}
	if ( canMelee && me->IsRangeLessThan( victim, z_attack_on_the_run_range.GetFloat() ) &&
		m_movingAttackTimer.IsElapsed() && !me->IsPlayingGesture( ACT_TERROR_ATTACK ) && me->GetVisionInterface()->IsAbleToSee( victim, IVision::DISREGARD_FOV ) )
	{
		me->AddGesture( ACT_TERROR_ATTACK, true );
		m_movingAttackTimer.Start( z_attack_interval.GetFloat() );
	}
	CBaseCombatCharacter *recognized = me->GetVisionInterface()->GetPrimaryKnownThreat();
	if ( victim->IsPlayer() && !ToTerrorPlayer( victim )->IsIT() &&
		recognized && recognized != victim && IsValidVictim( recognized ) )
	{
		// S413151 spills the candidate to F32 before the second, unspilled range call.
		const float candidateRangeSquared = static_cast< float >( me->GetRangeSquaredTo( recognized ) );
		const double victimRangeSquared = me->GetRangeSquaredTo( victim );
		if ( victimRangeSquared > candidateRangeSquared &&
			me->IsRangeLessThan( recognized, z_attack_change_target_range.GetFloat() ) )
		{
			victim = recognized;
			m_attack->SetVictim( victim );
		}
	}
	mover->Run();
	InfectedPathCost cost( me );
	m_path.Update( me, victim, cost );
	me->UpdateCommonApproachSteering( victim, &m_path );
	if ( !mover->IsClimbingOrJumping() && mover->IsOnGround() )
	{
		if ( m_path.IsReachable() )
		{
			const Activity run = ( me->GetInfectedFlags() & 4 ) ? ACT_TERROR_CRAWL_RUN :
				( body->IsActualPosture( IBody::STAND ) ? ACT_TERROR_RUN_INTENSE : ACT_TERROR_CROUCH_RUN_INTENSE );
			if ( !body->IsActivity( run ) )
				body->StartActivity( run );
		}
		else
		{
			if ( !body->IsActivity( ACT_TERROR_UNABLE_TO_REACH_TARGET ) )
				body->StartActivity( ACT_TERROR_UNABLE_TO_REACH_TARGET, IBody::MOTION_CONTROLLED_XY );
			QAngle facing;
			VectorAngles( victim->GetAbsOrigin() - me->GetAbsOrigin(), facing );
			QAngle angles = me->GetAbsAngles();
			angles.y = facing.y;
			me->SetAbsAngles( angles );
		}
	}
	if ( m_voiceTimer.IsElapsed() )
	{
		me->EmitVocalization( recognized ? "Zombie.RageAtVictim" : "Zombie.Rage" );
		m_voiceTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	}
	return Continue();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#pragma float_control( precise, on, push )
// S4EA090 keeps the length wide, but rounds before the reciprocal.
static double NormalizeITPushawayVector( Vector &vector )
{
	const double length = sqrt( ( static_cast< double >( vector.x ) * vector.x +
		static_cast< double >( vector.y ) * vector.y ) +
		static_cast< double >( vector.z ) * vector.z );
	const float roundedLength = static_cast< float >( length );
	vector *= 1.0f / ( roundedLength + FLT_EPSILON );
	return length;
}

EventDesiredResult< Infected > ChaseVictim::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	// S411AF0: a closed usable door interrupts the chase, not its victim.
	if ( other && ( FClassnameIs( other, "prop_door*" ) || FClassnameIs( other, "func_door*" ) ) &&
		me->IsUseableEntity( other ) )
	{
		CBasePropDoor *propDoor = dynamic_cast< CBasePropDoor * >( other );
		if ( propDoor )
		{
			if ( propDoor->IsDoorClosed() )
				return TrySuspendFor( new PunchVictim( m_attack, propDoor ), RESULT_IMPORTANT );
			if ( propDoor->IsDoorOpen() && me->GetLocomotionInterface()->IsStuck() )
			{
				CTakeDamageInfo info( me, me, 2000.0f, DMG_CLUB );
				ILocomotion *mover = me->GetLocomotionInterface();
				const Vector position = me->Weapon_ShootPosition();
				CalculateMeleeDamageForce( &info, mover->GetGroundMotionVector(), position, 1.0f );
				propDoor->TakeDamage( info );
			}
		}
		else
		{
			CBaseDoor *brushDoor = dynamic_cast< CBaseDoor * >( other );
			if ( brushDoor )
			{
				if ( brushDoor->IsDoorLocked() )
					return TrySuspendFor( new PunchVictim( m_attack, brushDoor ), RESULT_IMPORTANT );
				if ( m_doorUseTimer.IsElapsed() )
				{
					m_doorUseTimer.Start( 1.0f );
					me->UseEntity( brushDoor, USE_TOGGLE );
				}
			}
		}
	}
	CTerrorPlayer *player = ToTerrorPlayer( other );
	CBaseEntity *victim = m_attack->GetVictim();
	if ( player && victim && victim->IsPlayer() && IsSurvivor( player ) &&
		ToTerrorPlayer( victim )->IsIT() && victim->entindex() != player->entindex() &&
		player->CanPerformBackpackAction() )
	{
		// S411D91: don't steal an IT victim on contact. Push the other
		// survivor aside without reducing its current speed.
		Vector push = player->GetAbsOrigin() - me->GetAbsOrigin();
		NormalizeITPushawayVector( push );
		push.z += 0.33f;
		push *= z_pushaway_force.GetFloat();
		const Vector oldVelocity = player->GetAbsVelocity();
		Vector velocity = oldVelocity + push;
		const float oldSpeed = oldVelocity.Length();
		const double length = NormalizeITPushawayVector( velocity );
		const float cappedSpeed = length <= 220.0 ? static_cast< float >( length ) : 220.0f;
		velocity *= oldSpeed > cappedSpeed ? oldSpeed : cappedSpeed;
		player->SetAbsVelocity( velocity );
	}
	else if ( player && player->GetTeamNumber() == TEAM_SURVIVOR && IsValidVictim( player ) )
		m_attack->SetVictim( player );
	return TryContinue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > ChaseVictim::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	if ( IsValidVictim( victim ) )
		m_attack->SetVictim( victim );
	return TryContinue();
}

EventDesiredResult< Infected > ChaseVictim::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == AE_ATTACK_HIT )
		m_attack->OnPunch( me, m_attack->GetVictim() );
	return TryContinue();
}

PunchVictim::PunchVictim( InfectedAttack *attack, CBaseEntity *victim )
{
    // Native411700: countdown56, interval68, countdown76, invalid handle88
    // construct first; then attack52 and the real handle assignment.
    m_attack = attack;
    m_victim = victim;
}

ActionResult< Infected > PunchVictim::OnStart( Infected *me, Action< Infected > *priorAction )
{
    const int destroy = z_destroy_on_attack.GetInt();
    if ( destroy == 1 )
        UTIL_Remove( me );
    else if ( destroy == 2 )
    {
        CBaseEntity *victim = m_victim.Get();
        CBaseEntity *source = victim ? victim : me;
        CTakeDamageInfo info( source, source, 1000.0f, DMG_BULLET );
        me->TakeDamage( info );
    }
    m_started.Start();
    return Continue();
}

#pragma float_control( precise, on, push )
ActionResult< Infected > PunchVictim::Update( Infected *me, float interval )
{
    CBaseEntity *victim = m_victim.Get();
    if ( !victim )
        return Done();
    ZombieBotBody *headBody = me->GetBodyInterface();
    // Main's minimal-NULL-skip rule: native413430's head-aim receiver is
    // dereferenced before the later defined body-null Continue branch.
    if ( headBody )
        headBody->AimHeadTowards( victim, IBody::IMPORTANT, 0.1f, NULL, "Watch our victim" );
    if ( gpGlobals->curtime >= m_voiceTimer.m_timestamp )
    {
        me->EmitVocalization( "Zombie.RageAtVictim" );
        m_voiceTimer.Start( RandomFloat( 1.0f, 2.0f ) );
    }
    if ( !victim->IsPlayer() )
    {
        if ( victim->IsSolidFlagSet( FSOLID_NOT_SOLID ) )
            return Done();
    }
    else if ( victim->IsAlive() )
        m_deadVictimTimer.Start( RandomFloat( 1.0f, 2.0f ) );
    else if ( gpGlobals->curtime >= m_deadVictimTimer.m_timestamp )
        return Done();
    CTerrorPlayer *player = victim->IsPlayer() ? ToTerrorPlayer( victim ) : NULL;
    if ( me->IsRangeGreaterThan( victim, z_attack_max_range.GetFloat() ) )
        return Done();
    IBody *body = me->GetBodyInterface();
    if ( !body )
        return Continue();
    ILocomotion *mover = me->GetLocomotionInterface();
    if ( victim->IsPlayer() )
    {
        me->RemoveUncommonChaseGoals();
        if ( player && !me->IsPlayingGesture( ACT_TERROR_ATTACK ) )
        {
            // Native2020 incap AND !13296 actual tongue-hanging byte selects LOW.
            const Activity activity = player->IsIncapacitated() && !player->m_isHangingFromTongue ?
                ACT_TERROR_ATTACK_LOW_CONTINUOUSLY : ACT_TERROR_ATTACK_CONTINUOUSLY;
            if ( !body->IsActivity( activity ) )
                body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY );
        }
    }
    else if ( FClassnameIs( victim, "func_door*" ) || FClassnameIs( victim, "prop_door*" ) )
    {
        CBasePropDoor *door = dynamic_cast< CBasePropDoor * >( victim );
        const Activity activity = door && door->HasUnlockSequence() ?
            ACT_TERROR_REACH_THROUGH_DOOR : ACT_TERROR_ATTACK_DOOR_CONTINUOUSLY;
        if ( !body->IsActivity( activity ) )
            body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY );
        if ( mover )
        {
            Vector mins, maxs;
            victim->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
            const float width = maxs.x - mins.x;
            const float depth = maxs.y - mins.y;
            Vector face = me->GetPosition();
            if ( !( fabs( width ) > fabs( depth ) ) )
                face.x += victim->WorldSpaceCenter().x > me->GetPosition().x ? 1000.0f : -1000.0f;
            else
                face.y += victim->WorldSpaceCenter().y > me->GetPosition().y ? 1000.0f : -1000.0f;
            mover->FaceTowards( face );
        }
        return Continue();
    }
    else
    {
        INextBot *bot = dynamic_cast< INextBot * >( victim );
        bool low;
        if ( bot )
        {
            IBody *victimBody = bot->GetBodyInterface();
            low = !victimBody || !victimBody->IsActualPosture( IBody::STAND );
        }
        else
        {
            ILocomotion *ownMover = me->GetLocomotionInterface();
            // Native413886 dereferences this locomotion receiver. With a NULL
            // provider, skip only selection/facing; keep the elapsed-time exit.
            if ( !ownMover )
                return m_started.GetElapsedTime() > 3.0f ? Done() : Continue();
            // Native COMISS/JBE chooses standing on unordered, too.
            low = ownMover->GetFeet().z > victim->WorldSpaceCenter().z;
        }
        const Activity activity = low ? ACT_TERROR_ATTACK_LOW_CONTINUOUSLY : ACT_TERROR_ATTACK_CONTINUOUSLY;
        if ( !body->IsActivity( activity ) )
            body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY );
        if ( !bot && m_started.GetElapsedTime() > 3.0f )
            return Done();
    }
    if ( mover )
        mover->FaceTowards( victim->WorldSpaceCenter() );
    return Continue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > PunchVictim::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == AE_ATTACK_HIT )
	{
		m_attack->OnPunch( me, m_victim.Get() );
		return TryToSustain();
	}
	return TryContinue();
}

InfectedDying::InfectedDying( const CTakeDamageInfo &info ) : m_info( info ), m_yawDrift( 0.0f )
{
	m_optionalEnds = !info.GetAttacker() || info.GetAttacker()->IsPlayer() ? RandomInt( 0, 10 ) : 999;
}

// Replace existing InfectedDying::SelectDeathActivity in infected_behavior.cpp.
// Native S415D80; protected virtual seam is shared with actual WitchDying.
// This is not an Infected::GetDeathActivity facade.
#pragma float_control( precise, on, push )
bool InfectedDying::SelectDeathActivity( Infected *me )
{
	const int damageType = m_info.GetDamageType();
	CBaseEntity *attacker = m_info.GetAttacker();
	CBasePlayer *player = ToBasePlayer( attacker );
	if ( player && player->GetTeamNumber() == TEAM_INFECTED && player->GetZombieClass() == 8 )
		return false;
	Vector force = m_info.GetDamageForce();
	if ( (unsigned int)damageType & 0x01000000u )
	{
		force *= 20.0f;
		m_info.SetDamageForce( force );
	}
	if ( (unsigned int)damageType & 0x01000040u )
		return false;
	if ( (unsigned int)damageType & 0x20000000u )
	{
		force *= 10.0f;
		m_info.SetDamageForce( force );
	}
	if ( player && me->LastHitGroup() == HITGROUP_HEAD && !( damageType & ( DMG_CLUB | DMG_SLASH ) ) )
	{
		m_info.SetDamageForce( vec3_origin );
		Vector head;
		QAngle angles;
		// S415D80: failed Head lookup skips only this undefined limb-force write.
		if ( me->GetAttachment( "Head", head, angles ) )
			me->m_gibbedLimbForce = attacker->EyePosition() - head;
		return false;
	}
	IBody *body = me->GetBodyInterface();
	ZombieBotLocomotion *mover = me->GetLocomotionInterface();
	if ( ( IsShoveActivity( body->GetActivity() ) || IsMeleeDeathActivity( body->GetActivity() ) ) &&
		me->GetAbsVelocity().LengthSqr() > 100.0f )
		return false;
	if ( !mover->IsOnGround() || mover->IsClimbingOrJumping() ||
		!body->IsActualPosture( IBody::STAND ) || body->IsPostureChanging() )
		return false;
	const int pelvis = me->LookupBone( "ValveBiped.bip01_pelvis" );
	if ( pelvis >= 0 )
	{
		Vector pos;
		QAngle angles;
		me->GetBonePosition( pelvis, pos, angles );
		if ( pos.z - me->GetAbsOrigin().z < 24.0f )
			return false;
	}
	Activity activity = ACT_INVALID;
	for ( int slot = 0; slot < 2; ++slot )
	{
		const char *death = me->GetWoundDeathActivity( slot );
		if ( !death || !death[0] )
			continue;
		if ( !Q_strcmp( death, "none" ) )
			return false;
		if ( activity == ACT_INVALID )
		{
			activity = static_cast< Activity >( ActivityList_IndexForName( death ) );
			m_yawDrift = 0.0f;
		}
	}
	Vector forward, right;
	AngleVectors( me->GetAbsAngles(), &forward, &right, NULL );
	if ( activity == ACT_INVALID && ( (unsigned int)damageType & 0x20000000u ) &&
		player && me->IsRangeLessThan( player, 500.0f ) )
	{
		Vector direction = force;
		VectorNormalize( direction );
		const float frontDot = DotProduct( direction, forward );
		const float sideDot = DotProduct( direction, right );
		if ( -frontDot > 0.70700002f ) activity = ACT_TERROR_DIE_BACKWARD_FROM_SHOTGUN;
		else if ( frontDot > 0.70700002f ) activity = ACT_TERROR_DIE_FORWARD_FROM_SHOTGUN;
		else if ( -sideDot > 0.70700002f ) activity = ACT_TERROR_DIE_LEFTWARD_FROM_SHOTGUN;
		else if ( sideDot > 0.70700002f ) activity = ACT_TERROR_DIE_RIGHTWARD_FROM_SHOTGUN;
		if ( activity != ACT_INVALID )
			m_yawDrift = 0.0f;
	}
	if ( activity == ACT_INVALID && ( damageType & ( DMG_CLUB | DMG_SLASH ) ) && player )
	{
		Vector direction = m_info.GetDamageDirection();
		direction.z = 0.0f;
		VectorNormalize( direction );
		const float frontDot = DotProduct( direction, forward );
		const float sideDot = DotProduct( direction, right );
		const bool blunt = ( damageType & DMG_CLUB ) != 0;
		if ( -frontDot > 0.70700002f ) activity = blunt ? ACT_BLUDGEON_DEATH_BACK : ACT_SLICING_DEATH_BACK;
		else if ( frontDot > 0.70700002f ) activity = blunt ? ACT_BLUDGEON_DEATH_FORWARD : ACT_SLICING_DEATH_FORWARD;
		else if ( -sideDot > 0.70700002f ) activity = blunt ? ACT_BLUDGEON_DEATH_LEFT : ACT_SLICING_DEATH_LEFT;
		else if ( sideDot > 0.70700002f ) activity = blunt ? ACT_BLUDGEON_DEATH_RIGHT : ACT_SLICING_DEATH_RIGHT;
		if ( activity != ACT_INVALID )
			m_yawDrift = 0.0f;
	}
	if ( activity == ACT_INVALID )
	{
		if ( me->GetAbsVelocity().LengthSqr() > 22500.0f )
		{
			activity = ACT_TERROR_DIE_WHILE_RUNNING;
			m_yawDrift = RandomFloat( -70.0f, 70.0f );
		}
		else
		{
			activity = ACT_TERROR_DIE_FROM_STAND;
			m_yawDrift = 0.0f;
		}
	}
	if ( !body->StartActivity( activity, IBody::MOTION_CONTROLLED_XY | IBody::ACTIVITY_UNINTERRUPTIBLE ) )
		return false;
	m_info.SetDamageForce( vec3_origin );
	return true;
}
#pragma float_control( pop )

ActionResult< Infected > InfectedDying::OnStart( Infected *me, Action< Infected > *priorAction )
{
	if ( me->m_lifeState == LIFE_ALIVE )
		me->Event_Killed( m_info );
	NotifyZombieDeath( me, 0 );
	me->RemoveAllGestures();
	const char *sound = "Zombie.Die";
	if ( !SelectDeathActivity( me ) )
	{
		if ( me->LastHitGroup() == HITGROUP_HEAD )
		{
			SetBodygroupExpression( me, "Head", "Head", 'D' );
			SetBodygroupExpression( me, "UpperBody", "Head", 'D' );
			sound = "Zombie.HeadlessCough";
		}
		me->Retire( m_info );
	}
	me->EmitSound( sound );
	return Continue();
}

ActionResult< Infected > InfectedDying::Update( Infected *me, float interval )
{
	QAngle angles = me->GetLocalAngles();
	angles.y += m_yawDrift * interval;
	me->SetLocalAngles( angles );
	return Continue();
}

EventDesiredResult< Infected > InfectedDying::OnAnimationActivityComplete( Infected *me, int activity )
{
	if ( activity == ACT_TERROR_DIE_FROM_STAND || activity == ACT_TERROR_DIE_WHILE_RUNNING ||
		activity == ACT_TERROR_DIE_BACKWARD_FROM_SHOTGUN || activity == ACT_TERROR_DIE_FORWARD_FROM_SHOTGUN ||
		activity == ACT_TERROR_DIE_LEFTWARD_FROM_SHOTGUN || activity == ACT_TERROR_DIE_RIGHTWARD_FROM_SHOTGUN || IsMeleeDeathActivity( activity ) )
		me->Retire( CTakeDamageInfo() );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedDying::OnAnimationEvent( Infected *me, animevent_t *event )
{
	if ( event->event == AE_OPTIONAL_END && --m_optionalEnds < 0 )
		me->Retire( CTakeDamageInfo() );
	return TryContinue();
}

void InfectedDying::ApplySceneDamage( Infected *me, const Vector &pos )
{
	Vector center = me->WorldSpaceCenter();
	Vector direction = center - pos;
	direction.NormalizeInPlace();
	CTakeDamageInfo info( me, me, direction * 3.0f, center, 50.0f, DMG_CLUB );
	CalculateMeleeDamageForce( &info, direction, pos, 1.0f );
	m_info = info; // original 415800 stores scene force; it does not TakeDamage
}

EventDesiredResult< Infected > InfectedDying::OnShoved( Infected *me, CBaseEntity *pusher )
{
	if ( pusher )
		ApplySceneDamage( me, pusher->GetAbsOrigin() );
	me->Retire( CTakeDamageInfo() );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedDying::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
	if ( other && !IsMeleeDeathActivity( me->GetBodyInterface()->GetActivity() ) && !other->MyNextBotPointer() )
	{
		if ( other->IsPlayer() )
			ApplySceneDamage( me, other->WorldSpaceCenter() );
		me->Retire( CTakeDamageInfo() );
	}
	return TryContinue();
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

namespace
{
// S40F480/40F4D0. Each path computation draws its own 0..0.75 preference,
// separate from the actor's constructor random value. Valid edges also make
// their native 0.8..1.2 draw; neither draw belongs on a cached weapon stream.
class InfectedAmbientMobPathCost : public IPathCost
{
public:
	explicit InfectedAmbientMobPathCost( Infected *me ) :
		m_locomotion( me->GetLocomotionInterface() ), m_preference( RandomFloat( 0.0f, 0.75f ) ) {}
	virtual float operator()( CNavArea *area, CNavArea *from, const CNavLadder *ladder,
		const CFuncElevator *elevator, float length ) const OVERRIDE
	{
		if ( !m_locomotion->IsAreaTraversable( area ) )
			return -1.0f;
		if ( !from )
			return 0.0f;
		const float preference = 1.0f - m_preference;
		float distance;
		if ( ladder )
		{
			float multiplier = ( preference + 1.0f ) * 5.0f;
			if ( from->GetCenter().z > area->GetCenter().z )
				multiplier *= 3.0f;
			distance = ladder->m_length * multiplier;
		}
		else
		{
			float height = from->ComputeAdjacentConnectionHeightChange( area );
			if ( !( height <= m_locomotion->GetMaxJumpHeight() ) )
				return -1.0f;
			distance = length;
			if ( distance <= 0.0f )
				distance = ( area->GetCenter() - from->GetCenter() ).Length();
			if ( height < 0.0f )
				height = 0.0f;
			const float multiplier = height - 24.0f < 0.0f ? 1.0f : preference * 3.0f + 1.0f;
			const float rise = fabsf( multiplier * height );
			if ( rise > 3.0f )
				distance += rise;
			// Native base-area attribute84, not Terror spawn-attribute296.
			if ( area->HasAttributes( 0x00080000 ) )
				distance *= 10.0f;
		}
		return RandomFloat( 0.8f, 1.2f ) * distance + from->GetCostSoFar();
	}
private:
	ILocomotion *m_locomotion;
	float m_preference;
};

// S40F710,40F7B0,4103F0,410520. Native stores an ordinary map-entity
// pointer; do not change malformed-goal behavior with EHANDLE/null fallback.
class InfectedAmbientMob : public Action< Infected >
{
public:
	explicit InfectedAmbientMob( CBaseEntity *goal ) : m_goal( goal ) {}
	virtual const char *GetName() const OVERRIDE { return "InfectedAmbientMob"; }
	virtual ActionResult< Infected > OnStart( Infected *me, Action< Infected > *prior ) OVERRIDE
	{
		m_path.Invalidate();
		InfectedAmbientMobPathCost cost( me );
		m_path.Compute( me, m_goal->GetAbsOrigin(), cost );
		me->SetInfectedFlags( 0x80 );
		m_repathTimer.Start( 1.0f );
		return Continue();
	}
	virtual ActionResult< Infected > Update( Infected *me, float interval ) OVERRIDE
	{
		ILocomotion *locomotion = me->GetLocomotionInterface();
		IBody *body = me->GetBodyInterface();
		if ( m_path.IsValid() )
		{
			m_path.Update( me );
			if ( !locomotion->IsClimbingOrJumping() && !locomotion->IsUsingLadder() && locomotion->IsOnGround() )
			{
				if ( !body->IsActivity( ACT_TERROR_RUN_INTENSE ) )
					body->StartActivity( ACT_TERROR_RUN_INTENSE, 0 );
			}
			const Vector &goal = m_goal->GetAbsOrigin();
			const Vector &position = me->GetAbsOrigin();
			const float dx = position.x - goal.x;
			const float dy = position.y - goal.y;
			if ( dx * dx + dy * dy <= 576.0f )
				UTIL_Remove( me );
		}
		else if ( m_repathTimer.m_timestamp <= 0.0f ||
			(double)gpGlobals->curtime < (double)m_repathTimer.m_timestamp )
		{
			DevWarning( "Ambient Mob has no path to the destination!\n" );
		}
		else
		{
			InfectedAmbientMobPathCost cost( me );
			m_path.Compute( me, m_goal->GetAbsOrigin(), cost );
			m_repathTimer.Start( 2.0f );
		}
		return Continue();
	}
private:
	CountdownTimer m_repathTimer;
	CBaseEntity *m_goal;
	PathFollower m_path;
};
}

// S4144B0, actual root action virtual316: suspend for the ambient route at
// CRITICAL event priority. This is not the Vector/range debug-move overload.
EventDesiredResult< Infected > InfectedBehavior::OnCommandApproach( Infected *me, CBaseEntity *goal )
{
	return TrySuspendFor( new InfectedAmbientMob( goal ), RESULT_CRITICAL, "Running in ambient mob" );
}

// The canonical directly reached attack-command consumers from M4's
// infected_bile_actor_patch.txt sections1-2. ChaseVictim's existing native
// callback is intentionally unchanged; no extra survivor-only eligibility.
EventDesiredResult< Infected > InfectedWander::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	if ( victim && victim->IsAlive() )
		return TryChangeTo( new InfectedAttack( victim ) );
	return TryContinue();
}

EventDesiredResult< Infected > InfectedAlert::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	if ( victim && victim->IsAlive() )
		return TryChangeTo( new InfectedAttack( victim ) );
	return TryContinue();
}

EventDesiredResult< Infected > PunchVictim::OnCommandAttack( Infected *me, CBaseEntity *victim )
{
	CBaseEntity *current = m_victim.Get();
	if ( victim && current && victim->entindex() != current->entindex() && IsValidVictim( victim ) )
	{
		m_attack->SetVictim( victim );
		return TryDone();
	}
	return TryContinue();
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
EventDesiredResult< Infected > InfectedExecAction::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
    const unsigned int damageType = (unsigned int)info.GetDamageType();
    const bool fatalBlast = ( damageType & DMG_BLAST ) && info.GetDamage() * 4.0f > info.GetMaxDamage();
    CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
    const bool mountedAtomize = ( damageType & 0x20000002u ) && info.GetDamage() >= (float)me->GetHealth() &&
        attacker && ( attacker->GetTeamNumber() == 2 || attacker->GetTeamNumber() == 4 ) &&
        attacker->m_usingMountedGun && z_minigun_atomize.GetBool();
    if ( mountedAtomize || fatalBlast )
    {
        me->SetHealth( -1 );
        me->Retire( info );
        return TryDone( RESULT_CRITICAL );
    }
    if ( damageType & 0x20000187u )
        me->AddGesture( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_FLINCH ), true );
    return TryContinue();
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

#pragma float_control( precise, on, push )
void Infected::CreateUncommonChaseGoal()
{
    if ( ( GetGender() != 16 && GetGender() != 17 ) || ( GetInfectedFlags() & 0x100 ) )
        return;
    CUtlVector< CBaseEntity * > children;
    CollectUncommonDescendants( FirstMoveChild(), children );
    if ( children.Count() >= 1 )
        return;
    CBaseEntity *goal = CreateEntityByName( "info_goal_infected_chase" );
    if ( goal )
    {
        DispatchSpawn( goal );
        const Vector center = WorldSpaceCenter();
        goal->Teleport( &center, &vec3_angle, &vec3_origin );
        variant_t value;
        goal->AcceptInput( "Enable", this, this, value, 0 );
        goal->SetParent( this, -1 );
        if ( !TheDirector->IsFinale() && GetGender() == 16 )
            SetUncommonMusicVariant( "clown" );
    }
}

void Infected::RemoveUncommonChaseGoals()
{
    if ( GetGender() != 16 && GetGender() != 17 )
        return;
    CUtlVector< CBaseEntity * > children;
    CollectUncommonDescendants( FirstMoveChild(), children );
    FOR_EACH_VEC( children, i )
    {
        UTIL_Remove( children[i] );
        SetUncommonMusicVariant( "" );
    }
}

ActionResult< Infected > InfectedFlee::OnStart( Infected *me, Action< Infected > *prior )
{
    m_goal = NULL;
    m_path.Invalidate();
    return Continue();
}

ActionResult< Infected > InfectedFlee::Update( Infected *me, float interval )
{
    if ( !m_goal || ( me->GetAbsOrigin() - m_goal->GetCenter() ).LengthSqr() < 250000.0f )
    {
        CEscapeRoute *route = GetCriticalPath();
        if ( route )
        {
            const TerrorNavArea *area = ToTerrorNavArea( me->GetLastKnownArea() );
            const float flow = area ? area->GetFlowDistance( FLOW_TOWARD_GOAL ) : -1.0f;
            float distance = RandomFloat( 1500.0f, 3000.0f );
            const float average = GetNativeFleeSurvivorAverageFlow();
            const float ratio = flow / route->GetLength();
            TerrorFlowDirection direction = FLOW_FROM_START;
            // Native COMISS/JBE takes unordered in this branch. Do not rewrite
            // to the superficially equivalent ordered ratio>0.1 expression.
            if ( !( ratio > 0.1f ) || ( !( ratio > 0.9f ) && !( average > flow ) ) )
            {
                direction = (TerrorFlowDirection)1;
            }
            else
            {
                direction = (TerrorFlowDirection)0;
                distance = -distance;
            }
            TerrorNavArea *goal = route->GetAreaAtFlowDistance( flow + distance, direction );
            if ( goal )
            {
                InfectedFleePathCost cost( me );
                m_path.Compute( me, goal->GetCenter(), cost, 0.0f );
                m_goal = goal; // Store even when Compute reports failure.
            }
        }
    }
    if ( m_goal )
    {
        if ( z_Infected_debug.GetBool() )
            NDebugOverlay::HorzArrow( me->GetAbsOrigin(), m_goal->GetCenter(), 1.0f, 255, 255, 0, 255, true, 0.2f );
        m_path.Update( me );
    }
    me->UpdateCommonApproachSteering( NULL, &m_path );
    ILocomotion *mover = me->GetLocomotionInterface();
    if ( !mover->IsClimbingOrJumping() && !mover->IsUsingLadder() && mover->IsOnGround() )
    {
        IBody *body = me->GetBodyInterface();
        const Activity activity = ( me->GetInfectedFlags() & 4 ) ? ACT_TERROR_CRAWL_RUN :
            ( body->IsActualPosture( IBody::STAND ) ? ACT_TERROR_RUN_INTENSE : ACT_TERROR_CROUCH_RUN_INTENSE );
        if ( !body->IsActivity( activity ) )
            body->StartActivity( activity );
    }
    return Continue();
}

EventDesiredResult< Infected > InfectedFlee::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
    if ( other && other->IsPlayer() && me->GetLocomotionInterface()->IsAttemptingToMove() )
        return TryChangeTo( new InfectedAttack( other ) );
    return TryToSustain();
}

EventDesiredResult< Infected > InfectedFlee::OnStuck( Infected *me )
{
    m_goal = NULL;
    return TryToSustain();
}

EventDesiredResult< Infected > InfectedFlee::OnShoved( Infected *me, CBaseEntity *pusher )
{
    return TrySuspendFor( new InfectedShoved( pusher ), RESULT_IMPORTANT );
}

EventDesiredResult< Infected > InfectedAttack::OnInjured( Infected *me, const CTakeDamageInfo &info )
{
    if ( me->GetInfectedFlags() & 0x400 )
        return TryChangeTo( new InfectedFlee );
    return TryToSustain();
}

EventDesiredResult< Infected > InfectedBehavior::OnIgnite( Infected *me )
{
    if ( !me->GetLocomotionInterface() )
        return TryContinue();
    me->SetHealth( -1 );
    return TryChangeTo( new InfectedBurn, RESULT_CRITICAL );
}
#pragma float_control( pop )


#pragma float_control( precise, on, push )
InfectedBlinded::InfectedBlinded( Action< Infected > *successor, const char *sound )
{
    // Native415670 has NO constructor store to yawDrift60. Real timers64/76
    // construct in declaration order before these successor/sound assignments.
    m_successor = successor;
    m_sound = sound;
}

InfectedBlinded::~InfectedBlinded()
{
    // Native4152A0 owns an untransferred successor, unlike StandingActivity.
    if ( m_successor )
    {
        delete m_successor;
        m_successor = NULL;
    }
}

ActionResult< Infected > InfectedBlinded::OnStart( Infected *me, Action< Infected > *prior )
{
    m_yawDrift = RandomFloat( -40.0f, 40.0f );
    m_blindTimer.Start( RandomFloat( 8.0f, 12.0f ) );
    me->GetBodyInterface()->SetArousal( IBody::INTENSE );
    return Continue();
}

ActionResult< Infected > InfectedBlinded::Update( Infected *me, float interval )
{
    IBody *body = me->GetBodyInterface();
    if ( !body->IsActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_RUN_ON_FIRE ) ) )
        body->StartActivity( TerrorActivityFromOriginal( TERROR_ORIGINAL_ACT_TERROR_RUN_ON_FIRE ), IBody::MOTION_CONTROLLED_XY );
    QAngle angles = me->GetLocalAngles();
    angles.y += m_yawDrift * interval;
    me->SetLocalAngles( angles );
    if ( gpGlobals->curtime >= m_voiceTimer.m_timestamp )
    {
        me->EmitVocalization( m_sound, true );
        m_voiceTimer.Start( RandomFloat( 1.0f, 3.0f ) );
    }
    // Original COMISS/JBE and FCOMIP/JB continue for unordered operands.
    if ( !( m_blindTimer.m_timestamp > 0.0f ) || !( gpGlobals->curtime >= m_blindTimer.m_timestamp ) )
        return Continue();
    if ( m_successor )
    {
        Action< Infected > *successor = m_successor;
        m_successor = NULL;
        return ChangeTo( successor, "Blindness wore off" );
    }
    return Done();
}

EventDesiredResult< Infected > InfectedBlinded::OnContact( Infected *me, CBaseEntity *other, CGameTrace *trace )
{
    // Main-approved NULL minimal skip, native415740: omit only the target
    // query/suspension; retain TryContinue. No substitute collision actor.
    if ( other && other->MyCombatCharacterPointer() )
        return TrySuspendFor( new InfectedShoved( other ), RESULT_TRY, "Bumped into another Actor while blinded" );
    return TryContinue();
}

EventDesiredResult< Infected > InfectedBlinded::OnShoved( Infected *me, CBaseEntity *pusher )
{
    return TrySuspendFor( new InfectedShoved( pusher ), RESULT_IMPORTANT, "Shoved by someone while blind" );
}

EventDesiredResult< Infected > InfectedBlinded::OnBlinded( Infected *me, CBaseEntity *blinder )
{
    const float duration = RandomFloat( 8.0f, 12.0f );
    if ( duration > m_blindTimer.m_timestamp - (double)gpGlobals->curtime )
        m_blindTimer.Start( duration );
    return TryToSustain();
}

InfectedStandingActivity::InfectedStandingActivity( Activity activity, float duration, Action< Infected > *successor )
{
    // Native418780 constructs timer56 before these scalar stores.
    m_activity = activity;
    m_successor = successor;
    m_complete = false;
    if ( duration > 0.0f )
        m_timer.Start( duration );
}

InfectedStandingActivity::~InfectedStandingActivity()
{
    // Native4189A0 calls ONLY Action::~Action. It does NOT delete an unused raw
    // successor68. Preserve this original ownership/leak quirk; no repair.
}

ActionResult< Infected > InfectedStandingActivity::OnStart( Infected *me, Action< Infected > *prior )
{
    me->GetBodyInterface()->StartActivity( m_activity, IBody::MOTION_CONTROLLED_XY );
    return Continue();
}

ActionResult< Infected > InfectedStandingActivity::Update( Infected *me, float interval )
{
    if ( m_timer.m_timestamp > 0.0f && gpGlobals->curtime >= m_timer.m_timestamp )
        m_complete = true;
    if ( !m_complete )
        return Continue();
    if ( m_successor )
    {
        Action< Infected > *successor = m_successor;
        m_successor = NULL;
        return ChangeTo( successor );
    }
    return Done();
}

EventDesiredResult< Infected > InfectedStandingActivity::OnAnimationActivityComplete( Infected *me, int activity )
{
    if ( activity == m_activity )
        m_complete = true;
    return TryContinue();
}
#pragma float_control( pop )

EventDesiredResult< Infected > InfectedAttack::OnBlinded( Infected *me, CBaseEntity *blinder )
{
    me->RemoveUncommonChaseGoals();
    const Vector position = blinder ? blinder->WorldSpaceCenter() : me->WorldSpaceCenter();
    return TryChangeTo( new InfectedBlinded( new InfectedAlert( position, InfectedAlert::INJURY ), "Zombie.IgniteScream" ) );
}
