//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_TERROR_LOCALPLAYER_H
#define HUD_TERROR_LOCALPLAYER_H
#ifdef _WIN32
#pragma once
#endif
#include "hudelement.h"
#include "tier1/utlvector.h"
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/PHandle.h>
class TeamDisplayPlayer;
class CHudLocalPlayerDisplay : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudLocalPlayerDisplay, vgui::EditablePanel );
public:
	CHudLocalPlayerDisplay( const char *name );
	virtual bool ShouldDraw() OVERRIDE;
	virtual void LevelInit() OVERRIDE;
	virtual void LevelShutdown() OVERRIDE;
	virtual void Reset() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	MESSAGE_FUNC_PTR( OnPlayerPanelDeleted, "PlayerPanelDeleted", pDeletedPanel );
private:
	vgui::DHANDLE< TeamDisplayPlayer > m_pLocalPlayer;
};
class CHudTeamDisplay : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudTeamDisplay, vgui::EditablePanel );
public:
	CHudTeamDisplay( const char *name );
	virtual ~CHudTeamDisplay();
	virtual bool ShouldDraw() OVERRIDE;
	virtual void LevelInit() OVERRIDE;
	virtual void LevelShutdown() OVERRIDE;
	virtual void Reset() OVERRIDE;
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE;
	bool HasPlayer( int index ) const; // C2C7D30, actual idle-human association.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
	MESSAGE_FUNC_PTR( OnPlayerPanelDeleted, "PlayerPanelDeleted", pDeletedPanel );
private:
	void ClearPlayers();
	void AddPlayer( int index );
	void RefreshLayout();
	vgui::DHANDLE< TeamDisplayPlayer > m_pPlayers[4];
	CUtlVectorFixed< vgui::DHANDLE< TeamDisplayPlayer >, 4 > m_order;
	vgui::HPanel m_hScoreboard;
};
vgui::Panel *CreateTerrorTeammateVoicePanel( vgui::Panel *parent );
#endif
