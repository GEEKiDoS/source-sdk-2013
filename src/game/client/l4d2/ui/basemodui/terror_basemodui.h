//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_H
#define TERROR_BASEMODUI_H
#ifdef _WIN32
#pragma once
#endif

// Client-owned UI only. The target GameUI module and its root retain ownership.
bool InitTerrorBaseModUI();
void ShutdownTerrorBaseModUI();
void OpenTerrorGraphicsOptions( int section = 0 );
// Returns false while the native dialog cannot be presented; caller may retry.
bool ShowGpuIdentificationWarning();

#endif // TERROR_BASEMODUI_H
