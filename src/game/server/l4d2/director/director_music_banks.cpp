//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/director/director_music_banks.h"
#include "filesystem.h"
#include "tier1/KeyValues.h"
#include "tier0/memdbgon.h"

// Native constructor policy adds DEVELOPMENTONLY to these raw-zero ConVars.
static ConVar music_bank_min_duration( "music_bank_min_duration", "900", 0x00000006 );
static ConVar music_bank_max_duration( "music_bank_max_duration", "900", 0x00000006 );

DirectorMusicBanks::DirectorMusicBanks() : m_remainingTime( 0.0f ) {}
void DirectorMusicBanks::OnMapStart() { m_remainingTime = 0.0f; }
void DirectorMusicBanks::OnRoundEnd( bool saveRemainingTime )
{
	if ( saveRemainingTime )
		m_remainingTime = m_bankTimer.GetRemainingTime();
}
void DirectorMusicBanks::Bank::Clear()
{
	for ( int i = 0; i < events.Count(); ++i ) delete[] events[i];
	events.RemoveAll();
}
void DirectorMusicBanks::Bank::Load( const char *prefix, KeyValues *sounds )
{
	Clear();
	const int prefixLength = Q_strlen( prefix );
	for ( KeyValues *sound = sounds; sound; )
	{
		// Native S28531F jumps to S2853E8 without advancing when this is
		// childless. Preserve that original malformed-key stall, not a skip.
		if ( !sound->GetFirstSubKey() )
			continue;
		if ( !Q_strnicmp( sound->GetName(), prefix, prefixLength ) )
		{
			// S10102640 interns a game string; it does NOT precache audio.
			AllocPooledString( sound->GetName() );
			const int length = Q_strlen( sound->GetName() ) + 1;
			char *name = new char[length];
			Q_memcpy( name, sound->GetName(), length );
			events.AddToTail( name );
		}
		sound = sound->GetNextKey();
	}
	// Preserve the original random calls, including the fast-removal truncation.
	const int last = events.Count() - 1;
	for ( int i = 0; i < last; ++i )
		V_swap( events[i], events[RandomInt( i, last )] );
	while ( events.Count() > 4 )
	{
		delete[] events[0];
		events.FastRemove( 0 );
	}
}
void DirectorMusicBanks::Bank::ShuffleForNextCycle()
{
	// Original intentionally stops at Count()-2 (not Count()-1).
	for ( int i = 0; i < events.Count() - 2; ++i )
		V_swap( events[i], events[RandomInt( i, events.Count() - 1 )] );
}
void DirectorMusicBanks::OnRoundStart( bool isRestartingScenario )
{
	if ( isRestartingScenario )
	{
		m_bankTimer.Start( m_remainingTime );
		return;
	}
	// S2854F0 owns this temporary tree; it is NOT a sound-registry reload.
	KeyValues *sounds = new KeyValues( "game_sounds_music.txt" );
	if ( sounds->LoadFromFile( filesystem, "scripts/game_sounds_music.txt", NULL ) )
	{
		m_atmosphere.Load( "Event.DisturbingAtmosphere", sounds );
		m_silence.Load( "Event.MomentOfSilence", sounds );
		m_reveal.Load( "Event.Reveal", sounds );
		m_atmosphere.current = 0;
		m_silence.current = 0;
		m_reveal.current = 0;
	}
	sounds->deleteThis();
	m_bankTimer.Start( RandomFloat( music_bank_min_duration.GetFloat(), music_bank_max_duration.GetFloat() ) );
}
void DirectorMusicBanks::Update()
{
	if ( !( static_cast< double >( gpGlobals->curtime ) >= static_cast< double >( m_bankTimer.m_timestamp.Get() ) ) )
		return;
	m_bankTimer.Start( RandomFloat( music_bank_min_duration.GetFloat(), music_bank_max_duration.GetFloat() ) );
	Bank *banks[] = { &m_atmosphere, &m_silence, &m_reveal };
	for ( int i = 0; i < ARRAYSIZE( banks ); ++i )
	{
		Bank &bank = *banks[i];
		if ( ++bank.current >= bank.events.Count() )
		{
			bank.ShuffleForNextCycle();
			bank.current = 0;
		}
	}
}
