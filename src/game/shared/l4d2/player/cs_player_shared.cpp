//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_cs_player.h"
#else
#include "l4d2/player/cs_player.h"
#endif
#include "l4d2/player/cs_player_shared_console_server_cvars.inc"
#include "tier0/memdbgon.h"

CSPlayerState CCSPlayer::State_Get() const
{
	return (CSPlayerState)(int)m_iPlayerState;
}

// S24B980 records every grounded result, even if the timestamp is unchanged.
void CCSPlayer::RecordLastStandingInfo( const Vector &position, const Vector &velocity, const QAngle &angles )
{
	if ( m_lastStandingTimer.GetStartTime() != gpGlobals->curtime )
		m_lastStandingTimer.Start();
	m_lastStandingPosition = position;
	m_lastStandingVelocity = velocity;
	m_lastStandingAngles = angles;
}

// S24BB70/C22DF00. The ladder normal alone has native transport.
void CCSPlayer::RecordLadderInfo( const Vector &position, const Vector &normal,
	const Vector &velocity, const QAngle &angles )
{
	if ( m_ladderInteractionTimer.GetStartTime() != gpGlobals->curtime )
		m_ladderInteractionTimer.Start();
	m_lastLadderPosition = position;
	m_lastLadderNormal = normal;
	m_lastLadderVelocity = velocity;
	m_lastLadderAngles = angles;
}

// S2A7B10 keeps the actual double subtraction, not a jump/land timer.
double CCSPlayer::GetTimeSinceLadderInteraction() const
{
	const float flTimestamp = m_ladderInteractionTimer.GetStartTime();
	return flTimestamp > 0.0f ?
		static_cast< double >( gpGlobals->curtime ) - static_cast< double >( flTimestamp ) :
		static_cast< double >( 99999.9f );
}

// S24B8C0/C22DD60 remembers the actual previous ladder, not a class/default.
bool CCSPlayer::CanRegrabLadder( const Vector &position, const Vector &normal ) const
{
	const Vector &lastNormal = m_lastLadderNormal;
	if ( lastNormal != normal )
		return true;
	const float flTimestamp = m_ladderInteractionTimer.GetStartTime();
	if ( !( flTimestamp > 0.0f ) )
		return true;
	const float flElapsed = static_cast< float >(
		static_cast< double >( gpGlobals->curtime ) - static_cast< double >( flTimestamp ) );
	if ( flElapsed > 1.0f )
		return true;
	const float x = position.x - m_lastLadderPosition.x;
	const float y = position.y - m_lastLadderPosition.y;
	return x * x + y * y >= 4096.0f;
}
