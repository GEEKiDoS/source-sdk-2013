//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_HEALTH_PANEL_H
#define TERROR_HEALTH_PANEL_H
#ifdef _WIN32
#pragma once
#endif
#include <vgui_controls/EditablePanel.h>
Color TerrorHealthPalette( int level );
class HealthPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( HealthPanel, vgui::EditablePanel );
public:
	HealthPanel( vgui::Panel *parent, const char *name );
	void SetHealth( int health, int temporary, int maximum, bool dying, bool incapacitated, bool local );
	Color GetHealthColor() const;
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void Paint() OVERRIDE;
private:
	int GetHealthLevel( int health ) const;
	void DrawRange( int texture, float start, float end );
	void DrawTexture( int texture, int x, int y, int wide, int tall, Color color, float u );
	void DrawTicks( int x, int y, int wide, int tall, int health, int temporary );
	bool m_bMaterialStyle, m_bLocal, m_bDying, m_bIncapacitated;
	int m_iHealth, m_iTemporary, m_iMaximum;
	float m_flLastChange;
	int m_iHealthTexture[5], m_iTicksTexture[5];
	int m_iWhite, m_iGrey, m_iOutline;
	CPanelAnimationVar( Color, m_monochromeColor, "monochrome_color", "0 0 0 0" );
	CPanelAnimationVarAliasType( int, m_inset, "inset", "2", "proportional_int" );
};
#endif
