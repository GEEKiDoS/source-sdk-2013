//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_vision.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/terror_director.h"
#include "nav_area.h"
#include "tier0/memdbgon.h"

extern bool IsInCommentaryMode();

ConVar survivor_vision_range_obscured( "survivor_vision_range_obscured", "750", 0x00004004 );
ConVar survivor_vision_range( "survivor_vision_range", "1500", 0x00004004 );

SurvivorVision::SurvivorVision( INextBot *bot ) : IVision( bot )
{
	// The native constructor only constructs IVision; the random reaction
	// time is produced by Reset, when INextBot resets its real components.
}

void SurvivorVision::Reset()
{
	IVision::Reset();
	SetFieldOfView( GetDefaultFieldOfView() );
	m_minRecognizeTime = RandomFloat( 0.2f, 0.3f );
}

float SurvivorVision::GetMaxVisionRange() const
{
	CNavArea *area = GetBot()->GetEntity()->GetLastKnownArea();
	// This is the serialized base NAV attribute, not the distinct Terror
	// spawn-attribute namespace's identically numbered bit.
	return area && area->HasAttributes( 0x1000 ) ? survivor_vision_range_obscured.GetFloat() : survivor_vision_range.GetFloat();
}

float SurvivorVision::GetMinRecognizeTime() const
{
	return m_minRecognizeTime;
}

bool SurvivorVision::IsIgnored( CBaseEntity *subject ) const
{
	// 3A74A0 is native IsIgnored, not IsVisibleEntityNoticed. Original
	// 3A7480 calls this virtual and negates it before the LOS test.
	if ( subject->IsPlayer() && static_cast< CTerrorPlayer * >( subject )->IsGhost() )
		return true;
	if ( subject->IsInStasis() )
		return true;

	if ( IsInCommentaryMode() && !TheDirector->IsFinale() && subject->IsPlayer() && subject->GetTeamNumber() == TEAM_INFECTED )
	{
		CTerrorPlayer *player = static_cast< CTerrorPlayer * >( subject );
		int zombieClass = player->GetZombieClass();
		if ( zombieClass >= 1 && zombieClass <= 6 )
			return true;
	}

	if ( GetBot()->IsRangeGreaterThan( subject, 400.0f ) )
	{
		CBaseCombatCharacter *actor = subject->MyCombatCharacterPointer();
		if ( actor )
		{
			CNavArea *area = actor->GetLastKnownArea();
			if ( area && ( area->HasAttributes( 0x1000 ) || area->GetLightIntensity( actor->GetAbsOrigin() ) < 0.1f ) )
				return true;
		}
	}
	return !subject->IsAlive();
}

bool SurvivorVision::IsVisibleEntityNoticed( CBaseEntity *subject ) const
{
	return !IsIgnored( subject );
}

float SurvivorVision::GetDefaultFieldOfView() const
{
	return 90.0f;
}

float SurvivorVision::GetFieldOfView() const
{
	return m_fieldOfView;
}

void SurvivorVision::SetFieldOfView( float angle )
{
	// The original keeps a SECOND cache here, and omits degrees-to-radians
	// in this cache. Inherited IVision field-of-view tests still use the
	// separate base cache established by IVision::Reset. Do not 'repair'
	// that mismatch by forwarding this setter to IVision.
	m_fieldOfView = angle;
	m_cosHalfFieldOfView = cos( angle * 0.5f );
}
