//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GLOW_PROPERTY_H
#define TERROR_GLOW_PROPERTY_H
#ifdef _WIN32
#pragma once
#endif

#include "tier1/utlvector.h"

class CGlowProperty;
class C_BasePlayer;

// Native CGlowManager registry: unique registration, fast removal on type0
// and Term. This is not SDK2013's CGlowObjectManager object-definition list.
const CUtlVector< CGlowProperty * > &TerrorGlow_GetProperties();

// The caller supplies the observer-resolved player for the ACTIVE render
// slot (native 0x10251B50), not an arbitrary local-player slot or view origin.
// Preconditions are the native manager's nonnull property owner/player tests.
bool TerrorGlow_PropertyInRange( const CGlowProperty &glow, C_BasePlayer *pPlayer );
bool TerrorGlow_PropertyUsableInView( const CGlowProperty &glow, C_BasePlayer *pPlayer,
	float useTolerance );

// Native 0x10273210 draw-only tests; these do not alter manager eligibility.
bool TerrorGlow_PropertyMatchesTeam( const CGlowProperty &glow, C_BasePlayer *pPlayer );
float TerrorGlow_PropertyBackfaceScale( const CGlowProperty &glow, C_BasePlayer *pPlayer,
	float backfaceMultiplier );

#endif // TERROR_GLOW_PROPERTY_H
