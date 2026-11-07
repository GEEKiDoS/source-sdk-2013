//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef VGUI_GAMEDLL_INT_H
#define VGUI_GAMEDLL_INT_H
#ifdef _WIN32
#pragma once
#endif
#include "ienginevgui.h"
class IGameUIFuncs;
extern IGameUIFuncs *gameuifuncs;
namespace vgui { class Panel; }
// The engine owns the VGUI runtime and PANEL_GAMEDLL root. These are game DLL glue.
bool VGui_GameDLL_Init();
void VGui_GameDLL_Shutdown();
void ShowGameDLLPanel( vgui::Panel *panel );
#endif
