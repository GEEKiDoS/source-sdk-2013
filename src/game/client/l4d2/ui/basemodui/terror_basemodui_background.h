//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_BASEMODUI_BACKGROUND_H
#define TERROR_BASEMODUI_BACKGROUND_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/ImagePanel.h"
#include "tier1/utlstring.h"
#include "l4d2/ui/basemodui/terror_bink_movie.h"

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
// deletion owner. Plays the menu's Bink movie through binkdec.dll (G0CC20's movie
// arm) and falls back to the static image while it is unavailable.
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
	bool OpenMovie();
	bool PaintMovie( int wide, int tall );
	void ComputeMovieCrop( int wide, int tall );
	int m_nBackgroundTexture;
	bool m_bWidescreen;
	bool m_bMovieUnavailable;
	CUtlString m_SchemeMovie;
	CTerrorBinkMovie m_Movie;
	// Texture-space rectangle of the movie shown in the panel (G9C10), for m_nCropWide x m_nCropTall.
	float m_flMovieU0, m_flMovieV0, m_flMovieU1, m_flMovieV1;
	int m_nCropWide, m_nCropTall;
	int m_nLogoTexture, m_nLogoWide, m_nLogoTall;
};
}
#endif // TERROR_BASEMODUI_BACKGROUND_H
