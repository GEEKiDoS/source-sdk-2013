//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_TAKEOVER_BAR_H
#define TERROR_TAKEOVER_BAR_H
#ifdef _WIN32
#pragma once
#endif
#include "l4d2/ui/terror_client_panels.h"
#include "util_shared.h"
class CBindPanel;
class C_TerrorPlayer;
namespace vgui { class Label; }
class CTakeoverBar : public CTerrorViewportPanel
{
	DECLARE_CLASS_SIMPLE( CTakeoverBar, CTerrorViewportPanel );
public:
	explicit CTakeoverBar( IViewPort *viewport );
	virtual bool HasInputElements() OVERRIDE { return false; }
	virtual bool NeedsUpdate() OVERRIDE;
	virtual void Update() OVERRIDE;
	virtual void SetData( KeyValues *data ) OVERRIDE;
	virtual void ShowPanel( bool state ) OVERRIDE;
	virtual bool IsVisible() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
private:
	CountdownTimer m_minimumShowTimer;
	int m_character;
	C_TerrorPlayer *m_lastTarget; // Native borrowed pointer, not a second actor owner.
	CBindPanel *m_fireBind;
	vgui::Label *m_fireMessage;
	CPanelAnimationVarAliasType( int, m_iPadding, "padding", "4", "proportional_int" );
};
#endif
