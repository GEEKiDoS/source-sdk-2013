//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_EMBEDDED_SCOREBOARDS_H
#define TERROR_EMBEDDED_SCOREBOARDS_H
#ifdef _WIN32
#pragma once
#endif
#include <vgui_controls/EditablePanel.h>
class CScavengeRoundPanel;
class CVersusModeLevelProgressBar;
class CScavengeModeEmbeddedScoreboard : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CScavengeModeEmbeddedScoreboard, vgui::EditablePanel );
public:
	CScavengeModeEmbeddedScoreboard( vgui::Panel *parent, const char *name );
	void UpdateScores();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
private:
	CScavengeRoundPanel *m_rounds[5], *m_final;
};
class CVersusModeEmbeddedScoreboard : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CVersusModeEmbeddedScoreboard, vgui::EditablePanel );
public:
	CVersusModeEmbeddedScoreboard( vgui::Panel *parent, const char *name );
	void SetPlayerSlot( int slot ); // C2EC0D0, actual player-slot propagation.
	void UpdateScores();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnTick() OVERRIDE;
private:
	int m_playerSlot; CVersusModeLevelProgressBar *m_progress;
};
#endif
