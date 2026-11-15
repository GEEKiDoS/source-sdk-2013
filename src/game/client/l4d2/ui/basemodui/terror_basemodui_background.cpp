//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_background.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "filesystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
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
CBasePanelBackground::CBasePanelBackground() : m_nBackgroundTexture( -1 ), m_bWidescreen( false ), m_bMovieUnavailable( false ),
	m_flMovieU0( 0.0f ), m_flMovieV0( 0.0f ), m_flMovieU1( 1.0f ), m_flMovieV1( 1.0f ), m_nCropWide( 0 ), m_nCropTall( 0 ),
	m_nLogoTexture( -1 ), m_nLogoWide( 0 ), m_nLogoTall( 0 )
{
}

CBasePanelBackground::~CBasePanelBackground()
{
	// GDC80 releases the root's textures before its VGUI base is destroyed.
	OnGameUIHidden();
	ReleaseTexture();
	if ( m_nLogoTexture != -1 )
	{
		vgui::surface()->DestroyTextureID( m_nLogoTexture );
		m_nLogoTexture = -1;
	}
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
	// G0C3A0 reloads the actual GAME material on scheme/resolution changes, loads the
	// movie logo sized by the screen aspect and recomputes the movie crop (G9C10).
	ReleaseTexture();
	m_SchemeMovie = scheme->GetResourceString( "BackgroundMovie" );
	SetBackgroundSize( wide, tall );
	if ( m_nLogoTexture == -1 )
	{
		m_nLogoTexture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( m_nLogoTexture, "vgui/Left4Dead2Logo", true, false );
	}
	int screenWide = 0, screenTall = 0;
	vgui::surface()->GetScreenSize( screenWide, screenTall );
	const bool widescreen = screenTall > 0 && float( screenWide ) / float( screenTall ) >= 1.5999f;
	m_nLogoWide = vgui::scheme()->GetProportionalScaledValue( widescreen ? 384 : 320 );
	m_nLogoTall = vgui::scheme()->GetProportionalScaledValue( widescreen ? 192 : 160 );
	m_nCropWide = m_nCropTall = 0;
}

bool CBasePanelBackground::OpenMovie()
{
	if ( m_Movie.IsOpen() )
		return true;
	// G9DD0: -nomenuvid only skips the movie; a failed load latches until G0AB00.
	if ( m_bMovieUnavailable || CommandLine()->FindParm( "-nomenuvid" ) )
		return false;
	// Native engine E5D9B0 -> E79A90 is positively a MOVIE getter. Its public
	// counterpart does not exist on target, so reproduce that game-owned naming
	// producer, then preserve G9DD0's GAME existence test and scheme fallback.
	char movie[MAX_PATH];
	V_snprintf( movie, sizeof( movie ), "media/l4d2_background%02d.bik", GetBackgroundNumber() );
	const char *movieFile = filesystem->FileExists( movie, "GAME" ) ? movie : m_SchemeMovie.String();
	if ( !m_Movie.Open( movieFile, "GAME" ) )
	{
		DevWarning( "Unable to load background movie '%s'\n", movieFile );
		m_bMovieUnavailable = true;
		return false;
	}
	m_nCropWide = m_nCropTall = 0;
	return true;
}

void CBasePanelBackground::ComputeMovieCrop( int wide, int tall )
{
	// G9C10: show the centred part of the movie that fills the panel at the movie's aspect.
	m_nCropWide = wide;
	m_nCropTall = tall;
	m_flMovieU0 = m_flMovieV0 = 0.0f;
	m_Movie.GetTexCoordRange( m_flMovieU1, m_flMovieV1 );
	const float panelAspect = float( wide ) / float( tall );
	const float movieAspect = float( m_Movie.GetWidth() ) / float( m_Movie.GetHeight() );
	if ( movieAspect > panelAspect )
	{
		const float shownWide = float( tall ) * movieAspect;
		const float span = float( wide ) * ( m_flMovieU1 - m_flMovieU0 ) / shownWide;
		m_flMovieU0 = ( m_flMovieU1 - span ) * 0.5f;
		m_flMovieU1 = m_flMovieU0 + span;
	}
	else if ( panelAspect > movieAspect )
	{
		const float shownTall = float( wide ) * float( m_Movie.GetHeight() ) / float( m_Movie.GetWidth() );
		const float span = float( tall ) * ( m_flMovieV1 - m_flMovieV0 ) / shownTall;
		m_flMovieV0 = ( m_flMovieV1 - span ) * 0.5f;
		m_flMovieV1 = m_flMovieV0 + span;
	}
}

bool CBasePanelBackground::PaintMovie( int wide, int tall )
{
	if ( !OpenMovie() )
		return false;
	m_Movie.Update();
	// Until the first frame is decoded the static background stands in.
	if ( !m_Movie.HasFrame() )
		return false;
	if ( wide != m_nCropWide || tall != m_nCropTall )
		ComputeMovieCrop( wide, tall );

	// G0C600: one white quad over the whole root in clip space, UVs inset by a texel.
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->Bind( m_Movie.GetMaterial(), NULL );
	int vx, vy, vw, vh;
	pRenderContext->GetViewport( vx, vy, vw, vh );
	const float left = -1.0f, top = 1.0f;
	const float right = float( wide - 1 ) * 2.0f / float( vw ) - 1.0f;
	const float bottom = 1.0f - float( tall - 1 ) * 2.0f / float( vh );
	const float rightU = m_flMovieU1 - 1.0f / float( wide ), bottomV = m_flMovieV1 - 1.0f / float( tall );
	IMesh *pMesh = pRenderContext->GetDynamicMesh( true );
	CMeshBuilder meshBuilder;
	meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );
	for ( int corner = 0; corner < 4; ++corner )
	{
		const bool bLeft = corner == 0 || corner == 3;
		const bool bBottom = ( corner & 2 ) != 0;
		meshBuilder.Position3f( bLeft ? left : right, bBottom ? bottom : top, 0.0f );
		meshBuilder.Normal3f( 0.0f, 0.0f, 1.0f );
		meshBuilder.TexCoord2f( 0, bLeft ? m_flMovieU0 : rightU, bBottom ? bottomV : m_flMovieV0 );
		meshBuilder.TangentS3f( 0.0f, 1.0f, 0.0f );
		meshBuilder.TangentT3f( 1.0f, 0.0f, 0.0f );
		meshBuilder.Color4ub( 255, 255, 255, 255 );
		meshBuilder.AdvanceVertex();
	}
	meshBuilder.End();
	pMesh->Draw();
	pRenderContext->MatrixMode( MATERIAL_VIEW );
	pRenderContext->PopMatrix();
	pRenderContext->MatrixMode( MATERIAL_PROJECTION );
	pRenderContext->PopMatrix();
	return true;
}

void CBasePanelBackground::OnGameUIHidden()
{
	// G0AB00 destroys the movie and resets the failed-load latch on hide/level load.
	m_Movie.Close();
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
	if ( PaintMovie( wide, tall ) )
	{
		// G0CC20's movie arm adds the logo near the top right at alpha 10; its alpha 40
		// arm needs the console AttractScreen window (type 28), which PC never opens.
		if ( m_nLogoTexture != -1 )
		{
			const int x = int( float( wide ) * 0.95f - float( m_nLogoWide ) );
			const int y = int( float( tall ) * 0.05f );
			vgui::surface()->DrawSetColor( 255, 255, 255, 10 );
			vgui::surface()->DrawSetTexture( m_nLogoTexture );
			vgui::surface()->DrawTexturedRect( x, y, x + m_nLogoWide, y + m_nLogoTall );
		}
		return;
	}
	// G0CCDA..CD26: both -nomenuvid and movie failure draw the same static
	// texture, white/opaque, stretched to the full root.
	vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
	vgui::surface()->DrawSetTexture( m_nBackgroundTexture );
	vgui::surface()->DrawTexturedRect( 0, 0, wide, tall );
}
}
