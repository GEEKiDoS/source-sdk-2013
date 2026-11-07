//========= Copyright Valve Corporation, All rights reserved. ============//
// C293B40..C293FA0 and C299510..C299ADD. No borrowed HL2/HL2MP HUD.
#include "cbase.h"
#include "l4d2/hud/hud_terror_context_feedback.h"
#include "hud.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/entities/c_point_script_use_target.h"
#include "l4d2/ui/terror_progress_bar.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui/IImage.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

DECLARE_HUDELEMENT( CBuildableCostPanel );
static ConVar director_show_intensity( "director_show_intensity", "0", FCVAR_CHEAT | FCVAR_CLIENTDLL );
static ConVar director_intensity_threshold( "director_intensity_threshold", "0.9", FCVAR_CHEAT | FCVAR_REPLICATED | FCVAR_CLIENTDLL );

CBuildableCostPanel::CBuildableCostPanel( const char *name ) : BaseClass( g_pClientMode->GetViewport(), "BuildableCostPanel" ), CHudElement( name )
{
	m_pBar = new ProgressBarPanel( this, "Bar" );
}
CBuildableCostPanel::~CBuildableCostPanel()
{
	delete m_pBar;
}
bool CBuildableCostPanel::ShouldDraw()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	// C269A00 is the existing owner-only predicate. C269A70 additionally
	// requires the use target to be this player and is not this native call.
	if ( !player || player->IsPerformingUseAction() ) return false;
	C_PointScriptUseTarget *target = dynamic_cast< C_PointScriptUseTarget * >( player->m_hBuildableButtonUseEnt.Get() );
	return target && target->CanShowBuildPanel() && CHudElement::ShouldDraw();
}
void CBuildableCostPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/HUD/buildablecostpanel.res" );
	BaseClass::ApplySchemeSettings( scheme );
}
void CBuildableCostPanel::OnThink()
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player || !player->m_hBuildableButtonUseEnt.Get() ) return;
	C_PointScriptUseTarget *target = dynamic_cast< C_PointScriptUseTarget * >( player->m_hBuildableButtonUseEnt.Get() );
	SetControlString( "BarLabel", target ? target->GetUseString() : "" );
	SetControlString( "Subtext", target ? target->GetUseSubString() : "" );
	if ( m_pBar ) m_pBar->SetProgress( target ? target->GetPreviousProgressPercent() : 0.0f );
}
void CBuildableCostPanel::Paint()
{
	// Actual native j_nullsub_28_0.
}

static int IntensityChannel( int channel, float scale )
{
	float value = (float)channel * scale;
	if ( value <= 255.0f )
	{
		if ( value < 0.0f ) value = 0.0f;
	}
	else value = 255.0f;
	return _mm_cvttss_si32( _mm_set_ss( value ) );
}
static Color IntensityShade( Color color, float scale )
{
	// C294100 clamps the FLOAT before signed32 truncation, including unordered.
	return Color( IntensityChannel( color.r(), scale ), IntensityChannel( color.g(), scale ),
		IntensityChannel( color.b(), scale ), color.a() );
}
static void DrawIntensityBar( int x0, int y0, int x1, int y1, Color color, float fraction )
{
	int width = x1 - x0, height = y1 - y0;
	vgui::surface()->DrawSetColor( color );
	if ( width <= height ) y0 = _mm_cvttss_si32( _mm_set_ss( ( 1.0f - fraction ) * (float)height + (float)y0 ) );
	else x1 = _mm_cvttss_si32( _mm_set_ss( (float)width * fraction + (float)x0 ) );
	vgui::surface()->DrawFilledRect( x0, y0, x1, y1 );
	if ( width > 2 && height > 2 && color.a() == 255 )
	{
		vgui::surface()->DrawSetColor( IntensityShade( color, 1.25f ) );
		vgui::surface()->DrawLine( x0, y0, x1, y0 );
		vgui::surface()->DrawLine( x0, y0, x0, y1 );
		vgui::surface()->DrawSetColor( IntensityShade( color, 0.75f ) );
		vgui::surface()->DrawLine( x0, y1, x1, y1 );
		vgui::surface()->DrawLine( x1, y0, x1, y1 );
	}
}
CHudIntensityGraph::CHudIntensityGraph( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudIntensityGraph" ), m_hFont( 0 )
{
	for ( int i = 0; i < 4; ++i ) m_pPortrait[i] = NULL;
}
bool CHudIntensityGraph::ShouldDraw()
{
	return director_show_intensity.GetInt() != 0 && CHudElement::ShouldDraw();
}
void CHudIntensityGraph::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetBgColor( scheme->GetColor( "TransparentLightBlack", Color( 0, 0, 0, 0 ) ) );
	SetPaintBackgroundEnabled( true );
	SetPaintBorderEnabled( false );
	m_pPortrait[0] = vgui::scheme()->GetImage( "s_panel_gambler", true );
	m_pPortrait[2] = vgui::scheme()->GetImage( "s_panel_coach", true );
	m_pPortrait[3] = vgui::scheme()->GetImage( "s_panel_mechanic", true );
	m_pPortrait[1] = vgui::scheme()->GetImage( "s_panel_producer", true );
	m_hFont = scheme->GetFont( "default", true );
}
void CHudIntensityGraph::Paint()
{
	int width, height; GetSize( width, height );
	int x = _mm_cvttss_si32( _mm_set_ss( (float)ScreenWidth() * 0.0015625f * 4.0f ) );
	// C2997A0's x87 stage uses __ftol2_sse: spill DOUBLE, CVTTSD2SI EAX32.
	int y = _mm_cvttsd_si32( _mm_set_sd( (double)ScreenHeight() * 0.002083333333333333 * 4.0 ) );
	const wchar_t *state = L"";
	C_TerrorPlayerResource *resource = dynamic_cast< C_TerrorPlayerResource * >( g_PR );
	if ( resource )
	{
		switch ( resource->GetTempoState() )
		{
		case 0: state = L"BUILD_UP"; break;
		case 1: state = L"SUSTAIN_PEAK"; break;
		case 2: state = L"PEAK_FADE"; break;
		case 3: state = L"RELAX"; break;
		}
	}
	int textWidth, textHeight; vgui::surface()->GetTextSize( m_hFont, state, textWidth, textHeight );
	int barWidth = ( width - 5 * x ) / 4;
	int barBottom = height - 4 * y - barWidth - textHeight;
	const int spacing = barWidth + x;
	for ( int i = 0; i < 4; ++i )
	{
		C_TerrorPlayer *player = GetTerrorPlayerBySurvivorCharacter( i );
		bool alive = player && player->m_lifeState == LIFE_ALIVE;
		float intensity = player ? (float)player->m_clientIntensity * 0.0099999998f : 0.0f;
		DrawIntensityBar( x, y, x + barWidth, barBottom + y, Color( 32, 32, 32, 192 ), 1.0f );
		if ( alive )
		{
			float display = intensity * 0.75f;
			if ( intensity >= director_intensity_threshold.GetFloat() ) display = intensity;
			Color color( (unsigned char)_mm_cvttss_si32( _mm_set_ss( display * 255.0f ) ),
				(unsigned char)_mm_cvttss_si32( _mm_set_ss( ( 1.0f - display ) * 255.0f ) ), 0, 255 );
			float fill = intensity < 0.02f ? 0.02f : intensity;
			DrawIntensityBar( x, y, x + barWidth, barBottom + y, color, fill );
		}
		if ( m_pPortrait[i] )
		{
			m_pPortrait[i]->SetPos( x, barBottom + 2 * y );
			m_pPortrait[i]->SetSize( barWidth, barWidth );
			m_pPortrait[i]->Paint();
		}
		x += spacing;
	}
	vgui::surface()->DrawSetTextFont( m_hFont );
	vgui::surface()->DrawSetTextPos( ( width - textWidth ) / 2, height - textHeight - y );
	vgui::surface()->DrawSetTextColor( 255, 255, 255, 255 );
	vgui::surface()->DrawPrintText( state, V_wcslen( state ) );
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
DECLARE_HUDELEMENT( CHudIntensityGraph );
