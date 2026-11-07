//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef HUD_MENU_H
#define HUD_MENU_H
#ifdef _WIN32
#pragma once
#endif

#include "utlvector.h"
#include "hudelement.h"
#include <vgui_controls/Panel.h>

#define MENU_SELECTION_TIMEOUT	5.0f

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
class CHudMenu : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudMenu, vgui::Panel );
public:
	CHudMenu( const char *pElementName );
#ifdef L4D2
	virtual ~CHudMenu();
	struct ProcessedLine;
	class CHudMenuOptions
	{
	public:
		CHudMenuOptions() : m_center( false ) {}
		virtual void ProcessLine( int index, ProcessedLine &line, const wchar_t *text, int selectedItem, unsigned char alpha ) = 0;
		virtual void OnBeforePaint() = 0;
		virtual ~CHudMenuOptions() {}
		bool m_center;
	};
	void ShowMenu( const char *text, int validSlots, float duration, CHudMenuOptions *options );
#endif
	void Init( void );
	void VidInit( void );
	void Reset( void );
	virtual bool ShouldDraw( void );
	void MsgFunc_ShowMenu( bf_read &msg );
	void HideMenu( void );
	void ShowMenu( const char * menuName, int keySlot );
	void ShowMenu_KeyValueItems( KeyValues *pKV );

	bool IsMenuOpen( void );
	void SelectMenuItem( int menu_item );

private:
	virtual void OnThink();
	virtual void Paint();
	virtual void ApplySchemeSettings(vgui::IScheme *pScheme);
private:
	void		ProcessText( void );

	void PaintString( const wchar_t *text, int textlen, vgui::HFont& font, int x, int y );

#ifdef L4D2
public:
#endif
	struct ProcessedLine
	{
		int	menuitem; // -1 for just text
		int startchar;
		int length;
		int pixels;
		int height;
#ifdef L4D2
		int hotspotX, hotspotY;
		bool customColor;
		Color color;
		ProcessedLine() : menuitem(0), startchar(0), length(0), pixels(0), height(0), hotspotX(9999), hotspotY(9999), customColor(false), color(0,0,0,0) {}
#endif
	};
#ifdef L4D2
private:
	CHudMenuOptions *m_options;
#endif

	CUtlVector< ProcessedLine >	m_Processed;

	int				m_nMaxPixels;
	int				m_nHeight;

	bool			m_bMenuDisplayed;
	int				m_bitsValidSlots;
	float			m_flShutoffTime;
	int				m_fWaitingForMore;
	int				m_nSelectedItem;
	bool			m_bMenuTakesInput;

	float			m_flSelectionTime;

	CPanelAnimationVar( float, m_flOpenCloseTime, "OpenCloseTime", "1" );

	CPanelAnimationVar( float, m_flBlur, "Blur", "0" );
	CPanelAnimationVar( float, m_flTextScan, "TextScane", "1" );

	CPanelAnimationVar( float, m_flAlphaOverride, "Alpha", "255.0" );
	CPanelAnimationVar( float, m_flSelectionAlphaOverride, "SelectionAlpha", "255.0" );

	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "MenuTextFont" );
	CPanelAnimationVar( vgui::HFont, m_hItemFont, "ItemFont", "MenuItemFont" );
	CPanelAnimationVar( vgui::HFont, m_hItemFontPulsing, "ItemFontPulsing", "MenuItemFontPulsing" );

	CPanelAnimationVar( Color, m_MenuColor, "MenuColor", "MenuColor" );
	CPanelAnimationVar( Color, m_ItemColor, "MenuItemColor", "ItemColor" );
	CPanelAnimationVar( Color, m_BoxColor, "MenuBoxColor", "MenuBoxBg" );
};

#endif // HUD_MENU_H
