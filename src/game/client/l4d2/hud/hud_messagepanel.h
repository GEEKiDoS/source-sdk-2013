//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef HUD_MESSAGEPANEL_H
#define HUD_MESSAGEPANEL_H
#ifdef _WIN32
#pragma once
#endif
#include "hudelement.h"
#include "vgui_controls/Label.h"
#include "tier1/utlvector.h"
class bf_read;
class HudMessageLabel;
class HudMessageCloseLabel;

// Native2B3E70: vgui::Label first, real CHudElement second. The genuine
// CHudElement allocator already zero-fills just as native factory2B45E0.
class CHudMessagePanel : public vgui::Label, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudMessagePanel, vgui::Label );
public:
	explicit CHudMessagePanel( const char *hudName );
	virtual void Init() OVERRIDE;
	virtual bool ShouldDraw() OVERRIDE;
	void MsgFunc_MessageText( bf_read &message );
	void AppendMessage( Color color, const char *text ); // 2B4030.
	void ClearMessages(); // 2B3BF0; does not itself change the visible flag.
	void RefreshCloseHint(); // Child2B3250, not a second ShowPanel operation.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
	virtual void OnTick() OVERRIDE {} // Genuine native nullsub630, 2B34D0.
private:
	HudMessageCloseLabel *m_pCloseLabel;
	CUtlVector< HudMessageLabel * > m_Lines;
	CPanelAnimationVarAliasType( int, m_offsetX, "text_xpos", "4", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_offsetY, "text_ypos", "4", "proportional_int" );
	CPanelAnimationVarAliasType( int, m_spacing, "text_spacing", "1", "proportional_int" );
};
#endif
