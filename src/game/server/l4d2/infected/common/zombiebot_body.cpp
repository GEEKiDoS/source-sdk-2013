//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/common/zombiebot_body.h"
#include "l4d2/infected/infected_activities.h"
#include "eventlist.h"
#include "animation.h"
#include "npcevent.h"
#include "studio.h"
#include "tier0/vprof.h"
#include "datacache/imdlcache.h"

#include "l4d2/player/terrorplayer.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/terror_physics_math.h"
#include "util_shared.h"
#include "l4d2/infected/common/zombiebot_body_console_server_cvars.inc"
#include "tier0/memdbgon.h"

static ConVar z_vocalize_shot_interval( "z_vocalize_shot_interval", ".5", 0x00004004,
	"The minimum time between vocalizing being shot" );

// The common's footstep consumer is client-owned. The server still owns the
// authoritative replicated interval (original server ConVar object RVA 0x7F48E0).
ConVar sv_footstepinterval( "sv_footstepinterval", "0.22", 0x00006004 );

ZombieBotBody::ZombieBotBody( Infected *infected ) : BaseClass( infected )
{
	m_infected = infected;
	m_activity = ACT_INVALID;
	m_activityFlags = 0;
	m_lastActivity = ACT_INVALID;
	m_lastActivityFlags = 0;
	m_desiredPosture = STAND;
	m_actualPosture = STAND;
	m_arousal = NEUTRAL;
	m_isPostureChanging = false;
	m_forwardAttachment = 0;
	m_moveXPose = m_moveYPose = m_bodyYawPose = m_bodyPitchPose = -1;
	m_leanYawPose = m_leanPitchPose = -1;
	m_eyeOffset = vec3_origin;
	m_viewVector.Init( 1.0f, 0.0f, 0.0f );
	m_eyePosition = vec3_origin;
}

void ZombieBotBody::Reset()
{
	BaseClass::Reset();
	// S:0x3F4D50: reset posture and the underlying activity without inventing
	// a sequence index. The initial action chooses the actual animation.
	m_eyeOffset = vec3_origin;
	m_viewVector.Init( 1.0f, 0.0f, 0.0f );
	m_desiredPosture = m_actualPosture = STAND;
	m_isPostureChanging = false;
	m_arousal = NEUTRAL;
	m_activity = ACT_TERROR_IDLE_NEUTRAL;
	m_activityFlags = 0;
	m_lastActivity = m_activity;
	m_lastActivityFlags = MOTION_CONTROLLED_XY;
	UpdateCollisionBounds();
}

void ZombieBotBody::OnModelChanged()
{
	// S:0x3F55C0. Pose names belong to the mounted model, not retail indices.
	m_forwardAttachment = m_infected->LookupAttachment( "forward" );
	m_moveXPose = m_infected->LookupPoseParameter( "move_x" );
	m_moveYPose = m_infected->LookupPoseParameter( "move_y" );
	m_bodyYawPose = m_infected->LookupPoseParameter( "body_yaw" );
	m_bodyPitchPose = m_infected->LookupPoseParameter( "body_pitch" );
	m_leanYawPose = m_infected->LookupPoseParameter( "lean_yaw" );
	m_leanPitchPose = m_infected->LookupPoseParameter( "lean_pitch" );
}

const Vector &ZombieBotBody::GetEyePosition() const
{
	m_eyePosition = m_infected->GetAbsOrigin() + m_eyeOffset;
	return m_eyePosition;
}

const Vector &ZombieBotBody::GetViewVector() const { return m_viewVector; }

void ZombieBotBody::AimHeadTowards( const Vector &lookAtPos, LookAtPriorityType priority,
	float duration, INextBotReply *replyWhenAimed, const char *reason )
{
	// S:0x3F5000 is the real five-argument position aim, not the older
	// seven-argument failure-only interface slots at 0x3F4410/0x3F4440.
	// Priority and duration do not gate this original immediate pose update.
	m_viewVector = lookAtPos - GetEyePosition();
	TerrorPhysicsNormalize( m_viewVector );
	QAngle aimAngles;
	VectorAngles( m_viewVector, aimAngles );
	const QAngle &bodyAngles = m_infected->GetLocalAngles();
	MDLCACHE_CRITICAL_SECTION();
	if ( m_bodyYawPose >= 0 )
		m_infected->SetPoseParameter( m_bodyYawPose, AngleDiff( aimAngles.y, bodyAngles.y ) );
	if ( m_bodyPitchPose >= 0 )
		m_infected->SetPoseParameter( m_bodyPitchPose, AngleDiff( aimAngles.x, bodyAngles.x ) );
	if ( replyWhenAimed )
		replyWhenAimed->OnSuccess( GetBot() );
}

void ZombieBotBody::AimHeadTowards( CBaseEntity *subject, LookAtPriorityType priority,
	float duration, INextBotReply *replyWhenAimed, const char *reason )
{
	// S:0x3F4650 -> 0x3F5000, then Infected::SetClientLookatTarget
	// (S:0x401CF0). Position-only aiming deliberately leaves this handle alone.
	AimHeadTowards( subject->EyePosition(), priority, duration, replyWhenAimed, reason );
	m_infected->m_clientLookatTarget = subject;
}
float ZombieBotBody::GetMaxHeadAngularVelocity() const { return 100.0f; }
bool ZombieBotBody::IsHeadSteady() const { return false; }
Activity ZombieBotBody::GetActivity() const { return m_activity; }
bool ZombieBotBody::IsActivity( Activity activity ) const { return m_activity == activity; }
bool ZombieBotBody::HasActivityType( unsigned int flags ) const { return ( m_activityFlags & flags ) != 0; }
IBody::PostureType ZombieBotBody::GetDesiredPosture() const { return m_desiredPosture; }
bool ZombieBotBody::IsDesiredPosture( PostureType posture ) const { return m_desiredPosture == posture; }
bool ZombieBotBody::IsInDesiredPosture() const { return m_desiredPosture == m_actualPosture; }
IBody::PostureType ZombieBotBody::GetActualPosture() const { return m_actualPosture; }
bool ZombieBotBody::IsActualPosture( PostureType posture ) const { return m_actualPosture == posture; }
bool ZombieBotBody::IsPostureChanging() const { return m_isPostureChanging; }
bool ZombieBotBody::IsPostureMobile() const
{
	return !m_isPostureChanging && m_actualPosture != SIT && m_actualPosture != LIE;
}
void ZombieBotBody::SetArousal( ArousalType arousal ) { m_arousal = arousal; }
IBody::ArousalType ZombieBotBody::GetArousal() const { return m_arousal; }
bool ZombieBotBody::IsArousal( ArousalType arousal ) const { return m_arousal == arousal; }

void ZombieBotBody::UpdateCollisionBounds()
{
	UTIL_SetSize( m_infected, GetHullMins(), GetHullMaxs() );
}

Activity ZombieBotBody::GetPostureTransition( PostureType posture ) const
{
	// S:0x3F46B0. Standing/crouching are instantaneous here; sitting and
	// lying have genuine motion-controlled model transitions.
	if ( posture == SIT )
	{
		if ( m_actualPosture == SIT )
			return ACT_INVALID;
		return m_actualPosture == LIE ? ACT_TERROR_LIE_TO_SIT : ACT_TERROR_SIT_FROM_STAND;
	}
	if ( posture == LIE )
	{
		if ( m_actualPosture == LIE )
			return ACT_INVALID;
		return m_actualPosture == SIT ? ACT_TERROR_SIT_TO_LIE : ACT_TERROR_LIE_FROM_STAND;
	}
	if ( m_actualPosture == SIT )
		return m_arousal == NEUTRAL ? ACT_TERROR_SIT_TO_STAND : ACT_TERROR_SIT_TO_STAND_ALERT;
	if ( m_actualPosture == LIE )
		return m_arousal == NEUTRAL ? ACT_TERROR_LIE_TO_STAND : ACT_TERROR_LIE_TO_STAND_ALERT;
	return ACT_INVALID;
}

void ZombieBotBody::SetDesiredPosture( PostureType posture )
{
	const Activity transition = GetPostureTransition( posture );
	if ( transition == ACT_INVALID )
	{
		m_desiredPosture = m_actualPosture = posture;
		UpdateCollisionBounds();
	}
	else
	{
		StartActivity( transition, MOTION_CONTROLLED_XY );
		m_infected->SetPlaybackRate( 1.0f );
		m_desiredPosture = posture;
	}
}

bool ZombieBotBody::IsPostureTransition( Activity activity ) const
{
	return activity == ACT_TERROR_SIT_FROM_STAND
		|| activity == ACT_TERROR_SIT_TO_STAND
		|| activity == ACT_TERROR_SIT_TO_STAND_ALERT
		|| activity == ACT_TERROR_SIT_TO_LIE
		|| activity == ACT_TERROR_LIE_FROM_STAND
		|| activity == ACT_TERROR_LIE_TO_STAND
		|| activity == ACT_TERROR_LIE_TO_STAND_ALERT
		|| activity == ACT_TERROR_LIE_TO_SIT;
}

bool ZombieBotBody::IsLandingActivity( Activity activity ) const
{
	return activity == ACT_TERROR_FALL
		|| activity == ACT_TERROR_JUMP_LANDING
		|| activity == ACT_TERROR_JUMP_LANDING_HARD
		|| activity == ACT_TERROR_JUMP_LANDING_NEUTRAL
		|| activity == ACT_TERROR_JUMP_LANDING_HARD_NEUTRAL;
}

bool ZombieBotBody::IsTransitoryActivity( Activity activity ) const
{
	if ( IsLandingActivity( activity ) )
		return true;
	return activity == ACT_TERROR_FACE_LEFT_NEUTRAL
		|| activity == ACT_TERROR_FACE_LEFT_ALERT
		|| activity == ACT_TERROR_FACE_LEFT_INTENSE
		|| activity == ACT_TERROR_FACE_RIGHT_NEUTRAL
		|| activity == ACT_TERROR_FACE_RIGHT_ALERT
		|| activity == ACT_TERROR_FACE_RIGHT_INTENSE
		|| activity == ACT_TERROR_ABOUT_FACE_NEUTRAL
		|| activity == ACT_TERROR_ABOUT_FACE_ALERT
		|| activity == ACT_TERROR_ABOUT_FACE_INTENSE;
}

Activity ZombieBotBody::GetPostureIdle() const
{
	if ( m_actualPosture == SIT )
		return ACT_TERROR_SIT_IDLE;
	if ( m_actualPosture == LIE )
		return ACT_TERROR_LIE_IDLE;
	return ACT_TERROR_IDLE_NEUTRAL;
}

int ZombieBotBody::SelectAnimationSequence( Activity activity ) const
{
	// S:0x3F6520. Death variants are selected by model event markers, hitgroup
	// and attack direction, not by an invented fixed death sequence.
	if ( activity == ACT_TERROR_DIE_FROM_STAND
		|| activity == ACT_TERROR_DIE_WHILE_RUNNING )
	{
		bool front = true;
		CBaseCombatCharacter *attacker = m_infected->GetLastAttacker();
		if ( attacker )
		{
			Vector forward;
			m_infected->GetVectors( &forward, NULL, NULL );
			front = DotProduct( attacker->GetAbsOrigin() - m_infected->GetAbsOrigin(), forward ) > 0.0f;
		}

		int eventIndex = AE_INVALID;
		if ( m_infected->WasShoveKilled() )
		{
			eventIndex = front ? AE_HIT_FRONT : AE_HIT_BACK;
		}
		else
		{
			switch ( m_infected->GetLastHitGroup() )
			{
			case HITGROUP_HEAD: eventIndex = front ? AE_HIT_HEAD_FRONT : AE_HIT_HEAD_BACK; break;
			case HITGROUP_CHEST: eventIndex = front ? AE_HIT_CHEST : AE_HIT_BACK_UPPER; break;
			case HITGROUP_STOMACH: eventIndex = front ? AE_HIT_STOMACH : AE_HIT_BACK_LOWER; break;
			case HITGROUP_LEFTARM: eventIndex = front ? AE_HIT_SHOULDER_LEFT_FRONT : AE_HIT_SHOULDER_LEFT_BACK; break;
			case HITGROUP_RIGHTARM: eventIndex = front ? AE_HIT_SHOULDER_RIGHT_FRONT : AE_HIT_SHOULDER_RIGHT_BACK; break;
			case HITGROUP_LEFTLEG: eventIndex = front ? AE_HIT_LEG_LEFT_FRONT : AE_HIT_LEG_LEFT_BACK; break;
			case HITGROUP_RIGHTLEG: eventIndex = front ? AE_HIT_LEG_RIGHT_FRONT : AE_HIT_LEG_RIGHT_BACK; break;
			}
		}

		CStudioHdr *studio = m_infected->GetModelPtr();
		if ( studio && studio->SequencesAvailable() && eventIndex != AE_INVALID )
		{
			CUtlVector< int > matching;
			for ( int sequence = 0; sequence < studio->GetNumSeq(); ++sequence )
			{
				if ( m_infected->GetSequenceActivity( sequence ) != activity )
					continue;
				animevent_t event;
				int index = 0;
				while ( ( index = GetAnimationEvent( studio, sequence, &event, 0.0f, 1.01f, index ) ) != 0 )
					if ( ( event.type & AE_TYPE_NEWEVENTSYSTEM ) && event.event == eventIndex )
						matching.AddToTail( sequence );
			}
			if ( matching.Count() )
				return matching[ RandomInt( 0, matching.Count() - 1 ) ];
		}
	}
	return m_infected->SelectWeightedSequence( activity, m_infected->GetSequence() );
}

bool ZombieBotBody::StartActivity( Activity activity, unsigned int flags )
{
	// S:0x3F5960. Completion clears these flags before callbacks; being at
	// the final cycle does not authorize an out-of-order interruption.
	if ( HasActivityType( ACTIVITY_UNINTERRUPTIBLE ) )
		return false;
	ZombieBotLocomotion *locomotion = m_infected->GetLocomotionInterface();
	if ( IsActivity( ACT_TERROR_FALL )
		&& !locomotion->IsOnGround() && !locomotion->IsUsingLadder() )
		return false;

	MDLCACHE_CRITICAL_SECTION();
	int sequence;
	if ( activity == ACT_TERROR_SHOVED_BACKWARD
		&& m_infected->GetGender() == 15 && RandomInt( 1, 100 ) <= 50 )
	{
		sequence = m_infected->LookupSequence( RandomInt( 0, 1 ) == 0 ? "shoved_backward_04g" : "shoved_backward_04j" );
	}
	else
	{
		sequence = SelectAnimationSequence( activity );
	}
	if ( sequence == ACTIVITY_NOT_AVAILABLE )
		return false;
	if ( m_activity != ACT_INVALID )
		GetBot()->OnAnimationActivityInterrupted( m_activity );

	m_infected->SetPlaybackRate( 1.0f );
	m_infected->SetSequence( sequence );
	m_infected->SetCycle( 0.0f );
	m_infected->ResetSequenceInfo();
	m_infected->m_sequenceStartTime = gpGlobals->curtime;
	m_activity = activity;
	m_activityFlags = flags;
	if ( !IsTransitoryActivity( activity ) && !( flags & ACTIVITY_TRANSITORY ) )
	{
		m_lastActivity = activity;
		m_lastActivityFlags = flags;
	}
	m_isPostureChanging = IsPostureTransition( activity );
	return true;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void ZombieBotBody::UpdateMotionPoseParameters()
{
	ZombieBotLocomotion *locomotion = m_infected->GetLocomotionInterface();
	const QAngle &lean = locomotion->GetDesiredLean();
	if ( m_leanYawPose >= 0 )
		m_infected->SetPoseParameter( m_leanYawPose, lean.y );
	if ( m_leanPitchPose >= 0 )
		m_infected->SetPoseParameter( m_leanPitchPose, lean.x );
	// S:0x401DB0 publishes the same lean used by the server poses.
	m_infected->m_clientLeanYaw = lean.y;

	Vector forward, right;
	m_infected->GetVectors( &forward, &right, NULL );
	if ( m_moveXPose >= 0 )
	{
		const Vector &velocity = m_infected->GetAbsVelocity();
		// S3F634C..637A: wide dot, F32 spill, then wide division and F32 argument.
		const float forwardSpeed = (float)( ( (double)velocity.y * forward.y + (double)forward.x * velocity.x )
			+ (double)velocity.z * forward.z );
		m_infected->SetPoseParameter( m_moveXPose, (float)( (double)forwardSpeed / (double)locomotion->GetRunSpeed() ) );
	}
	if ( m_moveYPose >= 0 )
	{
		const Vector &velocity = m_infected->GetAbsVelocity();
		const float sideSpeed = (float)( ( (double)velocity.y * right.y + (double)right.x * velocity.x )
			+ (double)velocity.z * right.z );
		m_infected->SetPoseParameter( m_moveYPose, (float)( (double)sideSpeed / (double)locomotion->GetRunSpeed() ) );
	}

	const float speed = m_infected->GetAbsVelocity().Length();
	const float sequenceSpeed = m_infected->m_flGroundSpeed;
	if ( HasActivityType( ENTINDEX_PLAYBACK_RATE )
		|| ( !HasActivityType( MOTION_CONTROLLED_XY ) && !( sequenceSpeed >= speed )
			&& !( sequenceSpeed <= 0.0f ) && locomotion->IsOnGround() ) )
	{
		const float rate = speed / sequenceSpeed;
		// Native COMISS/JBE keeps an unordered rate; do not replace it with 12.
		m_infected->SetPlaybackRate( rate > 12.0f ? 12.0f : ( rate < -4.0f ? -4.0f : rate ) );
	}
	else
	{
		m_infected->SetPlaybackRate( 1.0f );
	}
}

void ZombieBotBody::Update()
{
	VPROF_BUDGET( "ZombieBotBody::Update", "NextBotExpensive" );
	MDLCACHE_CRITICAL_SECTION();
	const float deltaT = GetUpdateInterval();
	ZombieBotLocomotion *locomotion = m_infected->GetLocomotionInterface();

	if ( m_forwardAttachment && m_eyeUpdateTimer.IsElapsed() )
	{
		m_infected->GetAttachment( m_forwardAttachment, m_eyeOffset, &m_viewVector );
		m_eyeOffset -= m_infected->GetAbsOrigin();
		// Infected virtual S:0x3FE040 is a real global RandomFloat(1,2).
		m_eyeUpdateTimer.Start( RandomFloat( 1.0f, 2.0f ) );
	}

	if ( HasActivityType( MOTION_CONTROLLED_XY | MOTION_CONTROLLED_Z ) )
	{
		bool finished;
		Vector newPosition;
		QAngle newAngles;
		if ( m_infected->GetIntervalMovement( deltaT, finished, newPosition, newAngles ) )
		{
			Vector velocity = ( newPosition - m_infected->GetAbsOrigin() ) / deltaT;
			if ( !HasActivityType( MOTION_CONTROLLED_XY ) )
			{
				velocity.x = m_infected->GetAbsVelocity().x;
				velocity.y = m_infected->GetAbsVelocity().y;
			}
			if ( !HasActivityType( MOTION_CONTROLLED_Z ) )
				velocity.z = m_infected->GetAbsVelocity().z;
			m_infected->SetAbsVelocity( velocity );
			m_infected->SetLocalAngles( newAngles );
			if ( velocity.x <= -0.01f || velocity.x >= 0.01f
				|| velocity.y <= -0.01f || velocity.y >= 0.01f
				|| velocity.z <= -0.01f || velocity.z >= 0.01f )
				locomotion->DriveTo( newPosition );
		}
	}

	if ( !locomotion->IsOnGround() && !locomotion->IsUsingLadder() && m_infected->GetAbsVelocity().z < -5.0f )
		StartActivity( ACT_TERROR_FALL );

	if ( m_infected->IsSequenceFinished() && !m_infected->SequenceLoops() )
	{
		const Activity completed = m_activity != ACT_INVALID ? m_activity : m_infected->GetSequenceActivity( m_infected->GetSequence() );
		const bool landing = IsLandingActivity( completed );
		m_activity = ACT_INVALID;
		m_activityFlags = 0;
		if ( landing )
			StartActivity( m_lastActivity, m_lastActivityFlags );
		else
			GetBot()->OnAnimationActivityComplete( completed );

		if ( m_isPostureChanging )
		{
			m_actualPosture = m_desiredPosture;
			m_isPostureChanging = false;
			UpdateCollisionBounds();
			StartActivity( GetPostureIdle(), MOTION_CONTROLLED_XY );
		}
	}

	if ( HasActivityType( ENTINDEX_PLAYBACK_RATE ) )
	{
		volatile float playbackRate = _SSE_cos( static_cast< float >( m_infected->entindex() ) );
		playbackRate = playbackRate - ( -1.0f );
		playbackRate = playbackRate * 0.20000005f;
		playbackRate = playbackRate * 0.5f;
		playbackRate = playbackRate + 0.89999998f;
		m_infected->SetPlaybackRate( playbackRate );
	}
	if ( !m_isPostureChanging && m_actualPosture != SIT && m_actualPosture != LIE
		&& !HasActivityType( MOTION_CONTROLLED_XY | MOTION_CONTROLLED_Z ) )
		UpdateMotionPoseParameters();

	// S:0x3F5C30 -> NextBot HandleAnimEvent -> intention/action responder.
	// This is the sole animation advance/event owner; no synthetic melee clock.
	m_infected->StudioFrameAdvance();
	m_infected->DispatchAnimEvents( m_infected );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

void ZombieBotBody::OnInjured( const CTakeDamageInfo &info )
{
    if ( info.GetDamageType() & DMG_BURN )
    {
        if ( !m_infected->IsOnFire() )
        {
            m_infected->Ignite( 9999.0f, info.GetAttacker() );
            if ( !UTIL_IsLowViolence() )
                m_infected->EmitSound( "Zombie.IgniteScream" );
        }
        color32 color = m_infected->GetRenderColor();
        if ( color.r > 75 ) color.r -= 5;
        if ( color.g > 75 ) color.g -= 5;
        if ( color.b > 75 ) color.b -= 5;
        m_infected->SetRenderColor( color.r, color.g, color.b );
    }
    else if ( info.GetDamageType() & DMG_BULLET )
        m_infected->EmitSound( "Zombie.BulletImpact" );

    if ( m_infected->IsAlive() )
    {
        if ( gpGlobals->curtime >= m_shotVocalizeTimer.m_timestamp && !( info.GetDamageType() & DMG_BURN ) )
        {
            m_shotVocalizeTimer.Start( z_vocalize_shot_interval.GetFloat() );
            m_infected->EmitSound( "Zombie.Shot" );
        }
        CTerrorPlayer *attacker = ToTerrorPlayer( info.GetAttacker() );
        if ( attacker && ( attacker->GetTeamNumber() == 2 || attacker->GetTeamNumber() == 4 ) )
            attacker->GetMusic()->OnDamageDealt( info.GetDamage() );
        m_infected->FireHurtEvent( info );
        if ( info.GetDamageType() & DMG_BURN )
        {
            IGameEvent *event = gameeventmanager->CreateEvent( "zombie_ignited" );
            if ( event )
            {
                CBasePlayer *player = ToBasePlayer( info.GetAttacker() );
                event->SetInt( "userid", player ? player->GetUserID() : 0 );
                event->SetInt( "gender", m_infected->GetGender() );
                event->SetInt( "entityid", m_infected->entindex() );
                event->SetString( "victimname", "Infected" );
                event->SetBool( "fire_ammo", ( info.GetDamageType() & DMG_BULLET ) != 0 );
                gameeventmanager->FireEvent( event );
            }
        }
    }
}

// S3F5760: ordinary commons use client footsteps; the shared Witch body owns
// this server path. Do not turn it into unconditional common server audio.
void ZombieBotBody::OnAnimationEvent( animevent_t *event )
{
	CBaseCombatCharacter *entity = GetBot() ? GetBot()->GetEntity() : NULL;
	if ( entity && !( entity->GetTeamNumber() == TEAM_INFECTED && entity->GetZombieClass() == 7 ) )
		return;
	if ( event->event != 50 && event->event != 51 )
		return;
	entity = GetBot() ? GetBot()->GetEntity() : NULL;
	if ( !entity )
		return;
	const int waterLevel = entity->GetWaterLevel();
	const bool shallow = waterLevel == WL_Feet || waterLevel == WL_Waist || waterLevel == WL_Eyes;
	const bool wading = waterLevel == WL_Waist || waterLevel == WL_Eyes;
	entity->PlayTerrorFootstepSound( entity->GetAbsOrigin(), event->event == 50, shallow, wading, false );
}
