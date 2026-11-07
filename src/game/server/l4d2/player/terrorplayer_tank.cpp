//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/boss/witch.h"

#include "l4d2/player/terrorplayer_tank_console_server_cvars.inc"
#include "tier0/memdbgon.h"

ConVar z_frustration_lifetime( "z_frustration_lifetime", "20", 0x00004004,
	"Frustration will accumulate for this many seconds before forcing an AI tank takeover" );
ConVar z_frustration_los_delay( "z_frustration_los_delay", "2", 0x00004004 );
ConVar z_frustration_spawn_delay( "z_frustration_spawn_delay", "10", 0x00004004 );

// S:0x30E660. Successful native claw/rock contacts use this same player state;
// neither a weapon-local counter nor survivor intensity owns Tank frustration.
void CTerrorPlayer::ResetFrustration()
{
	m_frustration = 0;
	m_frustrationGraceTimer.Start( 2.0f );
}

// S:0x41B2D0. Only the actual Witch slash producer assigns this handle.
void CTerrorPlayer::SetAttackingWitch( Witch *witch )
{
	m_witch = witch;
}

Witch *CTerrorPlayer::GetAttackingWitch() const
{
	return m_witch.Get();
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif
// S:0x303820. No arousal, harasser, same-team, or visibility test is present.
bool CTerrorPlayer::IsNearAttackingWitch() const
{
	Witch *witch = GetAttackingWitch();
	if ( !witch || !witch->IsAlive() )
		return false;
	const Vector origin = GetAbsOrigin();
	// S303820 re-resolves after the owner-origin update; skip only NULL use.
	witch = GetAttackingWitch();
	if ( !witch )
		return false;
	const Vector delta = witch->GetAbsOrigin() - origin;
	return ( delta.y * delta.y + delta.x * delta.x ) + delta.z * delta.z < 32400.0f;
}
#ifdef _MSC_VER
#pragma float_control( pop )
#endif
