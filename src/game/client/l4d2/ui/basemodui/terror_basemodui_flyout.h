//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_FLYOUT_H
#define TERROR_BASEMODUI_FLYOUT_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Button.h"
#include "tier1/utlstring.h"
#include "vgui_controls/PHandle.h"
#include "tier1/utlvector.h"

namespace L4D360UI
{
class FlyoutMenu;
class L4D360HybridButton;
class FlyoutMenuListener
{
public:
	virtual void OnNotifyChildFocus( vgui::Panel *child ) = 0;
	virtual void OnFlyoutMenuClosed( vgui::Panel *returnTo ) = 0;
	virtual void OnFlyoutMenuCancelled() = 0;
protected:
	virtual ~FlyoutMenuListener() {}
};

class FlyoutMenu : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( FlyoutMenu, vgui::EditablePanel );
public:
	virtual void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE;
	FlyoutMenu( vgui::Panel *parent, const char *name );
	virtual ~FlyoutMenu();
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void NavigateToChild( vgui::Panel *child ) OVERRIDE;
	void OpenMenu( vgui::Panel *parentButton, vgui::Panel *initialFocus = NULL, bool reloadResource = false, vgui::Panel *positionFrom = NULL );
	void SetBackgroundHeight( int tall );
	void CloseMenu( vgui::Panel *returnTo = NULL );
	static FlyoutMenu *GetActiveMenu();
	static void CloseActiveMenu( vgui::Panel *returnTo = NULL );
	void SetFlyoutMenuListener( FlyoutMenuListener *listener ) { m_pListener = listener; }
	vgui::Panel *GetParentButton() const { return m_ParentButton.Get(); }
	vgui::Button *FindChildButtonByCommand( const char *command );
	vgui::Button *FindNextChildButtonByCommand( const char *command );
	vgui::Button *FindPrevChildButtonByCommand( const char *command );
	void SetInitialFocus( const char *name );
	void SetMenuOffset( int x, int y ) { m_OffsetX = x; m_OffsetY = y; }
	void SetFlyoutInitialFocus( vgui::Panel *child );
	void SetOriginalTall( int tall ) { m_OriginalTall = tall; }
	int GetOriginalTall() const { return m_OriginalTall; }
	bool IsBlindNavigationDisabled() const { return m_bNoBlindNavigation; }
	bool LoadMenu( const char *resource, KeyValues *conditions = NULL );
	// Reuses the native resource's first row as the complete style/geometry
	// template. Producer supplies actual rows, in actual producer order.
	void BeginDynamicRows();
	L4D360HybridButton *AddDynamicRow( const char *name, const char *text, const char *command, bool enabled = true );
	void EndDynamicRows();
private:
	bool LoadMenuData( const char *resource, KeyValues *preloaded, KeyValues *conditions );
	void ApplyAutoNavigation( bool disabled );
	void RandomizeItems();
	void ResizeFromBackground();
	vgui::Panel *FindInitialFocus();
	CUtlString m_Resource, m_InitialFocus;
	vgui::DHANDLE<vgui::Panel> m_ParentButton, m_FocusedChild;
	FlyoutMenuListener *m_pListener;
	int m_OffsetX, m_OffsetY, m_OriginalTall;
	bool m_bOnlyActiveUser, m_bExpandUp, m_bWideAtOpen;
	bool m_bStandalonePositioning, m_bDirectCommandTarget, m_bNoBlindNavigation, m_bOpaque;
	bool m_bInitialFocusOverride;
	KeyValues *m_pRowTemplate;
	CUtlVector<L4D360HybridButton *> m_DynamicRows;
};

class DropDownMenu : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( DropDownMenu, vgui::EditablePanel );
public:
	typedef void ( *FlyoutOpenCallback )( DropDownMenu *, FlyoutMenu * );
	DropDownMenu( vgui::Panel *parent, const char *name );
	virtual void ApplySettings( KeyValues *data ) OVERRIDE;
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE;
	virtual void OnMouseWheeled( int delta ) OVERRIDE;
	virtual void SetEnabled( bool enabled ) OVERRIDE;
	virtual void NavigateTo() OVERRIDE;
	virtual void NavigateFrom() OVERRIDE;
	virtual void NavigateToChild( vgui::Panel *child ) OVERRIDE;
	void SetCurrentSelection( const char *command );
	const char *GetCurrentSelection() const;
	FlyoutMenu *GetFlyout();
	L4D360HybridButton *GetButton();
	void SetFlyout( const char *name );
	void SetShowSelectedText( bool show );
	void SetFlyoutOpenCallback( FlyoutOpenCallback callback ) { m_pOpenCallback = callback; }
	void CloseDropDown();
	void ChangeSelection( bool forward );
private:
	vgui::DHANDLE<L4D360HybridButton> m_Button;
	vgui::DHANDLE<FlyoutMenu> m_Flyout;
	char m_SelectedCommand[260];
	bool m_bShowSelectedText, m_bNoBlindNavigation;
	FlyoutOpenCallback m_pOpenCallback;
};

vgui::Panel *CreateBaseModFlyoutControl( vgui::Panel *parent, const char *className );
// G53DC0 uses the real mission/mode chapter count and native 2..15 variants.
FlyoutMenu *ConfigureChapterDropDown( DropDownMenu *dropDown, KeyValues *settings );
} // namespace L4D360UI
#endif // TERROR_BASEMODUI_FLYOUT_H
