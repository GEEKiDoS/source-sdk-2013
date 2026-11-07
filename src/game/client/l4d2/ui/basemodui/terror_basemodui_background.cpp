//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_background.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "filesystem.h"
#include "tier0/icommandline.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "tier0/memdbgon.h"

namespace
{
// Native engine E799C0 caches this for the process, shared by its image/movie
// getters. Target tier0 has no Plat_GetClockStart; Main approved Plat_MSTime.
int GetBackgroundNumber()
{
	static int background = 0;
	if ( !background )
	{
		const int requested = CommandLine()->ParmValue( "-background", 0 );
		background = requested > 0 ? clamp( requested, 1, 5 ) : 1 + Plat_MSTime() % 5;
	}
	return background;
}
}

L4DMenuBackground::L4DMenuBackground( vgui::Panel *parent, const char *panelName ) : BaseClass( parent, panelName )
{
}

void L4DMenuBackground::OnMousePressed( vgui::MouseCode code )
{
	// G6A140: every mouse press dismisses the active flyout and returns to its
	// parent button. ImagePanel owns the resource's fill/image/scale behavior.
	L4D360UI::FlyoutMenu *active = L4D360UI::FlyoutMenu::GetActiveMenu();
	if ( active )
		L4D360UI::FlyoutMenu::CloseActiveMenu( active->GetParentButton() );
	BaseClass::OnMousePressed( code );
}

namespace L4D360UI
{
CBasePanelBackground::CBasePanelBackground() : m_nBackgroundTexture( -1 ), m_bWidescreen( false ), m_bMovieUnavailable( false )
{
}

CBasePanelBackground::~CBasePanelBackground()
{
	// GDC80 releases the root's textures before its VGUI base is destroyed.
	OnGameUIHidden();
	ReleaseTexture();
}

void CBasePanelBackground::ReleaseTexture()
{
	if ( m_nBackgroundTexture != -1 )
	{
		vgui::surface()->DestroyTextureID( m_nBackgroundTexture );
		m_nBackgroundTexture = -1;
	}
}

void CBasePanelBackground::SetBackgroundSize( int wide, int tall )
{
	if ( wide <= 0 || tall <= 0 )
		return;
	// Native engine E79A30, called by G0C3A0: do not confuse the texture name
	// with target GetMainMenuBackgroundName, whose contract is a LEVEL name.
	const bool widescreen = float( wide ) / float( tall ) >= 1.5999f;
	if ( m_nBackgroundTexture != -1 && widescreen == m_bWidescreen )
		return;
	ReleaseTexture();
	m_bWidescreen = widescreen;
	char image[MAX_PATH];
	V_snprintf( image, sizeof( image ), "console/background%02d%s", GetBackgroundNumber(), widescreen ? "_widescreen" : "" );
	m_nBackgroundTexture = vgui::surface()->CreateNewTextureID();
	vgui::surface()->DrawSetTextureFile( m_nBackgroundTexture, image, true, false );
}

void CBasePanelBackground::ApplySchemeSettings( vgui::IScheme *scheme, int wide, int tall )
{
	// G0C3A0 reloads the actual GAME material on scheme/resolution changes.
	ReleaseTexture();
	m_SchemeMovie = scheme->GetResourceString( "BackgroundMovie" );
	SetBackgroundSize( wide, tall );
}

void CBasePanelBackground::ResolveBackgroundMovie()
{
	if ( m_bMovieUnavailable || CommandLine()->FindParm( "-nomenuvid" ) )
		return;
	// Native engine E5D9B0 -> E79A90 is positively a MOVIE getter. Its public
	// counterpart does not exist on target, so reproduce that game-owned naming
	// producer, then preserve G9DD0's GAME existence test and scheme fallback.
	char movie[MAX_PATH];
	V_snprintf( movie, sizeof( movie ), "media/l4d2_background%02d.bik", GetBackgroundNumber() );
	const char *movieFile = filesystem->FileExists( movie, "GAME" ) ? movie : m_SchemeMovie.String();
	DevWarning( "Unable to load background movie '%s': target x64 has no Bink backend.\n", movieFile );
	// Main-approved target capability: x64 video_services has no installed Bink
	// subsystem, while source2wrapper advertises only .webm. Do not load x86
	// IBik, reinterpret flags3 as IVideoServices flags, try an alternate movie,
	// or claim a successful CreateVideoMaterial. G9DD0's failure latch and
	// G0CCDA's static-background arm are the supported native behavior here.
	m_bMovieUnavailable = true;
}

void CBasePanelBackground::OnGameUIHidden()
{
	// G0AB00 resets the failed-load latch on hide/level load. No target movie,
	// decoder, audio notification or material exists to destroy in this arm.
	m_bMovieUnavailable = false;
}

void CBasePanelBackground::PaintBackground( int wide, int tall )
{
	// G0CC20 skips both actual and background levels (G86B10/G86B50), and the
	// loading screen owns its own paint. Leave target GameUI's loading curtain
	// engine-owned rather than fabricating its private host-state predicate.
	const char *level = engine->GetLevelName();
	if ( ( level && *level ) || engine->IsDrawingLoadingImage() )
	{
		if ( m_bMovieUnavailable )
			OnGameUIHidden();
		return;
	}
	SetBackgroundSize( wide, tall );
	if ( m_nBackgroundTexture == -1 )
		return;
	ResolveBackgroundMovie();
	// G0CCDA..CD26: both -nomenuvid and movie failure draw the same static
	// texture, white/opaque, stretched to the full root. No movie-only logo,
	// 0.5-second startup timing, UV crop or movie transition is applied here.
	vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
	vgui::surface()->DrawSetTexture( m_nBackgroundTexture );
	vgui::surface()->DrawTexturedRect( 0, 0, wide, tall );
}
}
