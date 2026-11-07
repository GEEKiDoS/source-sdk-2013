//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_CLIENT_GAME_DETAILS_H
#define TERROR_CLIENT_GAME_DETAILS_H
#ifdef _WIN32
#pragma once
#endif
class KeyValues;
// Owned result from actual client level+replicated mode+sole mission dictionary.
// Native session identity parity is qualified when the mode/BSP lookup is not unique.
// No MatchSession/System/network/Server/Members services are claimed.
KeyValues *TerrorBuildConsumedClientGameProjection();
#endif
