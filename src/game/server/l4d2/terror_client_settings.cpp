//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/gamerules/cs_gamerules.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/cs_player.h"
#include "l4d2/player/terrorplayer.h"
#include "recipientfilter.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

namespace
{
	// Identical reliable chat/event prefix of S2436F0 and S308830. The caller
	// controls the native difference: CCS copies32 bytes first; Terror does not.
	void BroadcastPlayerNameChange( CCSPlayer *player, const char *name )
	{
		const char *oldName = player->GetPlayerName();
		if ( !player->IsBot() )
		{
			CReliableBroadcastRecipientFilter filter;
			UTIL_SayText2Filter( filter, player, false, "#Cstrike_Name_Change", oldName, name );
			IGameEvent *event = gameeventmanager->CreateEvent( "player_changename" );
			if ( event )
			{
				event->SetInt( "userid", player->GetUserID() );
				event->SetString( "oldname", oldName );
				event->SetString( "newname", name );
				gameeventmanager->FireEvent( event );
			}
		}
	}
	bool NameTimeOrderedAtLeast( float left, float right )
	{
		//23FF0B/23FF24 COMISS rejects ordered >=; unordered is admitted.
		// Explicit IEEE ordering survives the target's /fp:fast transformations.
		unsigned int a = FloatBits( left ), b = FloatBits( right );
		if ( ( a & 0x7fffffffu ) > 0x7f800000u || ( b & 0x7fffffffu ) > 0x7f800000u ) return false;
		if ( ( a & 0x7fffffffu ) == 0u ) a = 0u;
		if ( ( b & 0x7fffffffu ) == 0u ) b = 0u;
		a = ( a & 0x80000000u ) ? ~a : a | 0x80000000u;
		b = ( b & 0x80000000u ) ? ~b : b | 0x80000000u;
		return a >= b;
	}
	bool IsPositiveNameDeadline( float deadline )
	{
		const unsigned int bits = FloatBits( deadline );
		return bits > 0u && bits <= 0x7f800000u;
	}
}

void CCSGameRules::ClientSettingsChanged( CBasePlayer *base )
{
	// S23E5A0. Do not also call CMultiplayRules: this owns the genuine name/hint chain.
	CCSPlayer *player = static_cast< CCSPlayer * >( base );
	const char *name = engine->GetClientConVarValue( player->entindex(), "name" );
	const char *current = player->GetPlayerName();
	if ( !current[0] ) player->SetPlayerName( name );
	if ( current[0] && Q_strncmp( current, name, 31 ) != 0 )
	{
		if ( player->CanChangeName() ) player->ChangeName( name );
		else
		{
			engine->ClientCommand( player->edict(), "name \"%s\"", current );
			ClientPrint( player, HUD_PRINTTALK, "#Name_change_limit_exceeded" );
		}
	}
	// Native9120 is reset TRUE after name handling on EVERY settings callback.
	player->m_bShowHints = true;
	if ( player->IsNetClient() )
	{
		const char *help = engine->GetClientConVarValue( player->entindex(), "cl_autohelp" );
		if ( help && atoi( help ) <= 0 ) player->m_bShowHints = false;
	}
}
void CTerrorGameRules::ClientSettingsChanged( CBasePlayer *player )
{
	// S2D9430: base/name/hints -> bot default FOV -> actual actor settings snapshot.
	BaseClass::ClientSettingsChanged( player );
	if ( player->IsBot() ) player->SetDefaultFOV( 90 );
	if ( player->IsPlayer() ) static_cast< CTerrorPlayer * >( player )->UpdateClientSettings();
}
bool CCSPlayer::CanChangeName()
{
	// S23FEE0: ADDSS stores, first COMISS/JB, then COMISS/JNB.
	if ( !IsBot() )
	{
		const float now = gpGlobals->curtime;
		const float recent = m_nameChangeHistory[0] + 10.0f;
		if ( NameTimeOrderedAtLeast( recent, now ) ) return false;
		const float oldest = m_nameChangeHistory[4] + 600.0f;
		if ( NameTimeOrderedAtLeast( oldest, now ) ) return false;
	}
	return true;
}
void CCSPlayer::ChangeName( const char *name )
{
	// S2436F0 copies before either broadcast or SetPlayerName, unlike Terror308830.
	char copiedName[32];
	Q_strncpy( copiedName, name, sizeof( copiedName ) );
	BroadcastPlayerNameChange( this, copiedName );
	SetPlayerName( copiedName );
	engine->ClientCommand( edict(), "name \"%s\"", copiedName );
	for ( int i = 4; i > 0; --i ) m_nameChangeHistory[i] = m_nameChangeHistory[i - 1];
	m_nameChangeHistory[0] = gpGlobals->curtime;
}
bool CTerrorPlayer::CanChangeName()
{
	// S303670 is genuinely ALWAYS true. It still queries the clock for a non-bot
	// with a positive deadline; that query is not an admission/rate-limit test.
	if ( !IsBot() && IsPositiveNameDeadline( m_nameChangeTimer.m_timestamp ) )
	{
		volatile double now = (double)gpGlobals->curtime;
		(void)now;
	}
	return true;
}
void CTerrorPlayer::ChangeName( const char *name )
{
	// S308830 has no CCS32-byte broadcast copy and no client name-rollback command.
	BroadcastPlayerNameChange( this, name );
	SetPlayerName( name );
	m_nameChangeTimer.m_timestamp = (float)( (double)gpGlobals->curtime + 10.0 );
	m_nameChangeTimer.m_duration = 10.0f;
}
void CTerrorPlayer::UpdateClientSettings()
{
	// S2FAB40 writes the SAME11596 owned KeyValues made by323260 and deleted325640.
	static const struct { const char *name, *botValue; } settings[] =
	{
		{ "fov_desired", "90" },
		{ "voice_spatialize", "0" }
	};
	for ( int i = 0; i < ARRAYSIZE( settings ); ++i )
	{
		if ( IsBot() && settings[i].botValue && settings[i].botValue[0] )
			m_pClientSettings->SetString( settings[i].name, settings[i].botValue );
		else
		{
			const char *value = engine->GetClientConVarValue( entindex(), settings[i].name );
			if ( value ) m_pClientSettings->SetString( settings[i].name, value );
		}
	}
}
