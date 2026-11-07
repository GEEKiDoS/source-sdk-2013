//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/cs_player.h"
#include "recipientfilter.h"
#include "tier0/memdbgon.h"

// S:2418D0. The parameter lookup is the original admission guard; emission
// still uses the real script sound path and the actual single-user filter.
void CCSPlayer::EmitPrivateSound( const char *sound )
{
	CSoundParameters parameters;
	if ( GetParametersForSound( sound, parameters, NULL ) )
	{
		CSingleUserRecipientFilter filter( this );
		EmitSound( filter, entindex(), sound, NULL, 0.0f, NULL );
	}
}
