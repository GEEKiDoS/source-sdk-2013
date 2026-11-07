#include "cbase.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/infected/common/zombiebot_vision.h"
#include "tier0/memdbgon.h"

// S3FD340: canonical single recognized-victim handle, not a fabricated
// CKnownEntity wrapper or a new nearest-player scan.
CBaseCombatCharacter *ZombieBotVision::GetPrimaryKnownThreat() const
{
	return m_recognizedVictim.Get();
}

// S3FD380: every non-infected team value is admitted, including TEAM_ANY.
CBaseEntity *ZombieBotVision::GetClosestKnown( int team ) const
{
	return team != TEAM_INFECTED ? m_recognizedVictim.Get() : NULL;
}

// S3FD430 genuinely IGNORES the supplied filter. Do not strengthen it.
CBaseEntity *ZombieBotVision::GetClosestKnown( const INextBotEntityFilter &filter ) const
{
	return m_recognizedVictim.Get();
}

// S3FD3C0 has TWO arguments. Zero/negative range bypasses the test, unlike
// base IVision's strictly negative bypass. Distance equality is included.
int ZombieBotVision::GetKnownCount( int team, float rangeLimit ) const
{
	return team != TEAM_INFECTED && m_recognizedVictim.Get() &&
		( rangeLimit <= 0.0f || m_recognizedRangeSquared <= rangeLimit * rangeLimit ) ? 1 : 0;
}

// S3FD470 compares the actual timer timestamp directly; HasStarted()/a
// generic interval fallback would change the native NaN path.
float ZombieBotVision::GetTimeSinceVisible( int team ) const
{
	if ( team == TEAM_INFECTED ) return 0.0f;
	const float timestamp = m_visibleTimer.GetStartTime();
	return timestamp <= 0.0f ? FLT_MAX : gpGlobals->curtime - timestamp;
}
