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

#endif // TERROR_BASEMODUI_H
