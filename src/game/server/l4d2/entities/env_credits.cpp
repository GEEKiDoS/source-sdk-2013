//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/nav/terror_nav_area.h"
#include "recipientfilter.h"
#include "usermessages.h"
#include "tier1/KeyValues.h"
#include "l4d2/entities/env_credits_console_server_cvars.inc"
#include "tier0/memdbgon.h"

// Sole definition belongs to the GapConsoleServer console packet.
extern ConVar z_credits_interval;

class CEnvCredits : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvCredits, CPointEntity );
	DECLARE_DATADESC();
	virtual void Spawn() OVERRIDE;
	void InputStart( inputdata_t &input );
	void CreditsThink();
private:
	void SendNextLine( CTerrorPlayer *player );
	CUtlVector< string_t > m_lines;
};
LINK_ENTITY_TO_CLASS( env_credits, CEnvCredits );
BEGIN_DATADESC( CEnvCredits )
	DEFINE_INPUTFUNC( FIELD_VOID, "Start", InputStart ),
	DEFINE_THINKFUNC( CreditsThink ),
END_DATADESC()

void CEnvCredits::Spawn()
{
	// S25AAF0: no base Spawn, no precache, no HUD start message.
	SetSolid( SOLID_NONE );
	SetMoveType( MOVETYPE_NONE );
}

static void CollectCreditsNames( KeyValues *credits, const char *section, CUtlVector< string_t > &names )
{
	KeyValues *group = credits->FindKey( section, false );
	if ( !group ) return;
	for ( KeyValues *name = group->GetFirstSubKey(); name; name = name->GetNextKey() )
		names.AddToTail( AllocPooledString( name->GetName() ) );
}
static void AppendRandomCredit( CUtlVector< string_t > &source, CUtlVector< string_t > &destination )
{
	if ( source.Count() )
	{
		const int index = RandomInt( 0, source.Count() - 1 );
		destination.AddToTail( source[index] );
		source.FastRemove( index );
	}
}

void CEnvCredits::InputStart( inputdata_t &input )
{
	// S25ADE0: Start resets the list, never any player's per-player cursor.
	SetThink( &CEnvCredits::CreditsThink );
	SetNextThink( gpGlobals->curtime );
	m_lines.RemoveAll();
	CUtlVector< string_t > turtleRock, contributors, valve;
	KeyValues *credits = new KeyValues( "credits" );
	credits->LoadFromFile( filesystem, "credits.txt", "MOD" );
	CollectCreditsNames( credits, "Turtle Rock Studios", turtleRock );
	CollectCreditsNames( credits, "Valve Contributors", contributors );
	CollectCreditsNames( credits, "Other Contributors", contributors );
	KeyValues *group = credits->FindKey( "Valve", false );
	if ( group )
	{
		for ( KeyValues *name = group->GetFirstSubKey(); name; name = name->GetNextKey() )
		{
			const string_t pooled = AllocPooledString( name->GetName() );
			if ( contributors.Find( pooled ) == contributors.InvalidIndex() )
				valve.AddToTail( pooled );
		}
	}
	credits->deleteThis();
	while ( turtleRock.Count() || contributors.Count() )
	{
		for ( int i = 0; i < 4; ++i )
			AppendRandomCredit( turtleRock, m_lines );
		AppendRandomCredit( contributors, m_lines );
	}
	while ( valve.Count() )
		AppendRandomCredit( valve, m_lines );
}

static CTerrorPlayer *CreditsPlayerByIndex( int index )
{
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_PlayerByIndex( index ) );
	// S25ABC0/25AC50: includes bots and disconnect-in-progress, excludes
	// PICKINGTEAM. The send pass has no team filter; team2 gates the checkpoint.
	return player && player->edict() && player->entindex() && player->IsPlayer() && player->State_Get() != STATE_PICKINGTEAM ? player : NULL;
}

#pragma float_control( precise, on, push )
void CEnvCredits::CreditsThink()
{
	bool inCheckpoint = false;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = CreditsPlayerByIndex( i );
		if ( !player || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		if ( player->IsAlive() )
		{
			TerrorNavArea *area = ToTerrorNavArea( player->GetLastKnownArea() );
			if ( area && area->HasSpawnAttributes( TERROR_NAV_CHECKPOINT ) )
			{
				inCheckpoint = true;
				break;
			}
		}
	}
	if ( inCheckpoint )
	{
		const float interval = z_credits_interval.GetFloat();
		// S25AD00 JNB: unordered interval also takes the send/schedule path.
		if ( !( interval <= 0.0f ) )
		{
			SetNextThink( gpGlobals->curtime + interval );
			for ( int i = 1; i <= gpGlobals->maxClients; ++i )
			{
				CTerrorPlayer *player = CreditsPlayerByIndex( i );
				if ( player ) SendNextLine( player );
			}
		}
	}
}
#pragma float_control( pop )

void CEnvCredits::SendNextLine( CTerrorPlayer *player )
{
	const int index = player->m_nextCreditsLineIndex;
	if ( index < m_lines.Count() )
	{
		CSingleUserRecipientFilter filter( player );
		UserMessageBegin( filter, "CreditsLine" );
			// Only the undefined negative-index read becomes NULL_STRING;
			// the native message and cursor increment still occur.
			WRITE_STRING( STRING( index >= 0 ? m_lines[index] : NULL_STRING ) );
		MessageEnd();
		player->m_nextCreditsLineIndex = index + 1;
	}
}
