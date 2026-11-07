//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BINDPANEL_H
#define TERROR_BINDPANEL_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/Panel.h"
class CHudTexture;

// Retail CBindPanel, client 2D6D20. Child lifetime belongs to VGUI.
class CBindPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CBindPanel, vgui::Panel );
public:
	CBindPanel( vgui::Panel *parent, const char *name );
	void SetBind( const char *binding, int slot = -1 ); // 2D6A20.
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
	virtual void OnThink() OVERRIDE;
	virtual void Paint() OVERRIDE;
private:
	void UpdateBinding(); // 2D65C0.
	void UpdateBackground(); // 2D5C60.
	void PaintBinding(); // 2D6130.
	int GetTextWidth( const wchar_t *text, vgui::HFont font ) const;
	char m_szBind[256];
	char m_szKey[256];
	char m_szLastKey[256];
	char m_szIcon[256];
	int m_iSlot;
	int m_iSteamInputOrigin;
	float m_flNextBindingUpdate;
	float m_flWidthScale;
	bool m_bControllerButton;
	bool m_bSteamInputGlyph;
	bool m_bDrawKeyText;
	CHudTexture *m_pIcon;
	// All three native font metadata keys really are "font".
	CPanelAnimationVar( vgui::HFont, m_hKeysFont, "font", "MenuSubTitle" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFontSC, "font", "InstructorButtonsSteamController" );
	CPanelAnimationVar( vgui::HFont, m_hButtonFont, "font", "InstructorButtons" );
	CPanelAnimationVar( float, m_flScale, "scale", "1.0" );
	CPanelAnimationVarAliasType( int, m_iIconTall, "icontall", "32", "proportional_int" );
};
#endif
