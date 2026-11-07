//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/nav/vgui_gamedll_int.h"
#include "ienginevgui.h"
#include "IGameUIFuncs.h"
#include "vgui/IVGui.h"
#include "vgui/IPanel.h"
#include "vgui_controls/Controls.h"
#include "vgui_controls/Panel.h"
#include "tier0/memdbgon.h"

IEngineVGui *enginevgui = NULL;
IGameUIFuncs *gameuifuncs = NULL;

bool VGui_GameDLL_Init()
{
	// S218A50. Tier3 has already connected all required interfaces. The target
	// controls initializer uses the same module name and the same six checks.
	return vgui::VGui_InitInterfacesList( "GAMEDLL", NULL, 0 );
}

void VGui_GameDLL_Shutdown()
{
	// S218AF0 is one RunFrame, NOT Stop/Shutdown of the shared engine runtime.
	vgui::ivgui()->RunFrame();
}

void ShowGameDLLPanel( vgui::Panel *panel )
{
	// S218A90. Actual target GetPanel(PANEL_GAMEDLL) is supported; no engine hook.
	const vgui::VPANEL root = enginevgui->GetPanel( PANEL_GAMEDLL );
	panel->SetParent( root );
	// Main-approved target API-shape adaptation: the engine only auto-shows this
	// real root under -tools. Expose it through the public VGUI panel interface.
	vgui::ipanel()->SetVisible( root, true );
	panel->SetVisible( true );
	panel->SetEnabled( true );
	panel->MoveToFront();
	panel->SetPos( 0, 0 );
}
