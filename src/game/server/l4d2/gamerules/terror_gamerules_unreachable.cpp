//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/special/spitter_projectile.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

// Actual792..816 fields, ctor and RestartRound resets are owned by M6's
// existing Rules lifecycle. These methods only produce/consume that state.
void CTerrorGameRules::OnInfectedBotPathFailed( float pressure, CTerrorPlayer *player ) // S:2DC5D0
{
	// No native team, alive or ghost gate. The caller selects a victim.
	if ( player && !player->IsBot() && !player->IsIncapacitated() && !player->m_isHangingFromLedge &&
		!player->m_tongueOwner.Get() && !player->m_pounceAttacker.Get() && !player->m_pummelAttacker.Get() && !player->m_jockeyAttacker.Get() )
	{
		m_hSurvivalExploitEntity = player;
		m_flSurvivalExploitAccumulator += pressure;
	}
}
void CTerrorGameRules::UpdateInfectedBotPathFailures() // S:2E18F5..1B84 within genuine Rules::Think2E0F20
{
	if ( !IsSurvivalMode() ) return; // metadata base is exactly"survival"
	if ( m_flSurvivalExploitAccumulator > 0.0f )
	{
		float increase = m_flSurvivalExploitAccumulator - m_flPreviousSurvivalExploitAccumulator;
		if ( increase > 0.0f )
		{
			if ( increase > 4.0f ) increase = 4.0f;
			const float pressure = increase + m_flPreviousSurvivalExploitAccumulator;
			m_flPreviousSurvivalExploitAccumulator = pressure;
			m_flSurvivalExploitAccumulator = pressure;
			m_flSurvivalExploitDecayTime = gpGlobals->curtime + 2.5f;
			CTerrorPlayer *human = ToTerrorPlayer( m_hSurvivalExploitEntity.Get() );
			if ( pressure > 16.0f && human )
			{
				if ( gpGlobals->curtime > m_flSurvivalExploitDamageTime )
				{
					CTakeDamageInfo info( human, human, 10.0f, DMG_GENERIC );
					human->TakeDamage( info );
					m_flSurvivalExploitDamageTime = gpGlobals->curtime + 1.0f;
				}
				if ( gpGlobals->curtime > m_flSurvivalExploitSpitTime )
				{
					++m_nSurvivalExploitSpitCount;
					const float x = random->RandomFloat( -10.0f, 10.0f );
					const float y = random->RandomFloat( -10.0f, 10.0f );
					CSpitterProjectile::Create( human->WorldSpaceCenter(), QAngle( -90.0f, 0.0f, 0.0f ), Vector( 0.0f, 0.0f, -5.0f ), AngularImpulse( x, y, 0.0f ), human ); //3E2E60
					m_flSurvivalExploitSpitTime = gpGlobals->curtime + 7.0f;
				}
			}
		}
		else if ( gpGlobals->curtime > m_flSurvivalExploitDecayTime )
		{
			float pressure = m_flSurvivalExploitAccumulator <= 8.0f ? m_flSurvivalExploitAccumulator : 8.0f;
			pressure -= gpGlobals->frametime;
			if ( pressure < 0.0f ) pressure = 0.0f;
			m_flSurvivalExploitAccumulator = pressure;
			m_flPreviousSurvivalExploitAccumulator = pressure;
		}
	}
	static ConVarRef cheats( "sv_cheats" );
	if ( cheats.GetBool() || m_nSurvivalExploitSpitCount >= 4 )
	{
		//2E0E10 is ForEachPlayer(action,true), NOT an EndRound callback.
		// Its stateless2E0980 action clears the EXISTING shared player field.
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && player->IsConnected() && !player->IsBot() ) player->m_bWasPresentAtSurvivalStart = false;
		}
	}
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
