// Shared declarations for the existing native client color owner.
#ifndef TERROR_CLIENT_COLORS_H
#define TERROR_CLIENT_COLORS_H
#ifdef _WIN32
#pragma once
#endif
#include "Color.h"
class C_BasePlayer;
extern Color g_ColorSurvivor;
extern Color g_ColorZombie;
Color GetClientColor( int clientIndex ); // Native C22C1B0.
Color GetPlayerColor( C_BasePlayer *player ); // Native C287210.
#endif
