//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_SCOREBOARD_STATS_H
#define TERROR_SCOREBOARD_STATS_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/EditablePanel.h"
class CAvatarImagePanel;
class HealthPanel;
class CIconPanel;
namespace vgui { class Label; class ImagePanel; }
class BasePlayerStatsPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( BasePlayerStatsPanel, vgui::EditablePanel );
public:
	BasePlayerStatsPanel( vgui::Panel *parent, const char *name );
	virtual int GetPlayerIndex() const = 0;
	virtual void UpdateStats( KeyValues *data ) = 0;
	virtual void SetSelected( bool selected ) = 0;
};
class InfectedStatsPanel : public BasePlayerStatsPanel
{
	DECLARE_CLASS_SIMPLE( InfectedStatsPanel, BasePlayerStatsPanel );
public:
	InfectedStatsPanel( vgui::Panel *parent, const char *name, const char *layout );
	virtual int GetPlayerIndex() const OVERRIDE { return m_playerIndex; }
	virtual void UpdateStats( KeyValues *data ) OVERRIDE;
	virtual void SetSelected( bool selected ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CAvatarImagePanel *m_avatar;
	const char *m_layout;
	int m_playerIndex;
};
class SurvivorStatsPanel : public BasePlayerStatsPanel
{
	DECLARE_CLASS_SIMPLE( SurvivorStatsPanel, BasePlayerStatsPanel );
public:
	SurvivorStatsPanel( vgui::Panel *parent, const char *name, const char *layout );
	virtual int GetPlayerIndex() const OVERRIDE { return m_playerIndex; }
	virtual void UpdateStats( KeyValues *data ) OVERRIDE;
	virtual void SetSelected( bool selected ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	CAvatarImagePanel *m_avatar;
	vgui::ImagePanel *m_head;
	vgui::Label *m_name[2], *m_status[2], *m_pingImage, *m_pingLabel, *m_record;
	HealthPanel *m_health;
	CIconPanel *m_voice, *m_recordImage, *m_slots[3];
	const char *m_layout;
	int m_playerIndex;
};
#endif
