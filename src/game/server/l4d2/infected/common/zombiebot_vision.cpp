//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "NextBot/NextBotInterface.h"
#include "NextBot/NextBotBodyInterface.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/director/director_session_manager.h"
#include "l4d2/nav/terror_nav_area.h"
#include "world.h"
#include "NextBot/NextBotUtil.h"
#include "l4d2/terror_shareddefs.h"
#include "tier0/vprof.h"
#include <float.h>

#include "l4d2/infected/common/zombiebot_vision_console_server_cvars.inc"
#include "tier0/memdbgon.h"

extern ConVar nb_blind;
extern bool IsInCommentaryMode();
static ConVar nb_vision_ignore_survivors( "nb_vision_ignore_survivors", "0", FCVAR_CHEAT | FCVAR_GAMEDLL );
static ConVar z_vision_range( "z_vision_range", "500", FCVAR_CHEAT | FCVAR_GAMEDLL, "How far a Zombie can see." );
static ConVar z_vision_range_alert( "z_vision_range_alert", "1500", FCVAR_CHEAT | FCVAR_GAMEDLL, "How far a Zombie can see when alert." );
static ConVar z_vision_range_daylight( "z_vision_range_daylight", "2400", FCVAR_CHEAT | FCVAR_GAMEDLL, "How far a Zombie can see in daylight" );
static ConVar z_notice_near_range( "z_notice_near_range", "100", FCVAR_CHEAT | FCVAR_GAMEDLL );
ConVar z_vision_range_obscured( "z_vision_range_obscured", "500", 0x00004004, "How far a Zombie can see in OBSCURED areas." );
ConVar z_vision_range_obscured_alert( "z_vision_range_obscured_alert", "750", 0x00004004, "How far a Zombie can see in OBSCURED areas when alert." );

ZombieBotVision::ZombieBotVision( INextBot *bot ) : BaseClass( bot ), m_recognizedVictim( NULL )
{
	m_recognizedRangeSquared = 1.0e11f;
	m_neutralRecognizeTime = 0.0f;
	m_alertRecognizeTime = 0.0f;
}

void ZombieBotVision::Reset()
{
	BaseClass::Reset();
	// S:0x3FD200. These are per-reset global draws, not per-target timers.
	m_neutralRecognizeTime = RandomFloat( 0.4f, 0.6f );
	m_alertRecognizeTime = RandomFloat( 0.2f, 0.4f );
	m_recognizedVictim = NULL;
	m_recognizedRangeSquared = 1.0e9f;
	m_scanTimer.Invalidate();
}

void ZombieBotVision::ClearRecognizedVictim()
{
	m_recognizedVictim = NULL;
	m_recognizedRangeSquared = 1.0e11f;
}

void ZombieBotVision::CollectPotentiallyVisibleEntities( CUtlVector< CBaseEntity * > *potentiallyVisible )
{
	potentiallyVisible->RemoveAll();
	CUtlVector< CTerrorPlayer * > players;
	CollectTerrorPlayers( players, TEAM_SURVIVOR, 2, 3, 9 );
	FOR_EACH_VEC( players, i )
		potentiallyVisible->AddToTail( players[i] );
}

float ZombieBotVision::GetDefaultFieldOfView() const { return 180.0f; } // S:0x3FD330.

float ZombieBotVision::GetMaxVisionRange() const
{
	const IBody *body = GetBot()->GetBodyInterface();
	const bool alert = body && !body->IsArousal( IBody::NEUTRAL );
	const TerrorNavArea *area = ToTerrorNavArea( GetBot()->GetEntity()->GetLastKnownArea() );
	if ( area && area->HasSpawnAttributes( TERROR_NAV_OBSCURED ) )
		return alert ? z_vision_range_obscured_alert.GetFloat() : z_vision_range_obscured.GetFloat();
	if ( IsMapTimeOfDay( 2, 4 ) || GetBot()->GetEntity()->GetGender() == 16 || GetBot()->GetEntity()->GetGender() == 17 )
		return z_vision_range_daylight.GetFloat();
	return alert ? z_vision_range_alert.GetFloat() : z_vision_range.GetFloat();
}

float ZombieBotVision::GetNoticeRange() const
{
	float range = z_notice_near_range.GetFloat();
	if ( IsMapTimeOfDay( 2, 4 ) || GetBot()->GetEntity()->GetGender() == 16 || GetBot()->GetEntity()->GetGender() == 17 )
		range = z_vision_range_daylight.GetFloat();
	return range;
}

float ZombieBotVision::GetMinRecognizeTime() const
{
	return GetBot()->GetBodyInterface()->IsArousal( IBody::NEUTRAL ) ? m_neutralRecognizeTime : m_alertRecognizeTime;
}

bool ZombieBotVision::IsIgnored( CBaseEntity *subject ) const
{
	if ( IsInCommentaryMode() && subject->IsPlayer() && !ToBasePlayer( subject )->IsFakeClient() )
		return true;
	CBaseCombatCharacter *me = GetBot()->GetEntity();
	if ( me->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
		return true;
	if ( me->IsPlayer() )
		return false;
	if ( GetBot()->GetBodyInterface()->IsActualPosture( IBody::STAND ) )
	{
		CBaseEntity *owner = subject->GetOwnerEntity();
		return owner && owner->GetTeamNumber() == TEAM_INFECTED;
	}
	return true;
}

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
bool ZombieBotVision::IsVisibleEntityNoticed( CBaseEntity *subject ) const
{
	if ( IsIgnored( subject ) )
		return false;
	if ( !subject->IsPlayer() || ( subject->GetTeamNumber() != TEAM_SURVIVOR && subject->GetTeamNumber() != TEAM_L4D1_SURVIVOR ) )
		return true;
	if ( nb_vision_ignore_survivors.GetBool() )
		return false;
	if ( !GetBot()->IsAbleToSee() || TheDirector->GetSessionManager()->IsStartAreaExitBlocked() )
		return false;
	IBody *body = GetBot()->GetBodyInterface();
	if ( body && !body->IsArousal( IBody::NEUTRAL ) )
		return true;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( subject );
	if ( player->IsIT() )
		return true;
	const float speedSquared = subject->GetAbsVelocity().LengthSqr();
	if ( speedSquared > 22500.0f )
		return true;
	bool illuminated = false;
	if ( player->FlashlightIsOn() && body )
	{
		const Vector playerEye = player->EyePosition();
		Vector toMe = body->GetEyePosition() - playerEye;
		toMe.NormalizeInPlace();
		Vector forward;
		player->EyeVectors( &forward );
		illuminated = DotProduct( toMe, forward ) > 0.9396f;
	}
	if ( !illuminated && ( !( speedSquared > 900.0f ) || player->IsStill( true ) ) )
		return false;
	return GetBot()->IsRangeLessThan( subject, GetNoticeRange() );
}


void ZombieBotVision::Update()
{
	VPROF_BUDGET( "ZombieBotVision::Update", "NextBot" );
	// S:0x3FD520 keeps its recognized handle while blinded, and does not
	// replace it with a fresh nearest survivor on every component update.
	bool blind = nb_blind.GetBool();
	CBaseCombatCharacter *me = GetBot()->GetEntity();
	if ( me && me->IsPlayer() && ( static_cast< CTerrorPlayer * >( me )->GetSenseFlags() & 1 ) )
		blind = true;
	if ( !m_scanTimer.IsElapsed() || blind )
		return;
	const bool neutral = GetBot()->GetBodyInterface()->IsArousal( IBody::NEUTRAL );
	m_scanTimer.Start( neutral ? 0.5f * GetMinRecognizeTime() : RandomFloat( 1.0f, 2.0f ) );

	CUtlVector< CBaseEntity * > candidates;
	CollectPotentiallyVisibleEntities( &candidates );
	CBaseEntity *selected = NULL;
	m_recognizedRangeSquared = 1.0e11f;
	const TerrorNavArea *myArea = ToTerrorNavArea( GetBot()->GetEntity()->GetLastKnownArea() );
	FOR_EACH_VEC( candidates, i )
	{
		CBaseEntity *candidate = candidates[i];
		if ( IsIgnored( candidate ) )
			continue;
		const double wideRangeSquared = GetBot()->GetRangeSquaredTo( candidate );
		const float rangeSquared = static_cast< float >( wideRangeSquared );
		if ( !( static_cast< double >( m_recognizedRangeSquared ) > wideRangeSquared ) )
			continue;
		const CNavArea *targetArea = static_cast< CTerrorPlayer * >( candidate )->GetLastKnownArea();
		if ( targetArea && myArea && !myArea->IsPotentiallyVisible( targetArea, false ) && !( rangeSquared < 40000.0f ) )
			continue;
		if ( IsAbleToSee( candidate, USE_FOV ) )
		{
			selected = candidate;
			m_recognizedRangeSquared = rangeSquared;
		}
	}

	CBaseEntity *previous = m_recognizedVictim.Get();
	if ( selected )
		m_visibleTimer.Start();
	if ( selected != previous )
	{
		m_recognizedVictim = selected ? selected->MyCombatCharacterPointer() : NULL;
		GetBot()->OnLostSight( previous );
		if ( selected )
			GetBot()->OnSight( selected );
		GetBot()->OnThreatChanged( selected ? selected->MyCombatCharacterPointer() : NULL );
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
