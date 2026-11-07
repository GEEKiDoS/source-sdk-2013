//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_DEATH_PANEL_H
#define TERROR_DEATH_PANEL_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/EditablePanel.h"
class DeathPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( DeathPanel, vgui::EditablePanel );
public:
	DeathPanel( vgui::Panel *parent, const char *name );
	virtual ~DeathPanel();
	virtual void SetVisible( bool state ) OVERRIDE;
	void SetDeathStats( KeyValues *data );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnThink() OVERRIDE;
private:
	void UpdateTickets();
	void SetStatistic( const char *control, const char *key );
	void ShowStatistics();
	KeyValues *m_data;
	float m_lastRefresh;
};
#endif
