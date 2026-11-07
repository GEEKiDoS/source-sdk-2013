//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/entities/car_alarm.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "l4d2/sound/ai_speechqueue.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

LINK_ENTITY_TO_CLASS( prop_car_alarm, CCarProp );
LINK_ENTITY_TO_CLASS( prop_car_glass, CCarGlassProp );

BEGIN_DATADESC( CCarProp )
	DEFINE_KEYFIELD( m_bDisabled, FIELD_BOOLEAN, "startdisabled" ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Enable", InputEnable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "Disable", InputDisable ),
	DEFINE_INPUTFUNC( FIELD_VOID, "SurvivorStandingOnCar", InputSurvivorStandingOnCar ),
	DEFINE_OUTPUT( m_OnCarAlarmStart, "OnCarAlarmStart" ),
	DEFINE_OUTPUT( m_OnCarAlarmEnd, "OnCarAlarmEnd" ),
	DEFINE_OUTPUT( m_OnCarAlarmChirpStart, "OnCarAlarmChirpStart" ),
	DEFINE_OUTPUT( m_OnCarAlarmChirpEnd, "OnCarAlarmChirpEnd" ),
	DEFINE_THINKFUNC( CCarPropAlarmEndThink ),
	DEFINE_THINKFUNC( CCarPropChirpStartThink ),
	DEFINE_THINKFUNC( CCarPropChirpEndThink ),
	DEFINE_ENTITYFUNC( CCarPropAlarmTouch ),
END_DATADESC()

ConVar car_alarm_distance( "car_alarm_distance", "750", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
ConVar car_alarm_duration( "car_alarm_duration", "15", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
ConVar car_alarm_chirp_distance( "car_alarm_chirp_distance", "350", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
ConVar car_alarm_chirp_interval( "car_alarm_chirp_interval", "3", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );
ConVar car_alarm_chirp_duration( "car_alarm_chirp_duration", "1", FCVAR_DEVELOPMENTONLY | FCVAR_GAMEDLL );

namespace
{
// Native scalar SSE grouping: (dx*dx + dy*dy) + dz*dz, using ABS ORIGINS.
// In particular, neither a bounding-box center nor a horizontal-only range.
float CarAlarmDistanceSquared( const Vector &from, const Vector &to )
{
	__m128 x = _mm_sub_ss( _mm_set_ss( to.x ), _mm_set_ss( from.x ) );
	__m128 y = _mm_sub_ss( _mm_set_ss( to.y ), _mm_set_ss( from.y ) );
	__m128 z = _mm_sub_ss( _mm_set_ss( to.z ), _mm_set_ss( from.z ) );
	return _mm_cvtss_f32( _mm_add_ss( _mm_add_ss( _mm_mul_ss( x, x ), _mm_mul_ss( y, y ) ), _mm_mul_ss( z, z ) ) );
}

float CarAlarmRangeSquared( float range )
{
	__m128 value = _mm_set_ss( range );
	return _mm_cvtss_f32( _mm_mul_ss( value, value ) );
}

bool CarAlarmOrderedGreater( float left, float right )
{
	return ( _mm_movemask_ps( _mm_cmpgt_ss( _mm_set_ss( left ), _mm_set_ss( right ) ) ) & 1 ) != 0;
}
}

CCarProp::CCarProp()
{
	// S1A3F90: actual base/output/timer ctors precede the listener/registry calls.
	// CBaseEntity's existing zero allocator owns the two unwritten flags.
	ListenForGameEvent( "weapon_fire" );
	TheDirector->RegisterCarAlarm( this );
	m_bAlarmSuppressed = false;
	m_bDisabled = false;
}

void CCarProp::UpdateOnRemove()
{
	// S1A40F0. CBreakableProp's inherited removal hook may unregister a second
	// time when PROPINTER_BOT_FORBIDDEN_TARGET is set. Preserve that ordering.
	TheDirector->UnregisterCarAlarm( this );
	BaseClass::UpdateOnRemove();
}

int CCarProp::OnTakeDamage( const CTakeDamageInfo &info )
{
	// S1A7C70: use DamageCustom, NOT ammo type (native info+76 versus ammo+84).
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker )
	{
		if ( attacker->GetTeamNumber() == TEAM_SURVIVOR )
		{
			if ( info.GetDamageCustom() != -1 && !( info.GetDamageType() & DMG_BURN ) )
				TriggerAlarm( attacker );
		}
		else if ( attacker->GetTeamNumber() == TEAM_INFECTED && attacker->IsPlayer() &&
			static_cast< CTerrorPlayer * >( attacker )->GetZombieClass() == 8 && !m_bAlarmTriggered )
		{
			m_bAlarmSuppressed = true;
			CCarPropAlarmEndThink();
		}
	}
	return BaseClass::OnTakeDamage( info );
}

void CCarProp::InputEnable( inputdata_t &inputdata )
{
	m_bDisabled = false;
	SetTouch( &CCarProp::CCarPropAlarmTouch );
}

void CCarProp::InputDisable( inputdata_t &inputdata )
{
	m_bDisabled = true;
	SetTouch( NULL );
}

void CCarProp::InputSurvivorStandingOnCar( inputdata_t &inputdata )
{
	TriggerAlarm( inputdata.pActivator );
}

void CCarProp::CCarPropAlarmTouch( CBaseEntity *other )
{
	// S19E870: this peculiar touch trigger is PZ-only and DOMINATED-only.
	// Default Spawn keeps inherited BreakablePropTouch until the Enable input.
	if ( other->GetTeamNumber() == TEAM_SURVIVOR && TerrorGameRules()->IsPlayerControlledZombies() &&
		other && other->IsPlayer() && static_cast< CTerrorPlayer * >( other )->IsDominated() )
	{
		TriggerAlarm( other );
	}
}

void CCarProp::FireGameEvent( IGameEvent *event )
{
	// S1A4260 receives the native listener subobject; C++ supplies the proper
	// complete object here. Native only registers weapon_fire, no name filter.
	if ( m_bAlarmSuppressed || m_bDisabled || m_bAlarmTriggered || m_bChirpPending )
		return;
	if ( m_AlarmTimer.HasStarted() && !m_AlarmTimer.IsElapsed() )
		return;
	if ( m_ChirpTimer.HasStarted() && !m_ChirpTimer.IsElapsed() )
		return;

	CBasePlayer *player = UTIL_PlayerByUserId( event->GetInt( "userid", 0 ) );
	if ( !player )
		return;
	const float range = car_alarm_chirp_distance.GetFloat();
	const Vector &carOrigin = GetAbsOrigin();
	const Vector &playerOrigin = player->GetAbsOrigin();
	const float distanceSquared = CarAlarmDistanceSquared( carOrigin, playerOrigin );
	if ( CarAlarmOrderedGreater( CarAlarmRangeSquared( range ), distanceSquared ) )
	{
		// S1A4369 uses the widened 0.1 DOUBLE constant before its float spill.
		float when = static_cast< float >( static_cast< double >( gpGlobals->curtime ) + 0.1 );
		SetContextThink( &CCarProp::CCarPropChirpStartThink, when, "ContextAlarm" );
		m_bChirpPending = true;
	}
}

void CCarProp::TriggerAlarm( CBaseEntity *activator )
{
	// S19E4E0: native one-shot, disabled/suppressed and live alarm guards.
	if ( m_bAlarmSuppressed || m_bDisabled || m_bAlarmTriggered )
		return;
	if ( m_AlarmTimer.HasStarted() && !m_AlarmTimer.IsElapsed() )
		return;
	if ( !activator )
		return;

	const float range = car_alarm_distance.GetFloat();
	const Vector &carOrigin = GetAbsOrigin();
	const Vector &activatorOrigin = activator->GetAbsOrigin();
	const float distanceSquared = CarAlarmDistanceSquared( carOrigin, activatorOrigin );
	// Native JA rejects only ordered greater, so NaN is NOT rejected here.
	// Chirp's strict greater test above deliberately has the opposite admission.
	if ( CarAlarmOrderedGreater( distanceSquared, CarAlarmRangeSquared( range ) ) )
		return;

	CTerrorPlayer *player = ToTerrorPlayer( activator );
	if ( player && player->IsBot() )
		return;
	if ( !TheDirector->OnScriptedPanicEvent( player, 1 ) )
		return;

	m_bAlarmTriggered = true;
	TheDirector->UnregisterCarAlarm( this );
	m_bChirpPending = false;
	if ( m_ChirpTimer.HasStarted() )
		CCarPropChirpEndThink();
	L4DGameStats().Event_CarAlarmActive();
	m_OnCarAlarmStart.FireOutput( this, this, 0.0f );
	m_AlarmTimer.Start( car_alarm_duration.GetFloat() );
	SetContextThink( &CCarProp::CCarPropAlarmEndThink, gpGlobals->curtime + car_alarm_duration.GetFloat(), "ContextAlarm" );

	if ( player )
	{
		AI_CriteriaSet criteria;
		criteria.AppendCriteria( "Subject", player->GetResponseCharacterName(), 1.0f );
		criteria.AppendCriteria( "PanicType", "CarAlarm", 1.0f );
		// Native target tag1=ALL, absolute deadline0 and NULL issuer; no direct
		// SpeakIfAllowed substitute or extra Subject/From criteria.
		ResponseQueueManager().Queue( "PanicEvent", criteria, 0.0f, ResponseTarget_t( ResponseTarget_t::ALL ), NULL );
	}
	IGameEvent *event = gameeventmanager->CreateEvent( "triggered_car_alarm" );
	if ( event )
	{
		event->SetInt( "userid", player ? player->GetUserID() : 0 );
		gameeventmanager->FireEvent( event );
	}
}

void CCarProp::CCarPropAlarmEndThink()
{
	SetContextThink( NULL, 0.0f, "ContextAlarm" );
	m_OnCarAlarmEnd.FireOutput( this, this, 0.0f );
	m_ChirpTimer.Invalidate();
	SetTouch( NULL );
	// The native alarm timer and one-shot flag are NOT reset here.
}

void CCarProp::CCarPropChirpStartThink()
{
	SetContextThink( NULL, 0.0f, "ContextAlarm" );
	m_bChirpPending = false;
	m_ChirpTimer.Start( car_alarm_chirp_interval.GetFloat() );
	m_OnCarAlarmChirpStart.FireOutput( this, this, 0.0f );
	SetContextThink( &CCarProp::CCarPropChirpEndThink, gpGlobals->curtime + car_alarm_chirp_duration.GetFloat(), "ContextAlarm" );
}

void CCarProp::CCarPropChirpEndThink()
{
	SetContextThink( NULL, 0.0f, "ContextAlarm" );
	m_OnCarAlarmChirpEnd.FireOutput( this, this, 0.0f );
	m_ChirpTimer.Invalidate();
}

int CCarGlassProp::OnTakeDamage( const CTakeDamageInfo &info )
{
	// S1A4400: unlike the car's own damage gate, native350A70 accepts BOTH
	// team2 and the separate L4D1 survivor team4 before looking up the parent.
	CBaseEntity *attacker = info.GetAttacker();
	if ( attacker )
	{
		const int team = attacker->GetTeamNumber();
		if ( ( team == TEAM_SURVIVOR || team == TEAM_L4D1_SURVIVOR ) &&
			info.GetDamageCustom() != -1 && !( info.GetDamageType() & DMG_BURN ) )
		{
			CCarProp *car = dynamic_cast< CCarProp * >( GetMoveParent() );
			if ( car )
				car->TriggerAlarm( attacker );
		}
	}
	return BaseClass::OnTakeDamage( info );
}
