//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_VERSUS_PROGRESS_H
#define TERROR_VERSUS_PROGRESS_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/EditablePanel.h"
class CHudTexture;
class CVersusModeLevelProgressBar : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CVersusModeLevelProgressBar, vgui::EditablePanel );
public:
	CVersusModeLevelProgressBar( vgui::Panel *parent, const char *name );
	void SetPlayerSlot( int slot ) { m_playerSlot = slot; }
	void SetProgressData( const int records[4][2], bool highlightLocal );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
private:
	int m_playerSlot;
	CHudTexture *m_skull;
	int m_progress[4][2];
	bool m_external, m_highlight;
	CPanelAnimationVarAliasType( int, m_barX, "bar_x", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_barY, "bar_y", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_barW, "bar_w", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_barH, "bar_h", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_gap, "bar_gap", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_skullSize, "skull_size", "0", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_skullY, "skull_y", "0", "proportional_int" );
	CPanelAnimationVar( Color, m_barColor, "bar_color", "White" );
	CPanelAnimationVar( Color, m_localColor, "bar_localplayer_color", "White" );
	CPanelAnimationVar( Color, m_backgroundColor, "bar_bgcolor", "White" );
};
#endif
