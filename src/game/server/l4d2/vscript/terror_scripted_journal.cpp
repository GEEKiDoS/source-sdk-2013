//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/vscript/terror_scripted_context.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/infected/common/infected.h"
#include "l4d2/infected/boss/witch.h"
#include "l4d2/director/zombiemanager.h"
#include "NextBot/NextBotManager.h"
#include "tier0/memdbgon.h"

namespace
{
	FileHandle_t NativeJournalInvalidHandle() { return reinterpret_cast< FileHandle_t >( (intp)-1 ); }
	template< int N > void JournalStart( char (&buffer)[N], const char *format, ... )
	{
		va_list args; va_start( args, format ); Q_vsnprintf( buffer, N, format, args ); va_end( args );
	}
	template< int N > void JournalAppend( char (&buffer)[N], const char *format, ... )
	{
		int length = Q_strlen( buffer ); if ( length >= N ) length = 0;
		va_list args; va_start( args, format ); Q_vsnprintf( buffer + length, N - length, format, args ); va_end( args );
	}
	void JournalWrite( FileHandle_t file, const char *text ) { filesystem->Write( text, Q_strlen( text ), file ); }
	void JournalPlayers( FileHandle_t file, const char *name, const CUtlVector< CTerrorPlayer * > &players, bool close )
	{
		char buffer[1024];
		JournalStart( buffer, "\t\t%s = [ ", name );
		FOR_EACH_VEC( players, i )
		{
			CTerrorPlayer *player = players[i];
			const Vector &pos = player->GetAbsOrigin();
			int health = player->GetHealth() * ( player->IsIncapacitated() ? -1 : 1 );
			JournalAppend( buffer, "%s[%d, %d, %.1f, %.1f, %.1f, %d] ", i ? ", " : "", player->entindex(), player->GetZombieClass(), pos.x, pos.y, pos.z, health );
		}
		if ( close ) JournalAppend( buffer, "]\n" );
		JournalWrite( file, buffer );
	}
	struct CollectJournalCommons
	{
		CUtlVector< Infected * > list;
		bool operator()( INextBot *bot )
		{
			CBaseCombatCharacter *actor = bot->GetEntity();
			if ( actor )
			{
				Infected *infected = actor->MyInfectedPointer();
				if ( infected && ( infected->GetTeamNumber() != TEAM_INFECTED || infected->GetZombieClass() == 0 ) && !infected->IsEFlagSet( EFL_KILLME ) )
					list.AddToTail( infected );
			}
			return true;
		}
	};
}
CTerrorScriptedJournal::CTerrorScriptedJournal()
{
	// Native2C6580 only writes enabled/timer/scope; the containing allocation
	// is zeroed. Zero and native's later -1 file sentinel are distinct states.
	m_bEnabled = false; m_UpdateTimer.Invalidate(); m_nScope = 3;
	m_hFile = NULL; m_hFunction = NULL; m_flInterval = 0.0f; m_nNative32 = 0;
}
void CTerrorScriptedJournal::Toggle()
{
	m_bEnabled = !m_bEnabled;
	m_UpdateTimer.Invalidate();
	if ( !m_bEnabled || m_hFile != NativeJournalInvalidHandle() )
	{
		JournalWrite( m_hFile, "]\n" );
		filesystem->Close( m_hFile );
		m_hFile = NativeJournalInvalidHandle();
		DevMsg( "Closing journal file\n" );
	}
}
void CTerrorScriptedJournal::Reset()
{
	if ( m_bEnabled ) Toggle();
	m_flInterval = 0.5f;
}
void CTerrorScriptedJournal::Update()
{
	if ( m_bEnabled && ( m_UpdateTimer.m_timestamp <= 0.0f || gpGlobals->curtime >= m_UpdateTimer.m_timestamp ) ) WriteFrame();
}
void CTerrorScriptedJournal::WriteFrame()
{
	char buffer[2048];
	if ( m_hFile == NativeJournalInvalidHandle() )
	{
		char filename[20] = "c:/l4journal000.nut";
		int index = 0;
		while ( filesystem->FileExists( filename ) )
		{
			++index;
			filename[12] = index / 100 + '0'; filename[13] = ( index % 100 ) / 10 + '0'; filename[14] = index % 10 + '0';
			if ( index >= 999 ) break;
		}
		// Native tests >999 after stopping at >=999: file999 may be overwritten.
		if ( index > 999 ) { Warning( "Out of Journal Filenames! Go Clean Up!\n" ); return; }
		DevMsg( "Journaling to %s\n", filename );
		m_hFile = filesystem->Open( filename, "w" );
		JournalStart( buffer, "JournalModeName <- \"%s\"\nJournalMapName <- \"%s\"\n", TheDirector->GetModeDescription()->GetName(), STRING( gpGlobals->mapname ) );
		CUtlVector< CTerrorPlayer * > players;
		CollectTerrorPlayers( players, TEAM_SURVIVOR, 2, 3, 9 );
		JournalAppend( buffer, "JournalPlayers <- [" );
		FOR_EACH_VEC( players, i ) JournalAppend( buffer, "%s {idx=%d,name=\"%s\"}", i ? "," : "", players[i]->entindex(), players[i]->GetPlayerName() );
		JournalAppend( buffer, " ]\n\nJournalDataStream <-\n[\n" );
		JournalWrite( m_hFile, buffer );
		m_hFunction = g_pScriptVM->LookupFunction( "JournalFunc", TheDirector->GetScriptScope( m_nScope ), true );
	}
	CUtlVector< CTerrorPlayer * > infected, survivors;
	CollectTerrorPlayers( infected, TEAM_INFECTED, 2, 3, 9 );
	float cooldown = TerrorGameRules()->GetHoldoutCooldownEndTime() - gpGlobals->curtime;
	if ( cooldown <= 0.0f ) cooldown = -1.0f;
	JournalStart( buffer, "\t{ Time = %.2f\n", gpGlobals->curtime );
	JournalAppend( buffer, "\t\tCooldown = %.2f\n", cooldown );
	JournalAppend( buffer, "\t\tCommons = %d\n", GetCommonInfectedCount( 0 ) );
	JournalAppend( buffer, "\t\tSpecials = %d\n", infected.Count() );
	int commonLimit = TheDirector->GetCommonLimit();
	int specialLimit = TheDirector->GetMaxSpecialCount();
	JournalAppend( buffer, "\t\tLimits = [ %d, %d ]\n", commonLimit, specialLimit );
	JournalAppend( buffer, "\t\tWave = %d\n", 0 );
	JournalAppend( buffer, "\t\tStageType = %d\n", TheDirector->GetScenario()->GetFinaleStage() );
	JournalAppend( buffer, "\t\tStageTimer = %.2f\n", TheDirector->GetScenario()->GetStageTimeRemaining() );
	JournalWrite( m_hFile, buffer );
	CollectTerrorPlayers( survivors, TEAM_SURVIVOR, 2, 3, 9 );
	JournalPlayers( m_hFile, "players", survivors, true );
	JournalPlayers( m_hFile, "mobs", infected, false );
	JournalStart( buffer, "%s", infected.Count() ? "," : "" );
	int witches = 0;
	const CUtlVector< Witch * > &list = GetWitchList();
	FOR_EACH_VEC( list, i )
	{
		Witch *witch = list[i];
		if ( !witch->IsAlive() ) continue;
		const Vector &pos = witch->GetAbsOrigin();
		JournalAppend( buffer, "%s[%d, %d, %.1f, %.1f, %.1f, %d] ", witches ? ", " : "", witch->entindex(), witch->GetZombieClass(), pos.x, pos.y, pos.z, witch->GetHealth() );
		++witches;
	}
	JournalAppend( buffer, "]\n" ); JournalWrite( m_hFile, buffer );
	CollectJournalCommons commons;
	TheNextBots().ForEachBot( commons );
	JournalStart( buffer, "\t\t%s = [ ", "commons" );
	FOR_EACH_VEC( commons.list, i )
	{
		const Vector &pos = commons.list[i]->GetAbsOrigin();
		JournalAppend( buffer, "%s[%d,%d,%d] ", i ? ", " : "", (int)pos.x, (int)pos.y, (int)pos.z );
	}
	JournalAppend( buffer, "]\n" ); JournalWrite( m_hFile, buffer );
	JournalStart( buffer, "\t\tTimers = [ " );
	for ( int i = 0; i < 6; ++i )
	{
		double remaining;
		if ( ceil( TheDirector->GetDirectorTimerRemaining( i ) ) < 0.0 ) remaining = 0.0;
		else remaining = ceil( TheDirector->GetDirectorTimerRemaining( i ) );
		JournalAppend( buffer, "%s%.2f", i ? ", " : "", remaining );
	}
	for ( int i = 0; i < 20; ++i )
		JournalAppend( buffer, ",%.2f", i >= TheDirector->GetSpecialSpawnTimerCount() ? -1.0 : TheDirector->GetSpecialSpawnTimerRemaining( i ) );
	JournalAppend( buffer, "]\n" ); JournalWrite( m_hFile, buffer );
	if ( m_hFunction && g_pScriptVM->Call( m_hFunction, TheDirector->GetScriptScope( m_nScope ), true, NULL ) == SCRIPT_DONE )
	{
		ScriptVariant_t value;
		if ( g_pScriptVM->GetValue( TheDirector->GetOptionsScope() == INVALID_HSCRIPT ? NULL : TheDirector->GetOptionsScope(), "JournalString", &value ) && value.GetType() == FIELD_CSTRING )
		{
			JournalStart( buffer, "\t\tcustom = %s\n", (const char *)value ); JournalWrite( m_hFile, buffer );
		}
		value.Free();
	}
	JournalStart( buffer, "\t\t}\n" ); JournalWrite( m_hFile, buffer );
	m_UpdateTimer.Start( 0.5f );
}
CON_COMMAND_F( scriptedmode_journal, "Toggle journaling - defaults to logging special location.", 0x00004006 )
{
	TheDirector->GetScriptedMode()->GetJournal()->Toggle();
}
