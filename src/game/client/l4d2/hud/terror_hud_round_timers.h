//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_HUD_ROUND_TIMERS_H
#define TERROR_HUD_ROUND_TIMERS_H
#ifdef _WIN32
#pragma once
#endif
// Native C2C4410. A owns the sole implementation; rows/mode boards consume it.
void SplitTerrorSurvivalTime( float time, int &minutes, int &seconds, int &hundredths );
#endif
