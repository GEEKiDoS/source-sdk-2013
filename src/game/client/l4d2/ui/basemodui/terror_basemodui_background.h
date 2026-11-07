//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_BACKGROUND_H
#define TERROR_BASEMODUI_BACKGROUND_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/ImagePanel.h"
#include "tier1/utlstring.h"

// Native RTTI G1F97A8 is global, unlike CL4DBasePanel and FlyoutMenu.
class L4DMenuBackground : public vgui::ImagePanel
{
	DECLARE_CLASS_SIMPLE( L4DMenuBackground, vgui::ImagePanel );
public:
	L4DMenuBackground( vgui::Panel *parent, const char *panelName );
	virtual void OnMousePressed( vgui::MouseCode code ) OVERRIDE;
};

namespace L4D360UI
{
// Extracted storage for CL4DBasePanel's background, not another VGUI panel or
// deletion owner. The supported x64 runtime takes the native unavailable arm.
class CBasePanelBackground
{
public:
	CBasePanelBackground();
	~CBasePanelBackground();
	void ApplySchemeSettings( vgui::IScheme *scheme, int wide, int tall );
	void PaintBackground( int wide, int tall );
	void OnGameUIHidden();
private:
	void ReleaseTexture();
	void SetBackgroundSize( int wide, int tall );
	void ResolveBackgroundMovie();
	int m_nBackgroundTexture;
	bool m_bWidescreen;
	bool m_bMovieUnavailable;
	CUtlString m_SchemeMovie;
};
}
#endif // TERROR_BASEMODUI_BACKGROUND_H
