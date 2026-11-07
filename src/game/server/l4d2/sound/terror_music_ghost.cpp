//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/terror_music.h"
#include "l4d2/player/terrorplayer.h"
#include "recipientfilter.h"
#include "engine/IEngineSound.h"
#include "tier0/memdbgon.h"

// S:2AAE30 / Music v+12. This does NOT reset Music state, stop tracks or
// clear adrenaline; those are separate real callbacks on the same owner.
void Music::ClearPlayerDSP()
{
	if ( m_me->IsNetClient() )
	{
		CSingleUserRecipientFilter filter( m_me );
		enginesound->SetPlayerDSP( filter, 0, false );
	}
}

// Real six native writers: 2AD030/2ACBA0/2AD540/2AE370/2AD9D0/2ADE60.
// The class determines the FIRST random timer and the following fixed order.
// Existing UpdateSpecialProximity's native2ACBA0 call passes class2 explicitly.
void Music::PostponeSpecialProximity( int zombieClass )
{
	if ( zombieClass < 1 || zombieClass > 6 ) return; // Actual322310 switch guard.
	static const int order[6][6] =
	{
		{ 1, 3, 5, 6, 4, 2 },
		{ 2, 1, 3, 5, 4, 6 },
		{ 3, 5, 4, 6, 2, 1 },
		{ 4, 6, 2, 1, 3, 5 },
		{ 5, 4, 6, 2, 1, 3 },
		{ 6, 2, 1, 3, 5, 4 }
	};
	CountdownTimer &first = m_specialAlertTimer[order[zombieClass - 1][0]];
	if ( (double)first.m_timestamp - (double)gpGlobals->curtime < 0.5 ) first.Start( RandomFloat( 0.5f, 1.0f ) );
	// Native comparisons use DOUBLE literals; starts add the corresponding
	// FLOAT constants and store float duration/deadline. Do not merge them.
	static const double threshold[5] = { 1.5, 1.6, 1.7, 1.8, 1.9 };
	for ( int i = 0; i < 5; ++i )
	{
		CountdownTimer &timer = m_specialAlertTimer[order[zombieClass - 1][i + 1]];
		if ( (double)timer.m_timestamp - (double)gpGlobals->curtime < threshold[i] ) timer.Start( (float)threshold[i] );
	}
}
