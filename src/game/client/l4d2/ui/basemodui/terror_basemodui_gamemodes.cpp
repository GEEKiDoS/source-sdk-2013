//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_gamemodes.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/gamerules/terror_mode.h"
#include "VGuiMatSurface/IMatSystemSurface.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imesh.h"
#include "vgui/IInput.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "tier1/fmtstr.h"
#include "tier0/platform.h"
#include "tier0/memdbgon.h"

using L4D360UI::L4D360HybridButton;

// The native VGUI Surface031 DrawTexturedRectEx is V0F3B0, not a different
// image widget. Its exact four-vertex raster is available through the target's
// real material surface binding and the ordinary SDK dynamic-mesh API.
class CGameModesPaint
{
public:
	explicit CGameModesPaint( vgui::Panel *panel ) : m_Context( materials )
	{
		int viewportX, viewportY, viewportWide, viewportTall;
		m_Context->GetViewport( viewportX, viewportY, viewportWide, viewportTall );
		m_nX = 0; m_nY = 0; panel->LocalToScreen( m_nX, m_nY );
		int left, top, right, bottom; panel->GetInset( left, top, right, bottom );
		m_nX += left - viewportX; m_nY += top - viewportY;
		m_flZ = vgui::surface()->GetZPos();
		m_flAlpha = vgui::surface()->DrawGetAlphaMultiplier();
		bool disabled;
		g_pMatSystemSurface->GetClippingRect( left, top, right, bottom, disabled );
		// Target PushMakeCurrent maintains a CPU clip (T011FB0 -> T003CF0).
		// Preserve that same rectangle in hardware while the uncut, colored
		// quad is rasterized; clipping must not quantize/interpolate new colors.
		m_Context->SetScissorRect( left + viewportX, top + viewportY, right + viewportX, bottom + viewportY, !disabled );
	}
	~CGameModesPaint()
	{
		// Stock target VGUI keeps the hardware scissor disabled; its normal
		// drawing paths clip vertices against the surface's separate CPU state.
		m_Context->SetScissorRect( -1, -1, -1, -1, false );
	}
	void Quad( int texture, int x0, int y0, int x1, int y1, Color color, float angle,
		float s0 = 0, float t0 = 0, float s1 = 1, float t1 = 1,
		int alphaTL = 255, int alphaTR = 255, int alphaBR = 255, int alphaBL = 255 )
	{
		if ( texture == -1 || x1 <= x0 || y1 <= y0 ) return;
		vgui::surface()->DrawSetColor( color ); vgui::surface()->DrawSetTexture( texture );
		const unsigned char drawAlpha = (int)( (float)color.a() * m_flAlpha );
		if ( !drawAlpha ) return;
		const float alphaScale = (float)drawAlpha * 0.0039215689f;
		const int corners[4] = { alphaTL, alphaTR, alphaBR, alphaBL };
		int alpha[4];
		for ( int i = 0; i < 4; ++i )
			alpha[i] = (int)( ( (float)corners[i] * alphaScale ) * 0.0039215689f * 255.0f );
		// V0F3B0 admits material lookup/binding only after both alpha gates.
		if ( !( alpha[0] | alpha[1] | alpha[2] | alpha[3] ) ) return;
		IMaterial *material = g_pMatSystemSurface->DrawGetTextureMaterial( texture );
		if ( !material ) return;
		const int mappingWide = material->GetMappingWidth(), mappingTall = material->GetMappingHeight();
		// V17CC0 binds ordinary resource images with half-texel bounds.
		const float halfU = mappingWide > 0 && mappingTall > 0 ? 0.5f / mappingWide : 0;
		const float halfV = mappingWide > 0 && mappingTall > 0 ? 0.5f / mappingTall : 0;
		const float du = ( 1.0f - halfU ) - halfU, dv = ( 1.0f - halfV ) - halfV;
		const float u[4] = { s0 * du + halfU, s1 * du + halfU, s1 * du + halfU, s0 * du + halfU };
		const float v[4] = { t0 * dv + halfV, t0 * dv + halfV, t1 * dv + halfV, t1 * dv + halfV };
		const float x[4] = { (float)x0, (float)x1, (float)x1, (float)x0 };
		const float y[4] = { (float)y0, (float)y0, (float)y1, (float)y1 };
		const float cx = (float)( x0 + x1 ) * 0.5f, cy = (float)( y0 + y1 ) * 0.5f;
		const float radians = angle * 0.017453292f;
		const float cosine = angle ? (float)cos( (double)radians ) : 1, sine = angle ? (float)sin( (double)radians ) : 0;
		IMesh *mesh = m_Context->GetDynamicMesh( true, NULL, NULL, material );
		CMeshBuilder builder; builder.Begin( mesh, MATERIAL_QUADS, 1 );
		for ( int i = 0; i < 4; ++i )
		{
			const float dx = x[i] - cx, dy = y[i] - cy;
			// V0F3B0 order TL,TR,BR,BL; V49160 indices 012 / 023.
			builder.Color4ub( color.r(), color.g(), color.b(), alpha[i] );
			builder.TexCoord2f( 0, u[i], v[i] );
			builder.Position3f( ( m_nX + cx ) + ( dx * cosine - dy * sine ), ( m_nY + cy ) + ( dy * cosine + dx * sine ), m_flZ );
			builder.AdvanceVertex();
		}
		builder.End(); mesh->Draw();
	}
private:
	CMatRenderContextPtr m_Context;
	int m_nX, m_nY;
	float m_flZ, m_flAlpha;
};

namespace
{
// G15ACC0: a real native UI ConVar, not an invented profile/session store.
ConVar ui_mainmenu_challenge( "ui_mainmenu_challenge", "mutation1", FCVAR_HIDDEN );
CUtlString s_LastGameMode;

int LoadModeTexture( const char *name )
{
	if ( !name || !*name ) return -1;
	int texture = vgui::surface()->DrawGetTextureId( name );
	if ( texture == -1 )
	{
		texture = vgui::surface()->CreateNewTextureID();
		vgui::surface()->DrawSetTextureFile( texture, name, 1, false );
	}
	return texture;
}

void DrawModeTexture( CGameModesPaint &paint, int texture, int x, int y, int wide, int tall, Color color, float angle = 0.0f )
{
	paint.Quad( texture, x, y, x + wide, y + tall, color, angle );
}

void DrawSmearEdge( CGameModesPaint &paint, int texture, int x0, int y0, int x1, int y1, float s0, float s1, int alpha0, int alpha1, Color color )
{
	paint.Quad( texture, x0, y0, x1, y1, color, 0, s0, 0, s1, 1, alpha0, alpha1, alpha1, alpha0 );
}

bool PointInModeRect( int x, int y, int left, int top, int wide, int tall )
{
	return x >= left && x <= left + wide && y >= top && y <= top + tall;
}

class GameModeButton : public L4D360HybridButton
{
	DECLARE_CLASS_SIMPLE( GameModeButton, L4D360HybridButton );
public:
	GameModeButton( GameModes *parent, const char *name, const char *text, const char *command ) : BaseClass( parent, name, text, parent, command ) {}
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE
	{
		const vgui::KeyCode key = GetBaseButtonCode( code );
		if ( key == KEY_LEFT || key == KEY_RIGHT || key == KEY_XBUTTON_LEFT || key == KEY_XBUTTON_RIGHT || key == KEY_XSTICK1_LEFT || key == KEY_XSTICK1_RIGHT || key == KEY_XSTICK2_LEFT || key == KEY_XSTICK2_RIGHT || key == KEY_XBUTTON_Y )
		{
			GetParent()->OnKeyCodePressed( code );
			return;
		}
		BaseClass::OnKeyCodePressed( code );
	}
};
}

GameModes::GameModes( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, "" ),
	m_nSelected( 0 ), m_nQueuedSteps( 0 ), m_flScrollStart( 0 ), m_bScrollRight( false ), m_bHideLabels( false ),
	m_nPicX( 0 ), m_nPicWide( 0 ), m_nPicTall( 0 ), m_nTitleX( 0 ), m_nTitleY( 0 ), m_nTitleWide( 0 ), m_nTitleTall( 0 ), m_nButtonTall( 0 ),
	m_nSubPics( 0 ), m_nSubGap( 0 ), m_nSubOffsetX( 0 ), m_nSubOffsetY( 0 ), m_nSubWide( 0 ), m_nSubTall( 0 ), m_nSubX( 0 ), m_nSubY( 0 ),
	m_nArrowWide( 0 ), m_nArrowTall( 0 ), m_nArrowOffsetY( 0 ), m_nRightArrowOffsetX( 0 ), m_nLeftX( 0 ), m_nLeftY( 0 ), m_nRightX( 0 ), m_nRightY( 0 ),
	m_nBorderTexture( -1 ), m_nTopTexture( -1 ), m_nBottomTexture( -1 ), m_nLeftTexture( -1 ), m_nRightTexture( -1 ), m_hSubFont( 0 ), m_nSubFontTall( 0 ), m_SmearColor( 0, 0, 0, 200 )
{
	SetProportional( true );
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	SetButtonBorderEnabled( false );
	SetKeyBoardInputEnabled( true );
}

GameModes::~GameModes() { ClearModes(); }

void GameModes::ClearModes()
{
	FOR_EACH_VEC( m_Modes, i ) delete m_Modes[i].button;
	m_Modes.RemoveAll();
}

int GameModes::Scale( int value ) { return vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), value ); }

void GameModes::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_nTopTexture = LoadModeTexture( scheme->GetResourceString( "Frame.TopBorderImage" ) );
	m_nBottomTexture = LoadModeTexture( scheme->GetResourceString( "Frame.BottomBorderImage" ) );
	m_SmearColor = scheme->GetColor( "Frame.SmearColor", Color( 0, 0, 0, 200 ) );
}

void GameModes::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings( data );
	ClearModes();
	m_nBorderTexture = LoadModeTexture( data->GetString( "borderimage", "" ) );
	m_nLeftTexture = LoadModeTexture( data->GetString( "leftarrow", "" ) );
	m_nRightTexture = LoadModeTexture( data->GetString( "rightarrow", "" ) );
	m_nPicX = Scale( data->GetInt( "picoffsetx" ) );
	m_nPicWide = Scale( data->GetInt( "picwidth" ) );
	m_nPicTall = Scale( data->GetInt( "picheight" ) );
	m_nTitleX = Scale( data->GetInt( "menutitlex" ) );
	m_nTitleY = Scale( data->GetInt( "menutitley" ) );
	m_nTitleWide = Scale( data->GetInt( "menutitlewide" ) );
	m_nTitleTall = Scale( data->GetInt( "menutitletall" ) );
	m_nSubGap = Scale( data->GetInt( "subpicgap" ) );
	m_nSubOffsetX = Scale( data->GetInt( "subpicoffsetx" ) );
	m_nSubOffsetY = Scale( data->GetInt( "subpicoffsety" ) );
	m_nSubWide = Scale( data->GetInt( "subpicwidth" ) );
	m_nSubTall = Scale( data->GetInt( "subpicheight" ) );
	m_bHideLabels = Scale( data->GetInt( "hidelabels" ) ) != 0;
	m_nArrowWide = Scale( data->GetInt( "arrowwidth" ) );
	m_nArrowTall = Scale( data->GetInt( "arrowheight" ) );
	m_nArrowOffsetY = Scale( data->GetInt( "arrowoffsety" ) );
	m_nRightArrowOffsetX = Scale( data->GetInt( "rightarrowoffsetx" ) );
	vgui::IScheme *scheme = vgui::scheme()->GetIScheme( GetScheme() );
	m_hSubFont = scheme->GetFont( data->GetString( "subpicnamefont", "" ), true );
	m_nSubFontTall = vgui::surface()->GetFontTall( m_hSubFont );
	for ( KeyValues *record = data->GetFirstTrueSubKey(); record; record = record->GetNextTrueSubKey() )
	{
		if ( Q_stricmp( record->GetName(), "mode" ) ) continue;
		Mode_t &mode = m_Modes[m_Modes.AddToTail()];
		mode.id = record->GetString( "id", "" );
		mode.name = record->GetString( "name", "" );
		mode.command = record->GetString( "command", "" );
		mode.title = record->GetString( "menutitle", "" );
		mode.hint = record->GetString( "menuhint", "" );
		mode.disabledHint = record->GetString( "menuhintdisabled", "" );
		mode.texture = LoadModeTexture( record->GetString( "image", "" ) );
		mode.enabled = record->GetInt( "enabled", 1 ) != 0;
		mode.customMode = record->GetInt( "custommode", 0 );
		mode.button = new GameModeButton( this, mode.id.String(), mode.title.String(), mode.command.String() );
		KeyValues::AutoDelete settings( new KeyValues( "BtnGameMode" ) );
		settings->SetInt( "xpos", vgui::scheme()->GetProportionalNormalizedValueEx( GetScheme(), m_nTitleX ) );
		settings->SetInt( "ypos", vgui::scheme()->GetProportionalNormalizedValueEx( GetScheme(), m_nTitleY + m_nPicTall ) );
		settings->SetInt( "wide", vgui::scheme()->GetProportionalNormalizedValueEx( GetScheme(), m_nTitleWide ) );
		settings->SetInt( "tall", vgui::scheme()->GetProportionalNormalizedValueEx( GetScheme(), m_nTitleTall ) );
		settings->SetInt( "autoResize", 1 ); settings->SetInt( "pinCorner", 0 );
		settings->SetInt( "visible", 0 ); settings->SetInt( "enabled", mode.enabled ); settings->SetInt( "tabPosition", 0 );
		settings->SetString( "labelText", mode.title.String() ); settings->SetString( "command", mode.command.String() );
		settings->SetString( "tooltiptext", mode.hint.String() ); settings->SetString( "disabled_tooltiptext", mode.disabledHint.String() );
		settings->SetString( "style", "GameModeButton" ); settings->SetInt( "ActivationType", 1 );
		settings->SetString( "EnableCondition", record->GetString( "EnableCondition", "" ) );
		settings->SetString( "navUp", data->GetString( "navUp", "" ) ); settings->SetString( "navDown", data->GetString( "navDown", "" ) );
		settings->SetInt( "wideatopen", data->GetInt( "wideatopen" ) );
		mode.button->ApplySettings( settings );
		mode.button->InvalidateLayout( true, true );
	}
	m_nButtonTall = m_Modes.Count() ? m_Modes[0].button->GetTall() : m_nTitleTall;
	m_nSubPics = MIN( data->GetInt( "subpics" ), MAX( 0, m_Modes.Count() - 1 ) );
	SetSize( m_nPicWide + m_nPicX + m_nSubOffsetX + m_nSubPics * m_nSubGap + m_nSubWide * ( m_nSubPics + 1 ),
		m_nTitleY + m_nTitleTall + m_nPicTall + ( m_nButtonTall - m_nTitleTall ) / 2 );
	m_nSubX = m_nPicX + m_nPicWide + m_nSubOffsetX;
	m_nSubY = m_nSubOffsetY + ( m_nTitleY + m_nPicTall + m_nTitleTall - m_nSubTall ) / 2;
	m_nLeftX = m_nPicX - m_nArrowWide - m_nSubGap;
	m_nLeftY = m_nSubY + m_nSubTall - m_nArrowTall + m_nArrowOffsetY;
	m_nRightX = m_nRightArrowOffsetX ? m_nPicX + m_nPicWide + m_nSubGap + m_nRightArrowOffsetX : m_nSubX + m_nSubPics * ( m_nSubGap + m_nSubWide );
	m_nRightY = m_nLeftY;
	m_nSelected = 0; m_flScrollStart = 0; m_nQueuedSteps = 0;
	SetActiveMode( s_LastGameMode.String(), false );
	if ( m_Modes.Count() && !m_Modes[m_nSelected].button->IsVisible() ) SetActiveMode( 0, false );
	UpdateCustomModeHint();
}

L4D360HybridButton *GameModes::GetActiveButton() const { return m_Modes.Count() ? m_Modes[m_nSelected].button : NULL; }

void GameModes::SetActiveMode( int index, bool focus )
{
	if ( index < 0 || index >= m_Modes.Count() ) return;
	L4D360HybridButton *previous = GetActiveButton();
	m_nSelected = index;
	s_LastGameMode = m_Modes[index].id;
	FOR_EACH_VEC( m_Modes, i ) m_Modes[i].button->SetVisible( i == index );
	if ( focus )
	{
		if ( previous ) previous->NavigateFrom();
		m_Modes[index].button->NavigateTo();
	}
}

bool GameModes::SetActiveMode( const char *id, bool focus )
{
	FOR_EACH_VEC( m_Modes, i )
		if ( !Q_stricmp( m_Modes[i].id.String(), id ) ) { m_flScrollStart = 0; SetActiveMode( i, focus ); return true; }
	return false;
}

void GameModes::NavigateTo() { if ( GetActiveButton() ) GetActiveButton()->NavigateTo(); else BaseClass::NavigateTo(); }
void GameModes::NavigateFrom() { FOR_EACH_VEC( m_Modes, i ) m_Modes[i].button->NavigateFrom(); BaseClass::NavigateFrom(); }
void GameModes::OnCommand( const char *command ) { if ( GetParent() ) GetParent()->OnCommand( command ); }

bool GameModes::ScrollLeft()
{
	if ( m_flScrollStart != 0 || !m_Modes.Count() ) return false;
	if ( m_nSubPics ) { m_flScrollStart = Plat_FloatTime(); m_bScrollRight = false; vgui::surface()->PlaySound( "UI/buttonclick.wav" ); }
	return true;
}

bool GameModes::ScrollRight( int count )
{
	if ( m_flScrollStart != 0 || !m_Modes.Count() ) return false;
	if ( m_nSubPics )
	{
		m_flScrollStart = Plat_FloatTime(); m_bScrollRight = true;
		m_nQueuedSteps = count <= 1 ? 0 : count;
		vgui::surface()->PlaySound( "UI/buttonclick.wav" );
	}
	return true;
}

void GameModes::OnKeyCodePressed( vgui::KeyCode code )
{
	const vgui::KeyCode key = GetBaseButtonCode( code );
	if ( key == KEY_LEFT || key == KEY_XBUTTON_LEFT || key == KEY_XSTICK1_LEFT || key == KEY_XSTICK2_LEFT )
	{
		if ( m_flScrollStart == 0 && m_nSubPics ) { L4D360UI::FlyoutMenu::CloseActiveMenu(); ScrollLeft(); return; }
	}
	else if ( key == KEY_RIGHT || key == KEY_XBUTTON_RIGHT || key == KEY_XSTICK1_RIGHT || key == KEY_XSTICK2_RIGHT )
	{
		if ( m_flScrollStart == 0 && m_nSubPics ) { L4D360UI::FlyoutMenu::CloseActiveMenu(); ScrollRight(); return; }
	}
	else if ( key == KEY_XBUTTON_Y ) { OnCommand( "SeeAll" ); return; }
	BaseClass::OnKeyCodePressed( code );
}

void GameModes::OnMousePressed( vgui::MouseCode code )
{
	if ( code != MOUSE_LEFT || m_flScrollStart != 0 || !m_nSubPics ) return;
	int x, y; vgui::input()->GetCursorPos( x, y ); ScreenToLocal( x, y );
	if ( PointInModeRect( x, y, m_nPicX, 0, m_nPicWide, m_nPicTall ) )
	{
		L4D360HybridButton *button = GetActiveButton();
		if ( button && !button->IsOpen() && button->IsEnabled() ) button->DoClick();
		return;
	}
	if ( PointInModeRect( x, y, m_nLeftX, m_nLeftY, m_nArrowWide, m_nArrowTall ) ) { L4D360UI::FlyoutMenu::CloseActiveMenu(); ScrollLeft(); return; }
	if ( PointInModeRect( x, y, m_nRightX, m_nRightY, m_nArrowWide, m_nArrowTall ) ) { L4D360UI::FlyoutMenu::CloseActiveMenu(); ScrollRight(); return; }
	for ( int i = 1; i <= m_nSubPics; ++i )
		if ( PointInModeRect( x, y, m_nSubX + ( i - 1 ) * ( m_nSubGap + m_nSubWide ), m_nSubY, m_nSubWide, m_nSubTall ) )
		{ L4D360UI::FlyoutMenu::CloseActiveMenu(); ScrollRight( i ); return; }
}

L4D360HybridButton *GameModes::GetButtonForCommand( const char *command ) const
{
	const char *original = command;
	if ( !Q_stricmp( command, "FlmVersusFlyout" ) ) original = "VersusSoftLock";
	else if ( !Q_stricmp( command, "FlmSurvivalFlyout" ) ) original = "SurvivalCheck";
	else if ( !Q_stricmp( command, "FlmScavengeFlyout" ) ) original = "ScavengeCheck";
	else if ( !Q_stricmp( command, "FlmChallengeFlyout1" ) || !Q_stricmp( command, "FlmChallengeFlyout4" ) || !Q_stricmp( command, "FlmChallengeFlyout8" ) ) original = "PlayChallenge";
	FOR_EACH_VEC( m_Modes, i ) if ( !Q_stricmp( m_Modes[i].command.String(), original ) ) return m_Modes[i].button;
	return NULL;
}

void GameModes::UpdateCustomModeHint()
{
	FOR_EACH_VEC( m_Modes, i )
	{
		if ( !m_Modes[i].customMode ) continue;
		const char *name = ui_mainmenu_challenge.GetString();
		KeyValues *mode = g_TerrorModes.FindMode( name );
		if ( !mode ) { m_Modes[i].button->SetTooltip( "Choose a Mode" ); return; }
		if ( !mode->GetInt( "addon", 1 ) )
			m_Modes[i].button->SetTooltip( CFmtStr( "#L4D360UI_MainMenu_PlayChallenge_Tip_%s", name ) );
		else
		{
			char title[1024], description[1024];
			g_pVGuiLocalize->ConvertUnicodeToANSI( mode->GetWString( "DisplayTitle", L"unicode fail" ), title, sizeof( title ) );
			g_pVGuiLocalize->ConvertUnicodeToANSI( mode->GetWString( "ShortDescription", mode->GetWString( "Description", L"unicode fail" ) ), description, sizeof( description ) );
			m_Modes[i].button->SetTooltip( CFmtStr( "%s - %s", title, description ) );
		}
		return;
	}
}

void GameModes::DrawSmear( CGameModesPaint &paint, int x0, int y0, int x1, int y1 )
{
	const int top = Scale( 21 ), bottom = Scale( 21 );
	const int middle = MAX( 0, y1 - y0 - top - bottom );
	const int x05 = x0 + (int)( ( x1 - x0 ) * 0.05f ), x20 = x0 + (int)( ( x1 - x0 ) * 0.2f );
	DrawSmearEdge( paint, m_nTopTexture, x0, y0, x05, y0 + top, 0, 0.05f, 0, 255, m_SmearColor );
	DrawSmearEdge( paint, m_nTopTexture, x05, y0, x20, y0 + top, 0.05f, 0.2f, 255, 255, m_SmearColor );
	DrawSmearEdge( paint, m_nTopTexture, x20, y0, x1, y0 + top, 0.2f, 1, 255, 0, m_SmearColor );
	if ( middle )
	{
		vgui::surface()->DrawSetColor( m_SmearColor );
		vgui::surface()->DrawFilledRectFade( x0, y0 + top, x05, y0 + top + middle, 0, 255, true );
		vgui::surface()->DrawFilledRectFade( x05, y0 + top, x20, y0 + top + middle, 255, 255, true );
		vgui::surface()->DrawFilledRectFade( x20, y0 + top, x1, y0 + top + middle, 255, 0, true );
	}
	const int y = y0 + top + middle;
	DrawSmearEdge( paint, m_nBottomTexture, x0, y, x05, y + bottom, 0, 0.05f, 0, 255, m_SmearColor );
	DrawSmearEdge( paint, m_nBottomTexture, x05, y, x20, y + bottom, 0.05f, 0.2f, 255, 255, m_SmearColor );
	DrawSmearEdge( paint, m_nBottomTexture, x20, y, x1, y + bottom, 0.2f, 1, 255, 0, m_SmearColor );
}

void GameModes::DrawSmallCard( CGameModesPaint &paint, int index, int x, int y, int shade, int alpha )
{
	DrawModeTexture( paint, m_Modes[index].texture, x, y, m_nSubWide, m_nSubTall, Color( shade, shade, shade, alpha ) );
	if ( m_bHideLabels ) return;
	vgui::surface()->DrawSetColor( 0, 0, 0, alpha );
	vgui::surface()->DrawFilledRect( x, y + m_nSubTall - m_nSubFontTall, x + m_nSubWide, y + m_nSubTall );
	wchar_t textBuffer[1024];
	const char *name = m_Modes[index].name.String();
	const wchar_t *text = *name == '#' ? g_pVGuiLocalize->Find( name ) : NULL;
	if ( !text && *name != '#' ) { g_pVGuiLocalize->ConvertANSIToUnicode( name, textBuffer, sizeof( textBuffer ) ); text = textBuffer; }
	if ( !text ) return;
	int wide, tall; vgui::surface()->GetTextSize( m_hSubFont, text, wide, tall );
	vgui::surface()->DrawSetTextFont( m_hSubFont );
	vgui::surface()->DrawSetTextColor( shade, shade, shade, alpha );
	vgui::surface()->DrawSetTextPos( x + vgui::surface()->GetCharacterWidth( m_hSubFont, L' ' ), y + m_nSubTall - m_nSubFontTall / 2 - tall / 2 );
	vgui::surface()->DrawPrintText( text, V_wcslen( text ) );
}

void GameModes::Paint()
{
	if ( !m_Modes.Count() ) return;
	CGameModesPaint paint( this );
	L4D360HybridButton *button = GetActiveButton();
	const bool focus = button->HasFocus() || button->IsNavigationFocused();
	const bool open = button->IsOpen();
	float progress = 0;
	if ( m_flScrollStart != 0 )
	{
		progress = ( Plat_FloatTime() - m_flScrollStart ) * ( m_nQueuedSteps > 0 ? 8.0f : 5.0f );
		if ( progress >= 1.0f )
		{
			progress = 1.0f; m_flScrollStart = 0;
			SetActiveMode( ( m_nSelected + ( m_bScrollRight ? 1 : m_Modes.Count() - 1 ) ) % m_Modes.Count(), focus );
			if ( --m_nQueuedSteps > 0 ) { m_flScrollStart = Plat_FloatTime(); progress = 0; }
		}
	}
	const bool scrolling = m_flScrollStart != 0;
	const int count = m_nSubPics + 1 + ( scrolling && m_bScrollRight ? 1 : 0 );
	const int next = ( m_nSelected + 1 ) % m_Modes.Count(), previous = ( m_nSelected + m_Modes.Count() - 1 ) % m_Modes.Count();
	const int titleY = m_nPicTall + m_nTitleY + ( m_nButtonTall - m_nTitleTall ) / 2;
	DrawSmear( paint, (int)( m_nTitleX * 0.7f ), titleY, m_nSubX + m_nSubPics * ( m_nSubWide + m_nSubGap ), titleY + m_nTitleTall );
	for ( int slot = 0; slot < count; ++slot )
	{
		const int mode = ( m_nSelected + slot ) % m_Modes.Count();
		if ( !slot )
		{
			if ( !scrolling )
			{
				DrawModeTexture( paint, m_Modes[mode].texture, m_nPicX, 0, m_nPicWide, m_nPicTall, Color( 255, 255, 255, 255 ), -3 );
				DrawModeTexture( paint, m_nBorderTexture, m_nPicX, 0, m_nPicWide, m_nPicTall, focus || open ? Color( 255, 255, 255, 255 ) : Color( 0, 0, 0, 255 ), -3 );
			}
			else
			{
				const float left = m_bScrollRight ? 1.0f - progress : progress;
				const int leftX = m_nPicX - m_nSubOffsetX - m_nSubWide;
				const int x = (int)Lerp( left, (float)leftX, (float)m_nPicX ), y = (int)Lerp( left, (float)m_nSubY, 0.0f );
				const int wide = (int)Lerp( left, (float)m_nSubWide, (float)m_nPicWide ), tall = (int)Lerp( left, (float)m_nSubTall, (float)m_nPicTall );
				const Color color( 255, 255, 255, (int)( left * 255 ) );
				DrawModeTexture( paint, m_Modes[m_bScrollRight ? m_nSelected : previous].texture, x, y, wide, tall, color, -3 * left );
				DrawModeTexture( paint, m_nBorderTexture, x, y, wide, tall, color, -3 * left );
				const float right = m_bScrollRight ? progress : 1.0f - progress;
				const int shade = (int)( right * 130 + 125 );
				DrawModeTexture( paint, m_Modes[m_bScrollRight ? next : m_nSelected].texture,
					(int)Lerp( right, (float)m_nSubX, (float)m_nPicX ), (int)Lerp( right, (float)m_nSubY, 0.0f ),
					(int)Lerp( right, (float)m_nSubWide, (float)m_nPicWide ), (int)Lerp( right, (float)m_nSubTall, (float)m_nPicTall ), Color( shade, shade, shade, 255 ), -3 * right );
			}
		}
		else if ( !( scrolling && m_bScrollRight && mode == next ) )
		{
			int x = m_nSubX + ( slot - 1 ) * ( m_nSubGap + m_nSubWide ), alpha = 255;
			if ( scrolling )
			{
				x += (int)( ( m_bScrollRight ? -1 : 1 ) * ( m_nSubGap + m_nSubWide ) * progress );
				if ( slot == count - 1 ) alpha = (int)( ( m_bScrollRight ? progress : 1.0f - progress ) * 255 );
			}
			DrawSmallCard( paint, mode, x, m_nSubY, focus || open ? 255 : 125, alpha );
		}
	}
	if ( m_nSubPics )
	{
		int x, y; vgui::input()->GetCursorPos( x, y ); ScreenToLocal( x, y );
		const int left = !scrolling && PointInModeRect( x, y, m_nLeftX, m_nLeftY, m_nArrowWide, m_nArrowTall ) ? 255 : 125;
		const int right = !scrolling && PointInModeRect( x, y, m_nRightX, m_nRightY, m_nArrowWide, m_nArrowTall ) ? 255 : 125;
		DrawModeTexture( paint, m_nLeftTexture, m_nLeftX, m_nLeftY, m_nArrowWide, m_nArrowTall, Color( left, left, left, 255 ) );
		DrawModeTexture( paint, m_nRightTexture, m_nRightX, m_nRightY, m_nArrowWide, m_nArrowTall, Color( right, right, right, 255 ) );
	}
}

namespace L4D360UI
{
const char *ResolveBaseModChallengeMode( const char *name, char *buffer, int bytes )
{
	if ( !Q_stricmp( name, "challenge" ) ) return ui_mainmenu_challenge.GetString();
	if ( Q_stricmp( name, "teamchallenge" ) ) return name;
	Q_snprintf( buffer, bytes, "team%s", ui_mainmenu_challenge.GetString() );
	return g_TerrorModes.FindMode( buffer ) ? buffer : ui_mainmenu_challenge.GetString();
}
const char *GetBaseModChallengeFlyout()
{
	KeyValues *mode = g_TerrorModes.FindMode( ui_mainmenu_challenge.GetString() );
	if ( !mode ) return "ChooseChallengeMode";
	const int players = mode->GetInt( "maxplayers", 0 );
	return players <= 1 ? "FlmChallengeFlyout1" : players <= 4 ? "FlmChallengeFlyout4" : "FlmChallengeFlyout8";
}
void SetBaseModChallengeMode( const char *name ) { ui_mainmenu_challenge.SetValue( name ); }
}
