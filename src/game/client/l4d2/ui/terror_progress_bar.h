//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef TERROR_PROGRESS_BAR_H
#define TERROR_PROGRESS_BAR_H
#ifdef _WIN32
#pragma once
#endif
#include "vgui_controls/Panel.h"
#include "vgui/ISurface.h"
#include <emmintrin.h>
// Sole existing native ProgressBarPanel, shared by the two real consumers.
class ProgressBarPanel : public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( ProgressBarPanel, vgui::Panel );
public:
	ProgressBarPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_flProgress( 0.0f ), m_iTexture( 0 ) {}
	void SetProgress( float progress ) { m_flProgress = progress; }
protected:
	virtual void ApplySettings( KeyValues *settings ) OVERRIDE
	{
		const char *image = settings->GetString( "image", NULL );
		if ( image )
		{
			if ( m_iTexture <= 0 ) m_iTexture = vgui::surface()->CreateNewTextureID();
			vgui::surface()->DrawSetTextureFile( m_iTexture, image, false, false );
		}
		BaseClass::ApplySettings( settings );
	}
	virtual void Paint() OVERRIDE
	{
		if ( m_iTexture == 0 ) return;
		vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
		vgui::surface()->DrawSetTexture( m_iTexture );
		vgui::surface()->DrawTexturedSubRect( 0, 0, _mm_cvttss_si32( _mm_set_ss( (float)GetWide() * m_flProgress ) ), GetTall(), 0, 0, m_flProgress, 1 );
	}
private:
	float m_flProgress;
	int m_iTexture;
};
#endif
