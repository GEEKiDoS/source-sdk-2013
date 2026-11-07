//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_HUD_HELPERS_H
#define TERROR_HUD_HELPERS_H
#ifdef _WIN32
#pragma once
#endif
class C_TerrorPlayer;
C_TerrorPlayer *CinematicViewPlayer(); // Existing clientmode owner251BB0; promote its linkage, no second body.
C_TerrorPlayer *PostProcessViewPlayer(); // Native251B50, IN_EYE only; same canonical owner.
int MapTerrorSurvivorDisplayCharacter( int character ); // Native286850/286580.
const char *GetTerrorSurvivorPortrait( int character, bool incapacitated = false );
const char *GetTerrorSurvivorDisplayNameToken( int character );
void GetTerrorHudPlayerName( C_TerrorPlayer *player, wchar_t *name, int nameBytes );
#endif
