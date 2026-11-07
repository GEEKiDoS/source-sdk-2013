//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/terrorplayer.h"
#include "tier0/memdbgon.h"

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

bool CTerrorPlayer::CanBeShoved( CBaseEntity *pusher ) const // S2FB730
{
	return GetTeamNumber() != TEAM_INFECTED || GetZombieClass() != 6;
}
void CTerrorPlayer::SetVariantType( int variant ) // S30E310
{
	// Genuine CNetworkVar mutation only. Model, ability and class setup are
	// performed by the original caller, not a new subtype service.
	m_nVariantType = variant;
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
