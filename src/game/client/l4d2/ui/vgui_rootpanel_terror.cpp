//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: L4D2 client DLL panel ownership.
//
//=============================================================================//

#include "cbase.h"
#include "vgui_int.h"
#include "ienginevgui.h"
#include "vgui_controls/Panel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CTerrorRootPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CTerrorRootPanel, vgui::Panel );

public:
	CTerrorRootPanel( vgui::VPANEL parent ) : BaseClass( NULL, "TerrorRootPanel" )
	{
		SetParent( parent );
		SetPaintEnabled( false );
		SetPaintBorderEnabled( false );
		SetPaintBackgroundEnabled( false );
		SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
	}

protected:
	virtual void OnScreenSizeChanged( int nOldWide, int nOldTall ) OVERRIDE
	{
		BaseClass::OnScreenSizeChanged( nOldWide, nOldTall );
		SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
	}
};

static CTerrorRootPanel *g_pRootPanel = NULL;

void VGUI_CreateClientDLLRootPanel()
{
	g_pRootPanel = new CTerrorRootPanel( enginevgui->GetPanel( PANEL_CLIENTDLL ) );
}

void VGUI_DestroyClientDLLRootPanel()
{
	delete g_pRootPanel;
	g_pRootPanel = NULL;
}

vgui::VPANEL VGui_GetClientDLLRootPanel()
{
	return g_pRootPanel ? g_pRootPanel->GetVPanel() : 0;
}
