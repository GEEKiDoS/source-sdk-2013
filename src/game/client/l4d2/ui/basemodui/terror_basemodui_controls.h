//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_CONTROLS_H
#define TERROR_BASEMODUI_CONTROLS_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Button.h"
#include "vgui_controls/Frame.h"
#include "vgui_controls/PHandle.h"
#include "tier1/utlstring.h"

namespace L4D360UI
{
// Native resource class, not an alias for the stock button. Commands continue
// through Button::FireActionSignal/AddActionSignalTarget and the existing host.
class L4D360HybridButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( L4D360HybridButton, vgui::Button );
public:
	L4D360HybridButton( vgui::Panel *parent, const char *name, const char *text = "", vgui::Panel *target = NULL, const char *command = NULL );
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void NavigateTo() OVERRIDE;
	virtual void NavigateFrom() OVERRIDE;
	virtual void FireActionSignal() OVERRIDE;
	virtual void OnCursorEntered() OVERRIDE;
	virtual void OnCursorExited() OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void OnKeyCodeReleased( vgui::KeyCode code ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual vgui::Panel *NavigateUp() OVERRIDE;
	virtual vgui::Panel *NavigateDown() OVERRIDE;
	virtual vgui::Panel *NavigateLeft() OVERRIDE;
	virtual vgui::Panel *NavigateRight() OVERRIDE;
	const char *Command() const;
	void SetOpen();
	void SetClosed();
	void SetOpen( bool open ) { if ( open ) SetOpen(); else SetClosed(); }
	bool IsOpen() const { return m_bOpen; }
	void SetDropDownIndicator( bool show );
	void SetDropDownIndicatorOpen( bool open );
	// G5B280's separate +592 flag; not ShowDropDownIndicator (+612).
	void SetDropDownEnabled( bool enabled ) { m_bDropDownEnabled = enabled; }
	void SetTooltip( const char *text, bool enabled = true );
	const char *GetTooltip() const;
	int GetOriginalTall() const { return m_nOriginalTall; }
	int GetWideAtOpen() const { return m_nWideAtOpen; }
	void SetCurrentSelection( const char *text );
	const char *GetCurrentSelection() const { return m_Selection.String(); }
	void SetUsablePlayerIndex( int index ) { m_nUsablePlayerIndex = index; }
	void SetDefaultTextColor( Color color ) { m_DefaultTextColor = color; }
	int GetStyle() const { return m_nStyle; }
	bool IsNavigationFocused();
protected:
	virtual void Paint() OVERRIDE;
	MESSAGE_FUNC( OnSiblingHybridButtonOpened, "OnSiblingHybridButtonOpened" );
private:
	enum State_t { STATE_ENABLED, STATE_DISABLED, STATE_FOCUS_ENABLED, STATE_FOCUS_DISABLED, STATE_OPEN };
	State_t GetCurrentState();
	void ApplyStyle( vgui::IScheme *scheme );
	void BroadcastSiblingOpened();
	void UpdateFooter();
	CUtlString m_Style, m_Tooltip, m_DisabledTooltip, m_Selection;
	int m_nStyle, m_nOriginalTall, m_nTextInsetX, m_nTextInsetY, m_nWideAtOpen;
	int m_nUsablePlayerIndex, m_nEnableCondition, m_nIndicatorSize;
	vgui::HFont m_hFont, m_hBlurFont, m_hHintFont, m_hSelectionFont, m_hSelectionBlurFont;
	int m_nFontTall, m_nHintTall, m_nDownTexture, m_nRightTexture;
	Color m_DefaultTextColor;
	bool m_bOpen, m_bNavigateFocus, m_bOnlyActiveUser, m_bIgnoreButtonA;
	bool m_bShowIndicator, m_bIndicatorOpen, m_bDropDownEnabled;
};

class CL4DFrame : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CL4DFrame, vgui::Frame );
public:
	CL4DFrame( vgui::Panel *parent, const char *name, bool okButton = false, bool cancelButton = true, bool markForDelete = true, bool sizeable = false );
	virtual ~CL4DFrame();
	virtual void OnOpen();
	virtual void OnClose() OVERRIDE;
	virtual vgui::Panel *NavigateBack();
	virtual void SetDataSettings( KeyValues *settings );
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void OnKeyCodeTyped( vgui::KeyCode code ) OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void NavigateTo() OVERRIDE;
	virtual void SetTitle( const char *title, bool surfaceTitle = false ) OVERRIDE;
	virtual void SetTitle( const wchar_t *title, bool surfaceTitle = false ) OVERRIDE;
	void SetNavBack( CL4DFrame *caller );
	CL4DFrame *GetNavBack() const;
	void SetFooterEnabled( bool enabled ) { m_bFooterEnabled = enabled; }
	bool IsFooterEnabled() const { return m_bFooterEnabled; }
	int DrawSmearBackground( int x, int y, int wide, int tall, bool proportionalEdge = false );
	void DrawDialogBackground( const char *title, const wchar_t *wideTitle = NULL, const char *description = NULL, const wchar_t *wideDescription = NULL, int *rectOut = NULL, bool uppercase = true );
	void SetOkButtonEnabled( bool enabled );
	void SetCancelButtonEnabled( bool enabled );
	bool LoadLayout( const char *resource = NULL, KeyValues *conditions = NULL );
	virtual void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE;
	void CenterDialogOnScreen();
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
protected:
	virtual void OnScreenSizeChanged( int oldWide, int oldTall ) OVERRIDE;
	MESSAGE_FUNC_CHARPTR( OnHybridButtonNavigatedTo, "OnHybridButtonNavigatedTo", button );
	int m_nTitleInsetX, m_nTitleInsetY, m_nContentTall;
	int m_nTopTexture, m_nBottomTexture;
	Color m_SmearColor;
	int m_iHeaderY, m_iHeaderTall, m_iTitleXOffset;
	vgui::DHANDLE<vgui::Panel> m_hLastFocus;
private:
	vgui::DHANDLE<CL4DFrame> m_hNavBack;
	vgui::Label *m_pTitle;
	CUtlString m_Resource;
	bool m_bOKEnabled, m_bCancelEnabled, m_bFooterEnabled, m_bLayoutLoaded;
};
// G0FEB0's native FlmChapterXXautogenerated .res-template expansion.
void PrepareBaseModFrameResource( KeyValues *data );
}
#endif // TERROR_BASEMODUI_CONTROLS_H
