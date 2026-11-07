//========= Copyright Valve Corporation, All rights reserved. ============//
// Native L4D360UI common controls. Original x86 addresses are evidence only.
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_gamemodes.h"
#include "l4d2/ui/basemodui/terror_basemodui_background.h"
#include "l4d2/ui/basemodui/terror_basemodui_panellist.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui/IInput.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "inputsystem/ButtonCode.h"
#include "tier1/KeyValues.h"
#include "tier1/fmtstr.h"
#include <wctype.h>
#include "tier0/memdbgon.h"

using namespace vgui;
namespace L4D360UI
{
DECLARE_BUILD_FACTORY( L4D360HybridButton );
DECLARE_BUILD_FACTORY( CL4DFrame );
namespace
{
int LoadTexture( const char *name )
{
	int id = surface()->DrawGetTextureId( name );
	if ( id == -1 )
	{
		id = surface()->CreateNewTextureID();
		surface()->DrawSetTextureFile( id, name, true, false );
	}
	return id;
}
int Scale( Panel *panel, int value )
{
	return scheme()->GetProportionalScaledValueEx( panel->GetScheme(), value );
}
Color NativeColor( unsigned int rgba )
{
	Color color;
	color.SetRawColor( rgba );
	return color;
}
const wchar_t *LocalizeText( const char *text, wchar_t *buffer, int bytes )
{
	const wchar_t *localized = text && *text == '#' ? g_pVGuiLocalize->Find( text ) : NULL;
	if ( localized ) return localized;
	g_pVGuiLocalize->ConvertANSIToUnicode( text ? text : "", buffer, bytes );
	return buffer;
}
void DrawText( HFont font, int x, int y, Color color, const wchar_t *text, int length )
{
	surface()->DrawSetTextFont( font );
	surface()->DrawSetTextPos( x, y );
	surface()->DrawSetTextColor( color );
	surface()->DrawPrintText( text, length );
}
}

L4D360HybridButton::L4D360HybridButton( Panel *parent, const char *name, const char *text, Panel *target, const char *command ) :
	BaseClass( parent, name, text, target, command ), m_nStyle( 0 ), m_nOriginalTall( 0 ), m_nTextInsetX( 0 ), m_nTextInsetY( 0 ), m_nWideAtOpen( 0 ),
	m_nUsablePlayerIndex( -1 ), m_nEnableCondition( 0 ), m_nIndicatorSize( 0 ), m_hFont( 0 ), m_hBlurFont( 0 ), m_hHintFont( 0 ),
	m_hSelectionFont( 0 ), m_hSelectionBlurFont( 0 ), m_nFontTall( 0 ), m_nHintTall( 0 ), m_nDownTexture( -1 ), m_nRightTexture( -1 ),
	m_DefaultTextColor( 0, 0, 0, 0 ), m_bOpen( false ), m_bNavigateFocus( false ), m_bOnlyActiveUser( false ), m_bIgnoreButtonA( false ),
	m_bShowIndicator( false ), m_bIndicatorOpen( false ), m_bDropDownEnabled( false )
{
	m_Style = "DefaultButton";
	SetPaintBackgroundEnabled( false );
	SetPaintBorderEnabled( false );
	SetContentAlignment( Label::a_west );
	SetButtonActivationType( ACTIVATE_ONRELEASED ); // G5CF70; resource defaults to on-press.
	SetProportional( true );
}

const char *L4D360HybridButton::Command() const
{
	KeyValues *command = const_cast<L4D360HybridButton *>( this )->GetCommand();
	return command ? command->GetString( "command", "" ) : "";
}
void L4D360HybridButton::SetCurrentSelection( const char *text ) { m_Selection = text ? text : ""; Repaint(); }
void L4D360HybridButton::SetDropDownIndicator( bool show ) { m_bShowIndicator = show; Repaint(); }
void L4D360HybridButton::SetDropDownIndicatorOpen( bool open ) { m_bIndicatorOpen = open; Repaint(); }
void L4D360HybridButton::SetTooltip( const char *text, bool enabled )
{
	if ( enabled ) m_Tooltip = text ? text : ""; else m_DisabledTooltip = text ? text : "";
	if ( HasFocus() ) UpdateFooter();
}
const char *L4D360HybridButton::GetTooltip() const
{
	return const_cast<L4D360HybridButton *>( this )->IsEnabled() ? m_Tooltip.String() : m_DisabledTooltip.String();
}
void L4D360HybridButton::UpdateFooter()
{
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( footer ) footer->SetHelpText( GetTooltip() );
}

void L4D360HybridButton::ApplyStyle( IScheme *uiScheme )
{
	if ( !uiScheme ) return;
	char key[128];
	Q_snprintf( key, sizeof( key ), "%s.Style", m_Style.String() );
	m_nStyle = atoi( uiScheme->GetResourceString( key ) );
	const char *font = "DefaultBold", *blur = "DefaultBoldBlur";
	m_hHintFont = m_hSelectionFont = m_hSelectionBlurFont = 0;
	switch ( m_nStyle )
	{
	case 1: case 9: font = "MainBold"; blur = "MainBoldBlur"; break;
	case 2: case 8: font = "DefaultMedium"; blur = "DefaultMediumBlur"; break;
	case 7: font = blur = "DefaultVerySmall"; break;
	case 0: case 3: case 4: case 5: case 6: case 10: break;
	default: m_nStyle = 0; break;
	}
	m_hFont = uiScheme->GetFont( font, true );
	m_hBlurFont = uiScheme->GetFont( blur, true );
	if ( m_nStyle == 3 )
	{
		m_hSelectionFont = uiScheme->GetFont( "DefaultMedium", true );
		m_hSelectionBlurFont = uiScheme->GetFont( "DefaultMediumBlur", true );
	}
	if ( m_nStyle == 9 ) m_hHintFont = uiScheme->GetFont( "Default", true );
	m_nFontTall = surface()->GetFontTall( m_hFont );
	m_nHintTall = m_hHintFont ? surface()->GetFontTall( m_hHintFont ) : 0;
	SetFont( m_hFont );
	Q_snprintf( key, sizeof( key ), "%s.TextInsetX", m_Style.String() );
	const char *value = uiScheme->GetResourceString( key );
	if ( value && *value ) m_nTextInsetX = Scale( this, atoi( value ) );
	Q_snprintf( key, sizeof( key ), "%s.TextInsetY", m_Style.String() );
	value = uiScheme->GetResourceString( key );
	if ( value && *value ) m_nTextInsetY = Scale( this, atoi( value ) );
	SetTextInset( m_nTextInsetX, m_nTextInsetY );
	if ( m_nStyle == 9 )
	{
		SetTall( m_nFontTall + m_nHintTall );
		m_nOriginalTall = m_nFontTall;
	}
}
void L4D360HybridButton::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings( data );
	m_Style = data->GetString( "Style", "DefaultButton" );
	m_nOriginalTall = GetTall();
	m_nWideAtOpen = scheme()->GetProportionalScaledValue( data->GetInt( "wideatopen", 0 ) );
	ApplyStyle( scheme()->GetIScheme( GetScheme() ) );
	if ( data->FindKey( "tooltiptext" ) ) SetTooltip( data->GetString( "tooltiptext" ) );
	if ( data->FindKey( "disabled_tooltiptext" ) ) SetTooltip( data->GetString( "disabled_tooltiptext" ), false );
	SetButtonActivationType( (ActivationType_t)data->GetInt( "ActivationType", 1 ) );
	const char *player = data->GetString( "usablePlayerIndex", "" );
	m_nUsablePlayerIndex = -1;
	if ( !Q_stricmp( player, "primary" ) ) m_nUsablePlayerIndex = 255;
	else if ( !Q_stricmp( player, "nobody" ) ) m_nUsablePlayerIndex = -2;
	else if ( *player >= '0' && *player <= '9' ) m_nUsablePlayerIndex = atoi( player );
	const char *condition = data->GetString( "EnableCondition", "" );
	if ( !Q_stricmp( condition, "LiveRequired" ) ) m_nEnableCondition = 1;
	else if ( !Q_stricmp( condition, "Never" ) ) m_nEnableCondition = 2;
	m_bOnlyActiveUser = data->GetBool( "OnlyActiveUser", false );
	m_bIgnoreButtonA = data->GetBool( "IgnoreButtonA", false );
	m_bShowIndicator = data->GetBool( "ShowDropDownIndicator", false );
	m_bIndicatorOpen = false;
	m_nIndicatorSize = scheme()->GetProportionalScaledValue( data->GetInt( "DropDownIndicatorSize", 8 ) );
	if ( m_nEnableCondition == 2 && IsEnabled() )
	{
		SetEnabled( false );
		SetTooltip( "#L4D360UI_MainMenu_DemoVersion", false );
	}
	if ( data->GetInt( "tabPosition", 0 ) == 1 ) NavigateTo();
}
void L4D360HybridButton::ApplySchemeSettings( IScheme *uiScheme )
{
	BaseClass::ApplySchemeSettings( uiScheme );
	SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false );
	SetReleasedSound( "UI/menu_accept.wav" );
	m_nDownTexture = LoadTexture( "vgui/icon_arrow_down" );
	m_nRightTexture = LoadTexture( "vgui/icon_arrow" );
	ApplyStyle( uiScheme );
}

bool L4D360HybridButton::IsNavigationFocused()
{
	const State_t state = GetCurrentState();
	return state == STATE_FOCUS_ENABLED || state == STATE_FOCUS_DISABLED;
}
L4D360HybridButton::State_t L4D360HybridButton::GetCurrentState()
{
	State_t state = STATE_ENABLED;
	if ( HasFocus() ) state = IsEnabled() ? STATE_FOCUS_ENABLED : STATE_FOCUS_DISABLED;
	if ( m_bOpen ) return STATE_OPEN;
	if ( IsArmed() || m_bNavigateFocus )
	{
		state = IsEnabled() ? STATE_FOCUS_ENABLED : STATE_FOCUS_DISABLED;
		if ( IsArmed() ) m_bNavigateFocus = false;
		return state;
	}
	return IsEnabled() ? state : STATE_DISABLED;
}
void L4D360HybridButton::BroadcastSiblingOpened()
{
	Panel *parent = GetParent();
	if ( !parent ) return;
	for ( int i = 0; i < parent->GetChildCount(); ++i )
	{
		L4D360HybridButton *sibling = dynamic_cast<L4D360HybridButton *>( parent->GetChild( i ) );
		if ( sibling && sibling != this ) PostMessage( sibling, new KeyValues( "OnSiblingHybridButtonOpened" ) );
	}
}
void L4D360HybridButton::SetOpen()
{
	if ( !m_bOpen ) { m_bOpen = true; BroadcastSiblingOpened(); Repaint(); }
}
void L4D360HybridButton::SetClosed() { m_bOpen = false; Repaint(); }
void L4D360HybridButton::OnSiblingHybridButtonOpened()
{
	FlyoutMenu *active = FlyoutMenu::GetActiveMenu();
	if ( active && active->GetParentButton() == this )
	{
		FlyoutMenu::CloseActiveMenu();
		m_bNavigateFocus = false;
	}
	else m_bOpen = m_bNavigateFocus = false;
	Repaint();
}
void L4D360HybridButton::FireActionSignal()
{
	BaseClass::FireActionSignal();
	BroadcastSiblingOpened();
}
void L4D360HybridButton::NavigateTo()
{
	// Native Button::NavigateTo is Panel navigation plus SetArmed. Target
	// Button adds an earlier PC RequestFocus; retain the native ordering here.
	Panel::NavigateTo();
	SetArmed( true );
	UpdateFooter();
	FlyoutMenu *flyout = dynamic_cast<FlyoutMenu *>( GetParent() );
	if ( flyout ) flyout->NavigateToChild( this );
	if ( GetParent() ) PostMessage( GetParent(), new KeyValues( "OnHybridButtonNavigatedTo", "button", GetName() ) );
	m_bNavigateFocus = true;
	RequestFocus();
}
void L4D360HybridButton::NavigateFrom()
{
	// Target Button::NavigateFrom synthesizes a controller-A release. Native
	// E5370 does not: navigation must not accidentally fire an activation.
	Panel::NavigateFrom();
	SetArmed( false );
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( footer ) footer->FadeOutHelpText();
	m_bNavigateFocus = false;
}
void L4D360HybridButton::OnCursorEntered()
{
	// Native E59A0 dispatches NavigateTo; target stock Button only arms itself.
	if ( IsEnabled() ) NavigateTo();
	if ( m_bOpen ) return;
	if ( IsEnabled() && !HasFocus() ) surface()->PlaySound( "UI/menu_focus.wav" );
	if ( GetParent() ) GetParent()->NavigateToChild( this ); else NavigateTo();
}
void L4D360HybridButton::OnCursorExited()
{
	if ( !_buttonFlags.IsFlagSet( BUTTON_KEY_DOWN ) ) NavigateFrom();
}
void L4D360HybridButton::OnMousePressed( MouseCode code )
{
	BaseClass::OnMousePressed( code );
	if ( code == MOUSE_RIGHT || ( code == MOUSE_LEFT && !IsEnabled() && !dynamic_cast<FlyoutMenu *>( GetParent() ) ) )
		FlyoutMenu::CloseActiveMenu( this );
	RequestFocus();
}
void L4D360HybridButton::OnKeyCodePressed( KeyCode code )
{
	const int user = GetJoystickForCode( code );
	if ( m_bOnlyActiveUser && user != 0 ) return;
	if ( GetBaseButtonCode( code ) == KEY_XBUTTON_A )
	{
		if ( m_bIgnoreButtonA ) { CallParentFunction( new KeyValues( "KeyCodePressed", "code", code ) ); return; }
		const bool usable = m_nUsablePlayerIndex == -1 || m_nUsablePlayerIndex == user || ( m_nUsablePlayerIndex == 255 && user == 0 );
		if ( !usable || !IsEnabled() ) { surface()->PlaySound( "UI/menu_invalid.wav" ); return; }
	}
	BaseClass::OnKeyCodePressed( code );
}
void L4D360HybridButton::OnKeyCodeReleased( KeyCode code )
{
	const bool armed = IsArmed();
	BaseClass::OnKeyCodeReleased( code );
	if ( armed && !IsArmed() ) SetArmed( true );
}
void L4D360HybridButton::OnThink()
{
	// G5C630's PC LiveRequired arm enables the widget. Session reachability is
	// decided by the owning screen, not a fabricated framework/profile service.
	if ( m_nEnableCondition == 1 ) SetEnabled( true );
	else if ( m_nEnableCondition == 2 && IsEnabled() )
	{
		SetEnabled( false ); SetTooltip( "#L4D360UI_MainMenu_DemoVersion", false );
	}
	BaseClass::OnThink();
}
Panel *L4D360HybridButton::NavigateUp()
{
	Panel *next = BaseClass::NavigateUp();
	const bool relay = dynamic_cast<DropDownMenu *>( GetParent() ) || dynamic_cast<SliderControl *>( GetParent() );
	return !next && relay ? GetParent()->NavigateUp() : next;
}
Panel *L4D360HybridButton::NavigateDown()
{
	Panel *next = BaseClass::NavigateDown();
	const bool relay = dynamic_cast<DropDownMenu *>( GetParent() ) || dynamic_cast<SliderControl *>( GetParent() );
	return !next && relay ? GetParent()->NavigateDown() : next;
}
Panel *L4D360HybridButton::NavigateLeft()
{
	Panel *next = BaseClass::NavigateLeft();
	return !next && dynamic_cast<DropDownMenu *>( GetParent() ) ? GetParent()->NavigateLeft() : next;
}
Panel *L4D360HybridButton::NavigateRight()
{
	Panel *next = BaseClass::NavigateRight();
	return !next && dynamic_cast<DropDownMenu *>( GetParent() ) ? GetParent()->NavigateRight() : next;
}

void L4D360HybridButton::Paint()
{
	IScheme *uiScheme = scheme()->GetIScheme( GetScheme() );
	if ( !uiScheme || !m_hFont ) return;
	const Color blotch = uiScheme->GetColor( "HybridButton.BlotchColor", Color( 0, 0, 0, 255 ) );
	const Color border = uiScheme->GetColor( "HybridButton.BorderColor", Color( 0, 0, 0, 255 ) );
	int wide = GetWide(), tall = m_nOriginalTall;
	int top = m_nStyle == 9 ? 0 : ( GetTall() - tall ) / 2;
	const State_t state = GetCurrentState();
	if ( ( m_nStyle == 3 || m_nStyle == 9 ) && state == STATE_OPEN && m_nWideAtOpen ) wide = m_nWideAtOpen;
	Color textColor( 0, 0, 0, 0 );
	bool solidText = true, blur = false, pulse = false, highlight = false;
	switch ( state )
	{
	case STATE_ENABLED:
		textColor = NativeColor( m_nStyle == 5 || m_nStyle == 6 ? 0xFF808000u : 0xFF7D7D7Du );
		if ( m_DefaultTextColor.GetRawColor() ) textColor = m_DefaultTextColor;
		break;
	case STATE_DISABLED: textColor = NativeColor( 0xFF686158u ); solidText = false; blur = true; break;
	case STATE_FOCUS_ENABLED:
		textColor = Color( 255, 255, 255, 255 ); blur = pulse = true;
		highlight = m_nStyle == 0 || m_nStyle == 3 || m_nStyle == 4 || m_nStyle == 5;
		break;
	case STATE_FOCUS_DISABLED: textColor = NativeColor( 0xFFC2BDB6u ); solidText = false; blur = true; break;
	case STATE_OPEN: textColor = Color( 200, 200, 200, 255 ); blur = highlight = true; break;
	}
	wchar_t text[512]; GetText( text, sizeof( text ) );
	const int length = Q_wcslen( text );
	int textWide, textTall; surface()->GetTextSize( m_hFont, text, textWide, textTall );
	textWide = MIN( MAX( 0, textWide ), wide - 2 * m_nTextInsetX );
	const int inset = m_nTextInsetX;
	int x = m_nStyle == 4 ? ( wide - textWide ) / 2 : inset;
	FlyoutMenu *active = FlyoutMenu::GetActiveMenu();
	if ( ( !active || active->GetParentButton() == this ) && highlight )
	{
		if ( state == STATE_OPEN )
		{
			surface()->DrawSetColor( 0, 0, 0, 255 );
			surface()->DrawFilledRectFade( 0, top, wide, top + tall, 0, 255, true );
		}
		surface()->DrawSetColor( blotch );
		const int blotchWide = m_nStyle == 4 ? textWide : textWide + Scale( this, 40 );
		const int split = x + (int)( blotchWide * ( m_nStyle == 4 ? 0.5f : 0.25f ) );
		surface()->DrawFilledRectFade( x, top, split, top + tall, 0, 150, true );
		surface()->DrawFilledRectFade( split, top, x + blotchWide, top + tall, 150, 0, true );
		surface()->DrawSetColor( border );
		if ( state == STATE_OPEN )
		{
			if ( active && active->GetOriginalTall() == 0 && active->GetParentButton() == this )
				surface()->DrawFilledRectFade( 0, top, wide, top + 2, 255, 0, true );
			else
			{
				surface()->DrawFilledRectFade( 0, top, wide, top + 2, 0, 255, true );
				surface()->DrawFilledRectFade( 0, top + tall - 2, wide, top + tall, 0, 255, true );
			}
		}
		else
		{
			const int half = (int)( wide * 0.5f );
			surface()->DrawFilledRectFade( 0, top, half, top + 2, 0, 255, true );
			surface()->DrawFilledRectFade( half, top, wide, top + 2, 255, 0, true );
			surface()->DrawFilledRectFade( 0, top + tall - 2, half, top + tall, 0, 255, true );
			surface()->DrawFilledRectFade( half, top + tall - 2, wide, top + tall, 255, 0, true );
		}
	}
	int drawn = length;
	if ( solidText )
	{
		int available = GetWide() - x;
		if ( m_bShowIndicator )
		{
			float textAdvance = 0.0f;
			for ( int i = 0; i < length; ++i )
			{
				float advance, abcA;
				surface()->GetKernedCharWidth( m_hFont, text[i], i ? text[i-1] : 0, i+1 < length ? text[i+1] : 0, advance, abcA );
				textAdvance += advance;
			}
			int indicatorX = (int)( inset + textAdvance + m_nIndicatorSize / 2 );
			if ( indicatorX + m_nIndicatorSize > GetWide() ) indicatorX = GetWide() - m_nIndicatorSize;
			const int indicatorY = top + ( m_nFontTall - m_nIndicatorSize ) / 2;
			surface()->DrawSetTexture( state == STATE_OPEN || m_bIndicatorOpen ? m_nDownTexture : m_nRightTexture );
			surface()->DrawSetColor( textColor );
			surface()->DrawTexturedRect( indicatorX, indicatorY, indicatorX + m_nIndicatorSize, indicatorY + m_nIndicatorSize );
			surface()->DrawSetTexture( 0 );
			available -= 2 * m_nIndicatorSize;
		}
		surface()->DrawSetTextFont( m_hFont ); surface()->DrawSetTextPos( x, top + m_nTextInsetY ); surface()->DrawSetTextColor( textColor );
		if ( textWide <= available ) surface()->DrawUnicodeString( text );
		else
		{
			available -= 3 * surface()->GetCharacterWidth( m_hFont, L'.' );
			drawn = 0;
			int pen = inset;
			for ( int i = 0; i < length; ++i )
			{
				surface()->DrawUnicodeChar( text[i] ); ++drawn;
				pen += surface()->GetCharacterWidth( m_hFont, text[i] );
				if ( pen >= inset + available ) break;
			}
			surface()->DrawPrintText( L"...", 3 );
		}
	}
	else
	{
		Color faint = textColor; faint[3] = 64;
		DrawText( m_hFont, x, top + m_nTextInsetY, faint, text, length );
	}
	if ( m_nStyle == 9 && state != STATE_OPEN )
	{
		wchar_t hintBuffer[512];
		const char *tooltip = GetTooltip();
		const wchar_t *hint = *tooltip == '#' ? g_pVGuiLocalize->Find( tooltip ) : NULL;
		if ( !hint )
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( *tooltip == '#' ? m_Selection.String() : tooltip, hintBuffer, sizeof( hintBuffer ) );
			hint = hintBuffer;
		}
		DrawText( m_hHintFont, x, top + m_nTextInsetY + m_nFontTall, textColor, hint, Q_wcslen( hint ) );
	}
	const int blurAlpha = pulse ? (int)( sin( Plat_FloatTime() * 4.0 ) * 30.0 + 60.0 ) : 30;
	if ( blur ) DrawText( m_hBlurFont, x, top + m_nTextInsetY,
		solidText ? Color( 255, 255, 255, blurAlpha ) : textColor, text, drawn );
	if ( m_nStyle == 3 && state != STATE_OPEN )
	{
		wchar_t selectionBuffer[512];
		const wchar_t *selection = LocalizeText( m_Selection.String(), selectionBuffer, sizeof( selectionBuffer ) );
		int sw, sh; surface()->GetTextSize( m_hSelectionFont, selection, sw, sh );
		const int sx = wide - sw - inset, sy = ( tall - sh ) / 2;
		DrawText( m_hSelectionFont, sx, sy, textColor, selection, Q_wcslen( selection ) );
		if ( blur ) DrawText( m_hSelectionBlurFont, sx, sy, Color( 255, 255, 255, blurAlpha ), selection, Q_wcslen( selection ) );
	}
	m_bNavigateFocus = false; // G5C610 clears the one-paint navigation latch.
}

CL4DFrame::CL4DFrame( Panel *parent, const char *name, bool okButton, bool cancelButton, bool markForDelete, bool sizeable ) :
	BaseClass( parent, name, true, false ), m_nTitleInsetX( 6 ), m_nTitleInsetY( 4 ), m_nContentTall( 0 ),
	m_nTopTexture( -1 ), m_nBottomTexture( -1 ), m_SmearColor( 0, 0, 0, 225 ), m_iHeaderY( 19 ), m_iHeaderTall( 41 ), m_iTitleXOffset( 180 ),
	m_pTitle( NULL ), m_bOKEnabled( okButton ), m_bCancelEnabled( cancelButton ), m_bFooterEnabled( false ), m_bLayoutLoaded( false )
{
	SetProportional( true );
	SetTitle( "", false );
	m_pTitle = new Label( this, "LblTitle", "" );
	m_Resource = CFmtStr( "Resource/UI/L4D360UI/%s.res", name );
	SetTitleBarVisible( false ); SetMoveable( false ); SetSizeable( sizeable );
	SetMenuButtonVisible( false ); SetCloseButtonVisible( false );
	SetMinimizeButtonVisible( false ); SetMaximizeButtonVisible( false );
	SetPaintBorderEnabled( false );
	DisableFadeEffect();
	SetDeleteSelfOnClose( markForDelete );
	if ( parent ) SetScheme( parent->GetScheme() );
}
CL4DFrame::~CL4DFrame() {}
void CL4DFrame::SetOkButtonEnabled( bool enabled )
{
	m_bOKEnabled = enabled;
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( !footer ) return;
	const int buttons = footer->GetButtons();
	footer->SetButtons( enabled ? buttons | FB_BUTTON_A : buttons & ~FB_BUTTON_A,
		footer->GetButtonLayout(), footer->IsHelpTextEnabled() );
}
void CL4DFrame::SetCancelButtonEnabled( bool enabled )
{
	m_bCancelEnabled = enabled;
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( !footer ) return;
	const int buttons = footer->GetButtons();
	footer->SetButtons( enabled ? buttons | FB_BUTTON_B : buttons & ~FB_BUTTON_B,
		footer->GetButtonLayout(), footer->IsHelpTextEnabled() );
}
void CL4DFrame::SetDataSettings( KeyValues *settings )
{
	// G0EB30 is the native empty default hook. A derived screen that owns
	// settings overrides it; the common frame must not invent another store.
	(void)settings;
}
void CL4DFrame::SetNavBack( CL4DFrame *caller ) { m_hNavBack = caller; }
CL4DFrame *CL4DFrame::GetNavBack() const { return m_hNavBack.Get(); }
void CL4DFrame::SetTitle( const char *title, bool surfaceTitle )
{
	BaseClass::SetTitle( title, surfaceTitle );
	if ( m_pTitle ) { m_pTitle->SetText( title ); m_pTitle->SizeToContents(); }
}
void CL4DFrame::SetTitle( const wchar_t *title, bool surfaceTitle )
{
	BaseClass::SetTitle( title, surfaceTitle );
	if ( m_pTitle ) { m_pTitle->SetText( title ); m_pTitle->SizeToContents(); }
}
bool CL4DFrame::LoadLayout( const char *resource, KeyValues *conditions )
{
	if ( resource && *resource && Q_stricmp( resource, m_Resource.String() ) )
	{
		m_Resource = resource; m_bLayoutLoaded = false;
	}
	// A selected GameSettings layout can keep the same filename while its
	// native mode/base-mode conditions change. Never discard those requests.
	if ( m_bLayoutLoaded && !conditions ) return true;
	KeyValues *data = new KeyValues( "" );
	if ( !LoadTerrorUiResource( data, m_Resource.String() ) ) { data->deleteThis(); return false; }
	LoadControlSettings( m_Resource.String(), "GAME", data, conditions );
	data->deleteThis();
	return true;
}
void PrepareBaseModFrameResource( KeyValues *data )
{
	KeyValues *chapter = data ? data->FindKey( "FlmChapterXXautogenerated" ) : NULL;
	if ( !chapter ) return;
	const char *resource = chapter->GetString( "ResourceFile", "" );
	for ( int i = 1; i <= 20; ++i )
	{
		KeyValues *copy = chapter->MakeCopy();
		const CFmtStr name( "FlmChapter%d", i );
		copy->SetName( name );
		copy->SetString( "fieldName", name );
		copy->SetString( "ResourceFile", CFmtStr( "FlmChapterXXautogenerated_%d/%s", i, resource ) );
		data->AddSubKey( copy );
	}
	data->RemoveSubKey( chapter );
	chapter->deleteThis();
}
void CL4DFrame::LoadControlSettings( const char *resource, const char *pathID, KeyValues *preloaded, KeyValues *conditions )
{
	KeyValues *data = preloaded;
	if ( !data )
	{
		data = new KeyValues( "" );
		if ( !LoadTerrorUiResource( data, resource ) ) { data->deleteThis(); return; }
	}
	if ( Q_stricmp( resource, m_Resource.String() ) ) m_Resource = resource;
	PrepareBaseModFrameResource( data );
	BaseClass::LoadControlSettings( resource, pathID ? pathID : "GAME", data, conditions );
	if ( data != preloaded ) data->deleteThis();
	m_nContentTall = GetTall();
	m_bLayoutLoaded = true;
}
void CL4DFrame::ApplySettings( KeyValues *data )
{
	BaseClass::ApplySettings( data );
	m_iHeaderY = Scale( this, data->GetInt( "header_y", 19 ) );
	m_iHeaderTall = Scale( this, data->GetInt( "header_tall", 41 ) );
	m_iTitleXOffset = Scale( this, data->GetInt( "header_title_x", 180 ) );
}
void CL4DFrame::ApplySchemeSettings( IScheme *uiScheme )
{
	BaseClass::ApplySchemeSettings( uiScheme );
	const char *font = uiScheme->GetResourceString( "FrameTitleBar.Font" );
	m_pTitle->SetFont( uiScheme->GetFont( font && *font ? font : "Default", IsProportional() ) );
	const char *inset = uiScheme->GetResourceString( "Frame.TitleTextInsetX" );
	if ( inset ) m_nTitleInsetX = atoi( inset );
	inset = uiScheme->GetResourceString( "Frame.TitleTextInsetY" );
	if ( inset ) m_nTitleInsetY = atoi( inset );
	m_nTopTexture = LoadTexture( uiScheme->GetResourceString( "Frame.TopBorderImage" ) );
	m_nBottomTexture = LoadTexture( uiScheme->GetResourceString( "Frame.BottomBorderImage" ) );
	m_SmearColor = uiScheme->GetColor( "Frame.SmearColor", Color( 0, 0, 0, 225 ) );
	m_pTitle->SetPos( m_nTitleInsetX, m_nTitleInsetY ); m_pTitle->SizeToContents();
	// G0F660/G10A40 reloads controls on each scheme application. Derived
	// screens then apply their selected resource/conditions and bind children.
	m_bLayoutLoaded = false;
	LoadLayout();
	SetPaintBorderEnabled( false ); SetTitleBarVisible( false ); SetMoveable( false ); SetSizeable( false );
	SetMenuButtonVisible( false ); SetCloseButtonVisible( false ); SetMinimizeButtonVisible( false ); SetMaximizeButtonVisible( false );
	DisableFadeEffect();
	InvalidateLayout();
}
Panel *CL4DFrame::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "L4D360HybridButton" ) ) return new L4D360HybridButton( this, "" );
	if ( !Q_stricmp( name, "GameModes" ) ) return new ::GameModes( this, "" );
	if ( !Q_stricmp( name, "L4DMenuBackground" ) ) return new ::L4DMenuBackground( this, "" );
	if ( !Q_stricmp( name, "GenericPanelList" ) ) return new GenericPanelList( this, "" );
	if ( !Q_stricmp( name, "CL4DFooterPanel" ) ) return new CL4DFooterPanel( this, "" );
	if ( !Q_stricmp( name, "CL4DWorkshopDownPanel" ) ) return new CL4DWorkshopDownPanel( this, "" );
	Panel *control = CreateBaseModFlyoutControl( this, name );
	if ( !control ) control = CreateBaseModOptionsControl( this, name );
	return control ? control : BaseClass::CreateControlByName( name );
}
void CL4DFrame::OnOpen()
{
	CL4DFrame *caller = GetNavBack();
	if ( caller && caller != this ) caller->SetVisible( false );
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( footer )
	{
		footer->SetVisible( IsFooterEnabled() );
		if ( IsFooterEnabled() ) { footer->MoveToFront(); footer->InvalidateLayout(); }
	}
	SetAlpha( 255 ); Activate();
	FlyoutMenu::CloseActiveMenu();
	if ( m_hLastFocus.Get() ) m_hLastFocus->NavigateTo(); else NavigateTo();
}
void CL4DFrame::OnClose()
{
	FlyoutMenu::CloseActiveMenu();
	BaseClass::OnClose();
}
Panel *CL4DFrame::NavigateBack()
{
	CL4DFrame *caller = GetNavBack();
	if ( caller && caller != this )
	{
		OnClose(); caller->SetVisible( true ); caller->OnOpen(); return caller;
	}
	// The client host owns the no-caller close/back route, not a fake window
	// manager or a second command dispatcher in this shared frame.
	if ( GetParent() ) GetParent()->OnCommand( "Back" );
	return NULL;
}
void CL4DFrame::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Back" ) || !Q_stricmp( command, "Cancel" ) ) { NavigateBack(); return; }
	if ( GetParent() ) GetParent()->OnCommand( command );
}
void CL4DFrame::OnKeyCodePressed( KeyCode code )
{
	const KeyCode base = GetBaseButtonCode( code );
	if ( base == KEY_XBUTTON_A )
	{
		if ( m_bOKEnabled ) EditablePanel::OnKeyCodePressed( code );
	}
	else if ( base == KEY_XBUTTON_B )
	{
		if ( m_bCancelEnabled ) { surface()->PlaySound( "UI/menu_back.wav" ); NavigateBack(); }
	}
	else if ( base != KEY_XBUTTON_START ) EditablePanel::OnKeyCodePressed( code );
	if ( code >= KEY_F1 && code <= KEY_F12 )
	{
		const char *binding = engine->Key_BindingForKey( code );
		if ( binding && *binding ) engine->ClientCmd( binding );
	}
}
void CL4DFrame::OnKeyCodeTyped( KeyCode code )
{
	if ( code == KEY_ENTER ) OnKeyCodePressed( KEY_XBUTTON_A );
	else if ( code == KEY_ESCAPE )
	{
		if ( FlyoutMenu::GetActiveMenu() ) FlyoutMenu::CloseActiveMenu(); else OnKeyCodePressed( KEY_XBUTTON_B );
	}
	BaseClass::OnKeyCodeTyped( code );
}
void CL4DFrame::OnMousePressed( MouseCode code )
{
	FlyoutMenu::CloseActiveMenu();
	BaseClass::OnMousePressed( code );
}
void CL4DFrame::NavigateTo()
{
	if ( m_hLastFocus.Get() && m_hLastFocus->IsVisible() ) m_hLastFocus->NavigateTo();
	else BaseClass::NavigateTo();
}
void CL4DFrame::OnHybridButtonNavigatedTo( const char *button )
{
	Panel *child = FindChildByName( button, true );
	if ( child ) m_hLastFocus = child;
}
void CL4DFrame::CenterDialogOnScreen()
{
	// LoadControlSettings owns the resource content height. GetTall() may
	// already include this helper's screen-height-minus-y expansion on reopen.
	int wide, tall; surface()->GetScreenSize( wide, tall );
	int footerY = 0;
	CL4DFooterPanel *footer = GetL4DFooterPanel( this );
	if ( footer && footer->HasContent() ) { int x; footer->GetPos( x, footerY ); }
	int y = ( ( footerY ? footerY : tall ) - m_nContentTall ) / 2;
	if ( footerY && y + m_nContentTall > footerY ) y = footerY - m_nContentTall;
	SetBounds( 0, y, wide, tall - y );
}
void CL4DFrame::OnScreenSizeChanged( int oldWide, int oldTall )
{
	m_bLayoutLoaded = false;
	BaseClass::OnScreenSizeChanged( oldWide, oldTall );
	InvalidateLayout( false, true );
}
int CL4DFrame::DrawSmearBackground( int x, int y, int wide, int tall, bool proportionalEdge )
{
	int top = scheme()->GetProportionalScaledValue( 21 ), bottom = top;
	if ( proportionalEdge ) { top = (int)( top * 0.75f ); bottom = (int)( bottom * 0.75f ); }
	const int middle = MAX( 0, tall - top - bottom );
	surface()->DrawSetColor( m_SmearColor );
	surface()->DrawSetTexture( m_nTopTexture );
	surface()->DrawTexturedSubRect( x, y, x + wide, y + top, 0, 0, 1, 1 );
	if ( middle ) surface()->DrawFilledRect( x, y + top, x + wide, y + top + middle );
	surface()->DrawSetTexture( m_nBottomTexture );
	surface()->DrawTexturedSubRect( x, y + top + middle, x + wide, y + top + middle + bottom, 0, 0, 1, 1 );
	return top + middle + bottom;
}
void CL4DFrame::DrawDialogBackground( const char *title, const wchar_t *wideTitle, const char *description, const wchar_t *wideDescription, int *rectOut, bool uppercase )
{
	if ( !title && !wideTitle ) return;
	IScheme *uiScheme = scheme()->GetIScheme( GetScheme() );
	if ( !uiScheme ) return;
	wchar_t titleBuffer[256], descriptionBuffer[256];
	const wchar_t *source = title ? LocalizeText( title, titleBuffer, sizeof( titleBuffer ) ) : wideTitle;
	if ( source != titleBuffer ) Q_wcsncpy( titleBuffer, source, sizeof( titleBuffer ) );
	if ( uppercase ) for ( int i = Q_wcslen( titleBuffer ) - 1; i >= 0; --i ) titleBuffer[i] = towupper( titleBuffer[i] );
	HFont titleFont = uiScheme->GetFont( "ScreenTitle", true );
	const HFont descriptionFont = uiScheme->GetFont( "DefaultMedium", true );
	int titleWide, titleTall; surface()->GetTextSize( titleFont, titleBuffer, titleWide, titleTall );
	const bool small = titleWide > GetWide() * 0.85;
	if ( small ) titleFont = uiScheme->GetFont( "FrameTitle", true );
	const wchar_t *desc = description ? LocalizeText( description, descriptionBuffer, sizeof( descriptionBuffer ) ) : wideDescription;
	int descWide = 0, descTall = 0;
	if ( desc ) surface()->GetTextSize( descriptionFont, desc, descWide, descTall );
	const int headerTall = titleTall + descTall + 2 * scheme()->GetProportionalScaledValue( 21 );
	const int x = (int)( GetWide() * ( small ? 0.05f : 0.1f ) );
	const int titleY = ( headerTall - descTall - titleTall ) / 2;
	const int painted = DrawSmearBackground( 0, 0, GetWide(), headerTall );
	DrawText( titleFont, x, titleY, Color( 200, 200, 200, 255 ), titleBuffer, Q_wcslen( titleBuffer ) );
	if ( desc ) DrawText( descriptionFont, x, titleY + titleTall, Color( 200, 200, 200, 255 ), desc, Q_wcslen( desc ) );
	DrawSmearBackground( 0, painted, GetWide(), m_nContentTall - painted );
	if ( rectOut ) { rectOut[0] = m_iHeaderY; rectOut[1] = m_iHeaderTall; rectOut[2] = painted; rectOut[3] = m_nContentTall - painted; }
}
} // namespace L4D360UI
