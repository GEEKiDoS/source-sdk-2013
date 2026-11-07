//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_GAME_DETAILS_H
#define TERROR_GAME_DETAILS_H
#ifdef _WIN32
#pragma once
#endif
#include "tier1/netadr.h"
class KeyValues;

// Complete INPUT contract of original M9960. No default constructor deliberately:
// a full-details caller must supply a genuine engine/network/session snapshot.
// The current target adapter below does NOT claim to supply missing local-address,
// friends-advertisement or client-only state through guessed/default values.
struct TerrorGameDetailsSnapshot_t
{
	bool serverActive;
	bool dedicated;
	bool advertiseFriends;
	bool friendsAdvertisementAllowed;
	const char *serverName;
	const char *serverMap;
	netadr_t onlineAddress;
	netadr_t localAddress;
	int slots;
	int players;
	bool clientInMainMenu;
	const char *clientLevelName;
};

// Caller owns returned details. Session settings are BORROWED and unmodified.
KeyValues *TerrorBuildGameDetails( const TerrorGameDetailsSnapshot_t &snapshot, KeyValues *sessionSettings, KeyValues *request );
#endif
