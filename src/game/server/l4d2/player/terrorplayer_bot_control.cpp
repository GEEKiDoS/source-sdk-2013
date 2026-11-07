//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"

#include "tier0/memdbgon.h"

// S:0x30AE60. The max-health mutation is intentionally not undone when
// statue mode is disabled; retail restores current health to one only.
void CTerrorPlayer::InputSetCommentaryStatueMode( inputdata_t &inputdata )
{
	const bool statue = inputdata.value.Bool();
	if ( m_isCommentaryStatue == statue )
		return;
	m_isCommentaryStatue = statue;
	if ( statue )
	{
		AddFlag( FL_FROZEN );
		SetMaxHealth( 1048576 );
		SetHealth( 1048576 );
	}
	else
	{
		RemoveFlag( FL_FROZEN );
		SetHealth( 1 );
	}
}
