//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef CS_TEXTWINDOW_H
#define CS_TEXTWINDOW_H
#ifdef _WIN32
#pragma once
#endif
#include "game_controls/vguitextwindow.h"
class CTerrorRichText;
class L4DButton;

// Native C232F80 and its inherited CTextWindow lifecycle, confined to L4D2.
class CCSTextWindow : public CTextWindow
{
	DECLARE_CLASS_SIMPLE( CCSTextWindow, CTextWindow );
public:
	explicit CCSTextWindow( IViewPort *viewport );
	virtual void SetData( KeyValues *data ) OVERRIDE;
	virtual void SetData( int type, const char *title, const char *message, const char *messageFallback, int command, bool unload ) OVERRIDE;
	virtual void Reset() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual bool NeedsUpdate() OVERRIDE { return true; }
	virtual void ShowPanel( bool state ) OVERRIDE;
	virtual void SetVisible( bool state ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
protected:
	// Native content refresh is a separate virtual from IViewPortPanel::Update.
	virtual void RefreshContents();
	bool IsHostfilePanel() const { return m_bHostfilePanel; }
private:
	void AdvanceFade();
	bool m_bHostfilePanel;
	int m_nFadePhase;
	float m_flFadeDeadline;
};

// The native viewport's "info" factory constructs this concrete subclass.
class CTerrorTextWindow : public CCSTextWindow
{
	DECLARE_CLASS_SIMPLE( CTerrorTextWindow, CCSTextWindow );
public:
	explicit CTerrorTextWindow( IViewPort *viewport );
	virtual void Update() OVERRIDE;
	virtual void ShowText( const char *text ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
protected:
	virtual void RefreshContents() OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
private:
	CTerrorRichText *m_pTerrorText;
	L4DButton *m_pTerrorOK;
};

// Native107A70A0: changes only on a real ShowPanel visibility transition.
bool IsTerrorTextWindowInputActive();
#endif
