//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_LEGACY_CONTROLS_H
#define TERROR_LEGACY_CONTROLS_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/EditablePanel.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/RichText.h"
#include "vgui_controls/Label.h"

class KeybindLabel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( KeybindLabel, vgui::Panel );
public:
	KeybindLabel( vgui::Panel *parent, const char *name );
	void SetText( const wchar_t *text, const char *binding ); // C2DCF40, called by CCSTextWindow.
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void GetSettings( KeyValues *settings ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
private:
	vgui::Label *m_pre, *m_key, *m_post;
	int m_alignment;
	CPanelAnimationVar( Color, m_textColor, "textcolor", "0 0 0 0" );
	CPanelAnimationVar( Color, m_keyColor, "keycolor", "0 0 0 0" );
};

class L4DButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( L4DButton, vgui::Button );
public:
	L4DButton( vgui::Panel *parent, const char *name, const char *text = "L4DButton", vgui::Panel *target = NULL, const char *command = NULL );
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE; // Native nullsub_642, not a background surrogate.
	virtual void Paint() OVERRIDE;
private:
	vgui::HFont m_normalFont, m_blurFont;
};

class MultiPanelButton;
class MultiButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( MultiButton, vgui::Button );
public:
	MultiButton( MultiPanelButton *parent, const char *name );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void OnCursorEntered() OVERRIDE;
	virtual void OnCursorExited() OVERRIDE;
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
	virtual void OnMouseReleased( vgui::MouseCode code ) OVERRIDE;
private:
	MultiPanelButton *m_owner;
};
class MultiPanelButton : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( MultiPanelButton, vgui::EditablePanel );
public:
	MultiPanelButton( vgui::Panel *parent, const char *name );
	void RefreshButtonState();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void PerformLayout() OVERRIDE;
private:
	MultiButton *m_button;
};

class CTerrorRichText : public vgui::RichText
{
	DECLARE_CLASS_SIMPLE( CTerrorRichText, vgui::RichText );
public:
	CTerrorRichText( vgui::Panel *parent, const char *name );
	virtual void SetText( const char *text ) OVERRIDE;
	virtual void SetText( const wchar_t *text ) OVERRIDE;
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE;
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	char m_font[64], m_color[64];
};

class RoundedButton : public vgui::Button
{
	DECLARE_CLASS_SIMPLE( RoundedButton, vgui::Button );
public:
	RoundedButton( vgui::Panel *parent, const char *name, const char *text = "RoundedButton", vgui::Panel *target = NULL, const char *command = NULL );
	virtual ~RoundedButton();
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	virtual void PaintBackground() OVERRIDE;
	virtual void PaintBorder() OVERRIDE;
private:
	void EnsureCorners();
	void DrawCorners( Color color, bool border, int wide, int tall );
	int m_texture[4][2], m_textureSize, m_cachedRadius, m_cachedBorder;
	Color m_fg[3], m_bg[3];
	CPanelAnimationVar( int, m_radius, "corner_radius", "10" );
	CPanelAnimationVar( int, m_borderWidth, "border_width", "2" );
};

class CScavengeRoundPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CScavengeRoundPanel, vgui::EditablePanel );
public:
	CScavengeRoundPanel( vgui::Panel *parent, const char *name );
	void SetScore( int teamSlot, int score );
	void SetScoreColor( int teamSlot, const char *schemeColor );
	void SetWinnerColors( int winner, int firstID, int secondID );
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
private:
	vgui::IScheme *m_roundScheme;
};
#endif
