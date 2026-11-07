//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_legacy_controls.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "vgui_controls/MessageMap.h"
#include "vgui_controls/ScrollBar.h"
#include "vgui_controls/TextImage.h"
#include "tier1/utlvector.h"
#include <ctype.h>
#include <math.h>
#include <malloc.h>
#include <emmintrin.h>
#include "tier0/memdbgon.h"
DECLARE_BUILD_FACTORY( KeybindLabel );
DECLARE_BUILD_FACTORY( RoundedButton );
DECLARE_BUILD_FACTORY( L4DButton );
DECLARE_BUILD_FACTORY( CTerrorRichText );
DECLARE_BUILD_FACTORY( MultiPanelButton );


static const char *s_alignment[] = { "north-west", "north", "north-east", "west", "center", "east", "south-west", "south", "south-east" };
KeybindLabel::KeybindLabel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_alignment( 4 )
{
	m_pre = new vgui::Label( this, "pre", L"" );
	m_key = new vgui::Label( this, "keybind", L"" );
	m_post = new vgui::Label( this, "post", L"" );
}
void KeybindLabel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_pre->SetFgColor( m_textColor ); m_key->SetFgColor( m_keyColor ); m_post->SetFgColor( m_textColor );
}
void KeybindLabel::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings );
	for ( int i = 0; i < ARRAYSIZE( s_alignment ); ++i )
		if ( !Q_stricmp( settings->GetString( "textAlignment", "" ), s_alignment[i] ) ) m_alignment = i;
	vgui::IScheme *scheme = vgui::scheme()->GetIScheme( GetScheme() );
	if ( scheme )
	{
		vgui::HFont font = scheme->GetFont( settings->GetString( "font", "Default" ), IsProportional() );
		m_pre->SetFont( font ); m_key->SetFont( font ); m_post->SetFont( font );
	}
}
void KeybindLabel::GetSettings( KeyValues *settings )
{
	BaseClass::GetSettings( settings );
	settings->SetString( "textAlignment", m_alignment >= 0 && m_alignment < 9 ? s_alignment[m_alignment] : "invalid" );
}
void KeybindLabel::SetText( const wchar_t *text, const char *binding )
{
	const wchar_t *resolved = text;
	if ( resolved && *resolved == L'#' )
	{
		char token[512]; g_pVGuiLocalize->ConvertUnicodeToANSI( resolved, token, sizeof( token ) );
		const wchar_t *localized = g_pVGuiLocalize->Find( token );
		if ( localized ) resolved = localized;
	}
	if ( !resolved ) resolved = L"";
	const wchar_t *marker = wcsstr( resolved, L"%s1" );
	if ( !marker )
	{
		m_pre->SetText( resolved ); m_key->SetText( L"" ); m_post->SetText( L"" );
	}
	else
	{
		CUtlVectorFixedGrowable<wchar_t,512> prefix;
		prefix.SetCount( int( marker - resolved ) + 1 );
		Q_memcpy( prefix.Base(), resolved, ( prefix.Count() - 1 ) * sizeof( wchar_t ) ); prefix.Tail() = 0;
		m_pre->SetText( prefix.Base() );
		// The only undefined native input is a missing binding after a real marker.
		if ( binding )
		{
			const char *key = engine->Key_LookupBinding( *binding == '+' ? binding + 1 : binding );
			if ( !key ) m_key->SetText( "< not bound >" );
			else if ( Q_strlen( key ) == 1 )
			{
				char upper[2] = { char( toupper( static_cast<unsigned char>( *key ) ) ), 0 };
				m_key->SetText( upper );
			}
			else m_key->SetText( key );
			m_key->SetFgColor( m_keyColor );
		}
		m_post->SetText( marker + 3 );
	}
	m_pre->SizeToContents(); m_key->SizeToContents(); m_post->SizeToContents();
	InvalidateLayout();
}
void KeybindLabel::PerformLayout()
{
	BaseClass::PerformLayout();
	int width[3], height[3];
	m_pre->GetContentSize( width[0], height[0] ); m_key->GetContentSize( width[1], height[1] ); m_post->GetContentSize( width[2], height[2] );
	const int wide = width[0] + width[1] + width[2], tall = MAX( height[0], MAX( height[1], height[2] ) );
	int x = 0, y = 0;
	if ( m_alignment == 1 || m_alignment == 4 || m_alignment == 7 ) x = ( GetWide() - wide ) / 2;
	else if ( m_alignment == 2 || m_alignment == 5 || m_alignment == 8 ) x = GetWide() - wide;
	if ( m_alignment >= 3 && m_alignment <= 5 ) y = ( GetTall() - tall ) / 2;
	else if ( m_alignment >= 6 && m_alignment <= 8 ) y = GetTall() - tall;
	m_pre->SetPos( x, y ); m_key->SetPos( x + width[0], y ); m_post->SetPos( x + width[0] + width[1], y );
}

L4DButton::L4DButton( vgui::Panel *parent, const char *name, const char *text, vgui::Panel *target, const char *command ) : BaseClass( parent, name, text, target, command ), m_normalFont( 0 ), m_blurFont( 0 )
{
	SetScheme( "ClientScheme" );
}
void L4DButton::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings );
	vgui::IScheme *scheme = vgui::scheme()->GetIScheme( GetScheme() );
	if ( scheme ) { m_normalFont = scheme->GetFont( "MainBold", true ); m_blurFont = scheme->GetFont( "MainBoldBlur", true ); SetFont( m_normalFont ); }
}
void L4DButton::ApplySchemeSettings( vgui::IScheme *scheme ) { BaseClass::ApplySchemeSettings( scheme ); SetFont( m_normalFont ); }
void L4DButton::PaintBackground() {} // C2E6A10 is an actual native nullsub.
#pragma float_control( precise, on, push )
void L4DButton::Paint()
{
	if ( m_normalFont == vgui::INVALID_FONT ) return; // Skip only undefined pre-ApplySettings font use.
	const bool armed = IsArmed();
	wchar_t text[512]; GetText( text, sizeof( text ) );
	int x, y, right, bottom; GetTextImageBounds( x, y, right, bottom );
	const double shift = double( ScreenWidth() ) * 0.0015625 * 6.0;
	x = _mm_cvttsd_si32( _mm_set_sd( double( x ) + ( x != 0 ? -shift : shift ) ) );
	vgui::surface()->DrawSetTextFont( m_normalFont ); vgui::surface()->DrawSetTextPos( x, y );
	vgui::surface()->DrawSetTextColor( armed ? Color( 255,255,255,255 ) : Color( 125,125,125,255 ) );
	vgui::surface()->DrawPrintText( text, Q_wcslen( text ) );
	if ( armed && m_blurFont != vgui::INVALID_FONT )
	{
		vgui::surface()->DrawSetTextColor( Color( 255,255,255,_mm_cvttsd_si32( _mm_set_sd( sin( Plat_FloatTime() * 4.0 ) * 30.0 + 60.0 ) ) ) );
		vgui::surface()->DrawSetTextFont( m_blurFont ); vgui::surface()->DrawSetTextPos( x, y ); vgui::surface()->DrawPrintText( text, Q_wcslen( text ) );
	}
}
#pragma float_control( pop )

CTerrorRichText::CTerrorRichText( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_font[0] = 0; m_color[0] = 0;
	SetCursor( vgui::dc_arrow );
	_vertScrollBar->UseImages( "scroll_up", "scroll_down", "scroll_line", "scroll_box" );
}
void CTerrorRichText::SetText( const char *text )
{
	char buffer[2048]; Q_strncpy( buffer, text, sizeof( buffer ) );
	for ( char *c = buffer; *c; ++c ) if ( *c == '\r' ) *c = ' ';
	BaseClass::SetText( buffer );
}
void CTerrorRichText::SetText( const wchar_t *text )
{
	wchar_t buffer[2048]; Q_wcsncpy( buffer, text, sizeof( buffer ) );
	for ( wchar_t *c = buffer; *c; ++c ) if ( *c == L'\r' ) *c = L' ';
	BaseClass::SetText( buffer );
}
void CTerrorRichText::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings );
	Q_strncpy( m_font, settings->GetString( "font", "Default" ), sizeof( m_font ) );
	Q_strncpy( m_color, settings->GetString( "fgcolor", "RichText.TextColor" ), sizeof( m_color ) );
	InvalidateLayout( false, true );
}
void CTerrorRichText::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetFont( scheme->GetFont( m_font, true ) ); SetFgColor( scheme->GetColor( m_color, Color( 255,255,255,255 ) ) );
	SetBorder( scheme->GetBorder( "NoBorder" ) ); SetBgColor( scheme->GetColor( "Blank", Color( 0,0,0,0 ) ) );
	SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false );
}

RoundedButton::RoundedButton( vgui::Panel *parent, const char *name, const char *text, vgui::Panel *target, const char *command ) : BaseClass( parent, name, text, target, command ), m_textureSize( -1 ), m_cachedRadius( -1 ), m_cachedBorder( -1 )
{
	SetScheme( "ClientScheme" );
	Q_memset( m_texture, 0, sizeof( m_texture ) );
	for ( int i = 0; i < 3; ++i ) { m_fg[i] = Color(0,0,0,0); m_bg[i] = Color(0,0,0,0); }
}
RoundedButton::~RoundedButton() {} // Native C2E6B00 has no corner/texture release.
void RoundedButton::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	const char *fg[] = { "RoundedButton.FgColor", "RoundedButton.ArmedFgColor", "RoundedButton.DepressedFgColor" };
	const char *bg[] = { "RoundedButton.BgColor", "RoundedButton.ArmedBgColor", "RoundedButton.DepressedBgColor" };
	for ( int i = 0; i < 3; ++i ) { m_fg[i] = scheme->GetColor( fg[i], Color(255,255,255,255) ); m_bg[i] = scheme->GetColor( bg[i], Color(0,0,0,255) ); }
	SetSelected( false );
}
#pragma float_control( precise, on, push )
void RoundedButton::EnsureCorners()
{
	if ( m_textureSize >= 0 && m_radius == m_cachedRadius && m_borderWidth == m_cachedBorder ) return;
	m_cachedRadius = m_radius; m_cachedBorder = m_borderWidth;
	m_textureSize = -1;
	if ( m_radius == INT_MIN ) return; // Only native signed radius-1 overflow is undefined.
	uint32 size = uint32( m_radius ) - 1u; size |= size >> 1; size |= size >> 2; size |= size >> 4; size |= size >> 8; size |= size >> 16; ++size;
	if ( size && size > uint32( INT_MAX ) / 4u / size ) return;
	m_textureSize = int( size );
	const int pixelBytes = int( size * size * 4 );
	unsigned char *fill = static_cast<unsigned char *>( _alloca( pixelBytes ) );
	unsigned char *border = static_cast<unsigned char *>( _alloca( pixelBytes ) );
	for ( int corner = 0; corner < 4; ++corner )
	{
		m_texture[corner][0] = vgui::surface()->CreateNewTextureID( true );
		m_texture[corner][1] = vgui::surface()->CreateNewTextureID( true );
		Q_memset( fill, 0, pixelBytes ); Q_memset( border, 0, pixelBytes );
		const int cx = corner >= 2 ? m_radius - 1 : 0, cy = corner == 0 || corner == 3 ? m_radius - 1 : 0;
		const int ox = corner <= 1 ? m_textureSize - m_radius : 0, oy = corner == 1 || corner == 2 ? m_textureSize - m_radius : 0;
		for ( int x = 0; x < m_radius; ++x ) for ( int y = 0; y < m_radius; ++y )
		{
			const int dx = x - cx, dy = y - cy;
			const double distance = sqrt( double( dx * dx + dy * dy ) );
			const float spilledDistance = float( distance );
			float inner = 0.0f, ring;
			if ( distance <= double( m_radius - 1 ) )
			{
				ring = float( m_radius - m_borderWidth ) <= spilledDistance ? 1.0f : clamp( 1.0f - ( float( m_radius - m_borderWidth ) - spilledDistance ), 0.0f, 1.0f );
				inner = 1.0f - ring;
			}
			else ring = clamp( 1.0f - ( spilledDistance - float( m_radius - 1 ) ), 0.0f, 1.0f );
			const int index = 4 * ( x + ox + m_textureSize * ( y + oy ) );
			fill[index] = fill[index+1] = fill[index+2] = border[index] = border[index+1] = border[index+2] = 255;
			fill[index+3] = (unsigned char)_mm_cvttss_si32( _mm_set_ss( inner * 255.0f ) ); border[index+3] = (unsigned char)_mm_cvttss_si32( _mm_set_ss( ring * 255.0f ) );
		}
		for ( int j = 0; j < 2; ++j )
		{
			g_pMatSystemSurface->DrawSetTextureRGBAEx2( m_texture[corner][j], j ? border : fill, m_textureSize, m_textureSize, IMAGE_FORMAT_RGBA8888, false );
		}
	}
}
#pragma float_control( pop )
void RoundedButton::DrawCorners( Color color, bool border, int wide, int tall )
{
	EnsureCorners();
	if ( m_textureSize < 0 ) return;
	for ( int i = 0; i < 4; ++i )
	{
		const int x = i <= 1 ? wide - m_textureSize : 0, y = i == 1 || i == 2 ? tall - m_textureSize : 0;
		vgui::surface()->DrawSetColor( color );
		vgui::surface()->DrawSetTexture( m_texture[i][border ? 1 : 0] ); vgui::surface()->DrawTexturedRect( x, y, x + m_textureSize, y + m_textureSize );
	}
}
void RoundedButton::PaintBackground()
{
	int wide, tall; GetSize( wide, tall );
	Color color;
	if ( IsArmed() && IsSelected() ) color = m_bg[2];
	else if ( IsArmed() ) color = m_bg[1];
	else color = m_bg[0];
	DrawCorners( color, false, wide, tall );
	vgui::surface()->DrawFilledRect( m_radius,m_borderWidth,wide-m_radius,tall-m_borderWidth );
	vgui::surface()->DrawFilledRect( m_borderWidth,m_radius,m_radius,tall-m_radius );
	vgui::surface()->DrawFilledRect( wide-m_radius,m_radius,wide-m_borderWidth,tall-m_radius );
}
void RoundedButton::PaintBorder()
{
	int wide, tall; GetSize( wide, tall );
	Color color;
	if ( IsArmed() && IsSelected() ) color = m_fg[2];
	else if ( IsArmed() ) color = m_fg[1];
	else color = m_fg[0];
	DrawCorners( color, true, wide, tall );
	vgui::surface()->DrawFilledRect( m_radius,0,wide-m_radius,m_borderWidth );
	vgui::surface()->DrawFilledRect( m_radius,tall-m_borderWidth,wide-m_radius,tall );
	vgui::surface()->DrawFilledRect( 0,m_radius,m_borderWidth,tall-m_radius );
	vgui::surface()->DrawFilledRect( wide-m_borderWidth,m_radius,wide,tall-m_radius );
}
CScavengeRoundPanel::CScavengeRoundPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_roundScheme( NULL ) {}
void CScavengeRoundPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/ScavengeRoundPanel.res" ); m_roundScheme = scheme; BaseClass::ApplySchemeSettings( scheme );
}
void CScavengeRoundPanel::SetScore( int teamSlot, int score )
{
	if ( teamSlot != 1 && teamSlot != 2 ) return;
	char text[32]; Q_snprintf( text, sizeof( text ), "%d", score );
	SetControlString( teamSlot == 1 ? "RoundFirstTeamScore" : "RoundSecondTeamScore", score < 0 ? "" : text );
}
void CScavengeRoundPanel::SetScoreColor( int teamSlot, const char *schemeColor )
{
	if ( teamSlot != 1 && teamSlot != 2 ) return;
	vgui::Panel *label = FindChildByName( teamSlot == 1 ? "RoundFirstTeamScore" : "RoundSecondTeamScore" );
	if ( label && m_roundScheme ) label->SetFgColor( m_roundScheme->GetColor( schemeColor, Color(255,255,255,255) ) );
}
void CScavengeRoundPanel::SetWinnerColors( int winner, int firstID, int secondID )
{
	SetScoreColor( 1, "MediumGray" ); SetScoreColor( 2, "MediumGray" );
	if ( winner == 0 || winner == firstID ) SetScoreColor( 1, "BrightGreen" );
	if ( winner == 0 || winner == secondID ) SetScoreColor( 2, "BrightGreen" );
}
MultiButton::MultiButton( MultiPanelButton *parent, const char *name ) : BaseClass( parent, name, "" ), m_owner( parent ) { SetScheme( "ClientScheme" ); }
void MultiButton::ApplySchemeSettings( vgui::IScheme *scheme ) { BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false ); }
void MultiButton::OnCursorEntered() { BaseClass::OnCursorEntered(); if ( m_owner ) m_owner->RefreshButtonState(); }
void MultiButton::OnCursorExited() { BaseClass::OnCursorExited(); if ( m_owner ) m_owner->RefreshButtonState(); }
void MultiButton::OnMousePressed( vgui::MouseCode code ) { BaseClass::OnMousePressed( code ); if ( m_owner ) m_owner->RefreshButtonState(); }
void MultiButton::OnMouseReleased( vgui::MouseCode code ) { BaseClass::OnMouseReleased( code ); if ( m_owner ) m_owner->RefreshButtonState(); }
MultiPanelButton::MultiPanelButton( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_button( NULL ) { SetScheme( "ClientScheme" ); }
void MultiPanelButton::ApplySchemeSettings( vgui::IScheme *scheme ) { BaseClass::ApplySchemeSettings( scheme ); }
void MultiPanelButton::RefreshButtonState()
{
	if ( !m_button ) return;
	m_button->RecalculateDepressedState();
	const bool depressed = m_button->IsDepressed(), armed = !depressed && m_button->IsArmed();
	SetControlVisible( "Armed", armed ); SetControlVisible( "Depressed", depressed ); SetControlVisible( "Normal", !armed && !depressed );
}
void MultiPanelButton::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings );
	const char *name = settings->GetString( "buttonName", "button" );
	if ( !m_button ) m_button = new MultiButton( this, name );
	const char *resource = settings->GetString( "panelResourceFilename", NULL );
	if ( resource && *resource ) LoadControlSettings( resource );
	else
	{
		KeyValues *panels = settings->FindKey( "panels" );
		if ( panels ) LoadControlSettings( "", NULL, panels );
		else LoadControlSettings( "resource/UI/RoundedButton.res" );
	}
	m_button->ApplySettings( settings ); m_button->SetName( name ); m_button->SetPaintBackgroundEnabled( false ); m_button->SetPaintBorderEnabled( false );
	RefreshButtonState();
}
void MultiPanelButton::PerformLayout() { BaseClass::PerformLayout(); if ( m_button ) m_button->SetBounds( 0,0,GetWide(),GetTall() ); }
