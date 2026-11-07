//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_HUD_VIEWPORT_PANELS_H
#define TERROR_HUD_VIEWPORT_PANELS_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
class CTerrorMessageBox : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CTerrorMessageBox, CTerrorViewportPanel );
public:
	CTerrorMessageBox( IViewPort *viewport, const char *name = "message_box" );
	virtual ~CTerrorMessageBox();
	virtual const char *GetName() OVERRIDE { return "message_box"; }
	virtual void SetData( KeyValues *data ) OVERRIDE;
	virtual void Update() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	KeyValues *m_data;
};
class CTerrorInfoWindow : public CTerrorMessageBox
{
	DECLARE_CLASS_SIMPLE( CTerrorInfoWindow, CTerrorMessageBox );
public:
	CTerrorInfoWindow( IViewPort *viewport, const char *name = "info_window" );
	virtual const char *GetName() OVERRIDE { return "info_window"; }
	virtual bool HasInputElements() OVERRIDE { return false; }
};
class CTerrorTeamMenu : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CTerrorTeamMenu, CTerrorViewportPanel );
public:
	explicit CTerrorTeamMenu( IViewPort *viewport );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
};
class CSurvivalModeShutdown : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CSurvivalModeShutdown, CTerrorViewportPanel );
public:
	explicit CSurvivalModeShutdown( IViewPort *viewport );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
};
class CFullscreenVersusModeResults : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CFullscreenVersusModeResults, CTerrorViewportPanel );
public:
	explicit CFullscreenVersusModeResults( IViewPort *viewport );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
};
class CVsModeShutdown : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CVsModeShutdown, CTerrorViewportPanel );
public:
	explicit CVsModeShutdown( IViewPort *viewport );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
};
IViewPortPanel *CreateTerrorHudViewportPanel( IViewPort *viewport, const char *name );
#endif
