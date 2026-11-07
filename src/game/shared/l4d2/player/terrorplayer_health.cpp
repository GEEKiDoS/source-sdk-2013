//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#ifdef CLIENT_DLL
#include "l4d2/player/c_terrorplayer.h"
#else
#include "l4d2/player/terrorplayer.h"
#include "l4d2/gamerules/terror_gamerules.h"
#endif
#include "tier0/memdbgon.h"

ConVar pain_pills_decay_rate( "pain_pills_decay_rate", "0.27", 
#if defined( CLIENT_DLL )
	0x00006008
#elif defined( GAME_DLL )
	0x00006004
#else
	FCVAR_REPLICATED | FCVAR_CHEAT
#endif
 );

#if defined( CLIENT_DLL )
ConVar survivor_incap_health( "survivor_incap_health", "300", 0x0000200A );
#endif

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif
// S:0x3022A0. The authoritative float is not rounded to its ten-bit wire
// representation. In particular, elapsed time is not clamped before subtraction.
float CTerrorPlayer::GetHealthBuffer() const
{
	float buffer = m_healthBuffer;
	if ( buffer > 0.0f )
	{
		buffer -= ( gpGlobals->curtime - m_healthBufferTime ) * pain_pills_decay_rate.GetFloat();
		if ( !( buffer > 0.0f ) )
			buffer = 0.0f;
	}
#ifndef CLIENT_DLL
	// The original getter has this observable server-side effect: one five-point
	// notification per call, not a loop catching up all expired five-point bands.
	if ( (float)m_lastHealthBuffer > buffer )
	{
		m_lastHealthBuffer = static_cast< int >( static_cast< unsigned int >( m_lastHealthBuffer ) - 5u );
		IGameEvent *event = gameeventmanager->CreateEvent( "player_hurt" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetInt( "health", MAX( 0, GetHealth() ) );
			event->SetInt( "attacker", 0 );
			gameeventmanager->FireEvent( event );
		}
	}
#endif
	return buffer;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

// S:0x325540 tests both base lifeState and the independent incap bit.
bool CTerrorPlayer::IsIncapacitated() const
{
	return m_lifeState == LIFE_ALIVE && m_isIncapacitated;
}

bool CTerrorPlayer::IsGoingToDie() const
{
#ifdef CLIENT_DLL
	// C269250's positive <= differs from server2C8E90 for a NaN buffer.
	return m_isGoingToDie && GetHealthBuffer() <= 0.0f && GetHealth() <= 1;
#else
	if ( !m_isGoingToDie || GetHealthBuffer() > 0.0f )
		return false;
	return GetHealth() <= 1;
#endif
}

bool CTerrorPlayer::IsHangingFromLedge() const
{
	return m_isHangingFromLedge;
}

bool CTerrorPlayer::IsFallingFromLedge() const
{
	return m_isFallingFromLedge;
}

bool CTerrorPlayer::IsOnThirdStrike() const
{
#ifdef CLIENT_DLL
	return m_bIsOnThirdStrike;
#else
	// S:0x303940. The received presentation bit is independent of this
	// Director-option-backed server query.
	return m_currentReviveCount >= TerrorGameRules()->GetSurvivorMaxIncapacitatedCount();
#endif
}

bool CTerrorPlayer::IsAdrenalineActive() const
{
	return m_bAdrenalineActive;
}

// S:0x2C7B60 and S:0x2C7BE0 return the actual serial-validated participants.
CTerrorPlayer *CTerrorPlayer::GetPounceAttacker() const
{
	return static_cast<CTerrorPlayer *>( m_pounceAttacker.Get() );
}

CTerrorPlayer *CTerrorPlayer::GetTongueOwner() const
{
	return static_cast<CTerrorPlayer *>( m_tongueOwner.Get() );
}

// These are actual received handles. Their attack producers belong to M5;
// absence of a producer must not be implemented as a constant-false query.
bool CTerrorPlayer::IsBeingPounced() const
{
	return m_pounceAttacker.Get() != NULL;
}

bool CTerrorPlayer::IsTongueVictim() const
{
	return m_tongueOwner.Get() != NULL;
}

bool CTerrorPlayer::IsPummelVictim() const
{
	return m_pummelAttacker.Get() != NULL;
}

bool CTerrorPlayer::IsJockeyVictim() const
{
	return m_jockeyAttacker.Get() != NULL;
}

bool CTerrorPlayer::IsBeingCarried() const
{
	return m_carryAttacker.Get() != NULL;
}

bool CTerrorPlayer::IsGhost() const
{
	return m_isGhost;
}

// S:0x0F3750. Eligibility is separate from the use owner's claim check.
bool CTerrorPlayer::CanBeRevived() const
{
#ifdef CLIENT_DLL
	// C:0x0FF5B0 differs from S:0x0F3750, including tongue phase and jockey.
	return IsIncapacitated() && !m_isHangingFromTongue &&
		!IsBeingPounced() && !IsJockeyVictim() && !IsPummelVictim();
#else
	return !IsBeingPounced() && !IsPummelVictim() &&
		( !IsTongueVictim() || IsHangingFromLedge() ) && IsIncapacitated();
#endif
}
