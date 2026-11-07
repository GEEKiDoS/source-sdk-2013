//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_FOOTER_H
#define TERROR_BASEMODUI_FOOTER_H
#ifdef _WIN32
#pragma once
#endif

#include "l4d2/ui/basemodui/terror_basemodui_controls.h"

namespace vgui { class Label; class ContinuousProgressBar; }
namespace L4D360UI
{
enum FooterButtons_t
{
	FB_NONE = 0,
	FB_BUTTON_A = 1,
	FB_BUTTON_B = 2,
	FB_BUTTON_X = 4,
	FB_BUTTON_Y = 8,
	FB_DPAD = 16
};

// G1F8918: five (anchor, x, y) triples followed by the allowed button mask.
// Anchors: 0=left, 1=right, 2=left of center, 3=right of center.
struct FooterButtonsLayout_t
{
	int position[5][3];
	int allowedButtons;
};

class CL4DFooterPanel : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( CL4DFooterPanel, CL4DFrame );
public:
	CL4DFooterPanel( vgui::Panel *parent, const char *name = "FooterPanel" );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
	void SetButtons( int buttons, int layout = 2, bool help = true, const FooterButtonsLayout_t *custom = NULL );
	void SetButtonText( int button, const char *text );
	void SetButtonText( int button, const wchar_t *text );
	int GetButtons() const { return m_nButtons; }
	int GetButtonLayout() const { return m_nLayout; }
	bool IsHelpTextEnabled();
	void SetHelpText( const char *text );
	void FadeOutHelpText();
	void SetCloudEnabled( bool enabled );
	bool HasContent();
protected:
	virtual void PaintBackground() OVERRIDE;
private:
	void PaintHelpText();
	void PaintButton( int index, const char *icon );
	int ButtonIndex( int button ) const;
	FooterButtonsLayout_t m_CustomLayouts[3];
	wchar_t m_ButtonText[5][64];
	char m_HelpText[512];
	wchar_t m_HelpTextUnicode[512];
	int m_nButtons, m_nLayout;
	vgui::HFont m_hHelpFont, m_hButtonFont, m_hButtonTextFont;
	bool m_bFontsInitialized;
	float m_flHelpFadeStart;
};

// The absent VWorkshop001/UGC owner takes G42C90's no-current-download branch.
// This panel owns only the native strip's controls, never download state/results.
class CL4DWorkshopDownPanel : public CL4DFrame
{
	DECLARE_CLASS_SIMPLE( CL4DWorkshopDownPanel, CL4DFrame );
public:
	CL4DWorkshopDownPanel( vgui::Panel *parent, const char *name = "WorkshopDownloadPanel" );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE;
protected:
	virtual void OnThink() OVERRIDE;
private:
	vgui::ContinuousProgressBar *m_pDownloadProgress;
	vgui::Label *m_pDownloadLabel, *m_pDownloadFileLabel;
};

// Finds the footer owned by the actual ancestor root, not a second global owner.
CL4DFooterPanel *GetL4DFooterPanel( vgui::Panel *descendant );
}
#endif // TERROR_BASEMODUI_FOOTER_H
