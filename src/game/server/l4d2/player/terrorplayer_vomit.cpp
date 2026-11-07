//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/weapons/vomitjar_projectile.h"
#include "l4d2/sound/terror_music.h"
#include "NextBot/NextBotManager.h"
#include "rumble_shared.h"
#include "tier0/memdbgon.h"

ConVar survivor_it_duration( "survivor_it_duration", "20", 0x00006004 );
extern bool IsInCommentaryMode();

void CTerrorPlayer::OnVomitedUpon( CTerrorPlayer *attacker, bool exploded )
{
	if ( IsInCommentaryMode() && !IsBot() )
		return;
	if ( m_vomitStart == 0.0f || gpGlobals->curtime > m_vomitFadeStart )
		m_vomitStart = gpGlobals->curtime;
	m_vomitFadeStart = gpGlobals->curtime + z_vomit_fade_start.GetFloat();
	float pitch = RandomFloat( z_vomit_hit_pitch_min.GetFloat(), z_vomit_hit_pitch_max.GetFloat() );
	float yaw = RandomFloat( z_vomit_hit_yaw_min.GetFloat(), z_vomit_hit_yaw_max.GetFloat() );
	SetPunchAngle( QAngle( pitch, yaw, 0.0f ) );
	if ( !IsIT() )
	{
		TheNextBots().OnInfectedIT( this );
		GetMusic()->OnVomitedUpon();
	}
	// Retail also increments the victim's own-character entry. The attacker
	// producer separately maintains totals/checkpoint counts; do not merge them.
	unsigned int character = GetSurvivorCharacter();
	if ( character < 4 )
	{
		if ( exploded )
			++m_survivorVomitExplosions[character];
		else
			++m_survivorVomitHits[character];
	}
	m_hITSource = attacker;
	m_bITFromBoomer = exploded;
	m_itTimer.Start( survivor_it_duration.GetFloat() );
	if ( m_vomitDamageSourceTimer.GetRemainingTime() <= 0.0f )
	{
		static ConVarRef vocalNoise( "z_noise_level_vocalize" );
		SetNoiseLevel( vocalNoise.GetFloat() );
		SpeakIfAllowed( "PlayerVomitInFace" );
		m_vocalizeTimer.Start( 1.5f );
	}
	m_hVomitDamageSource = attacker;
	m_vomitDamageSourceTimer.Start( 3.0f );
	RumbleEffect( 44, 0, RUMBLE_FLAG_RESTART );
}
