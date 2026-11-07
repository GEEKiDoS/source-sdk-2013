//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_TERROR_CONTEXT_FEEDBACK_H
#define HUD_TERROR_CONTEXT_FEEDBACK_H
#ifdef _WIN32
#pragma once
#endif
#include "hudelement.h"
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Panel.h"
class ProgressBarPanel;
namespace vgui { class IImage; }
class CBuildableCostPanel : public vgui::EditablePanel, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CBuildableCostPanel, vgui::EditablePanel );
public:
	CBuildableCostPanel( const char *name );
	virtual ~CBuildableCostPanel();
	virtual bool ShouldDraw() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual void Paint() OVERRIDE;
private:
	ProgressBarPanel *m_pBar;
};
class CHudIntensityGraph : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudIntensityGraph, vgui::Panel );
public:
	CHudIntensityGraph( const char *name );
	virtual bool ShouldDraw() OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void Paint() OVERRIDE;
private:
	vgui::IImage *m_pPortrait[4];
	vgui::HFont m_hFont;
};
#endif
