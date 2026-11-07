//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "inputsystem/iinputsystem.h"
#include "vgui/ILocalize.h"
#include "vgui/IPanel.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#include "vgui_controls/ProgressBar.h"
#include "vgui_controls/Label.h"
#include "tier0/platform.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
DECLARE_BUILD_FACTORY( CL4DFooterPanel );
DECLARE_BUILD_FACTORY( CL4DWorkshopDownPanel );

// Native G1F8918. Layouts 13..15 are the three caller-supplied layouts;
// ***footerbuttonslayout supplies the mask followed by the A/B/X/Y triples.
static const FooterButtonsLayout_t s_FooterLayouts[16] =
{
	{ { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, 0 },
	{ { {0,100,50}, {0,0,0}, {1,240,50}, {0,0,0}, {0,0,0} }, 5 },
	{ { {0,100,50}, {0,225,50}, {0,0,0}, {0,0,0}, {0,0,0} }, 3 },
	{ { {0,100,50}, {0,225,50}, {0,0,0}, {0,0,0}, {0,0,0} }, 3 },
	{ { {0,100,50}, {0,225,50}, {0,0,0}, {0,0,0}, {0,0,0} }, 3 },
	{ { {0,100,50}, {0,225,50}, {1,240,50}, {0,0,0}, {0,0,0} }, 7 },
	{ { {0,100,50}, {0,225,50}, {0,0,0}, {1,240,50}, {0,0,0} }, 11 },
	{ { {0,225,50}, {0,100,50}, {0,0,0}, {1,240,50}, {0,0,0} }, 11 },
	{ { {0,225,50}, {0,100,50}, {1,240,50}, {0,0,0}, {0,0,0} }, 7 },
	{ { {0,225,50}, {0,100,50}, {1,240,30}, {1,240,50}, {0,0,0} }, 15 },
	{ { {0,0,0}, {0,100,50}, {0,0,0}, {0,0,0}, {0,0,0} }, 2 },
	{ { {0,100,60}, {0,0,0}, {0,0,0}, {1,240,60}, {0,0,0} }, 9 },
	{ { {0,0,0}, {0,100,50}, {0,0,0}, {0,225,50}, {0,0,0} }, 10 },
	{ { {0,100,50}, {0,225,50}, {1,240,30}, {1,240,50}, {0,0,0} }, 15 },
	{ { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, 0 },
	{ { {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0}, {0,0,0} }, 0 }
};

static bool FooterSteamControllerActive()
{
	return g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
}

CL4DFooterPanel::CL4DFooterPanel( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, false, false, false, false ),
	m_nButtons( 0 ), m_nLayout( 0 ), m_hHelpFont( 0 ), m_hButtonFont( 0 ),
	m_hButtonTextFont( 0 ), m_bFontsInitialized( false ), m_flHelpFadeStart( 0.0f )
{
	// G430C0: the base panel, not the frame close path, owns this child.
	vgui::ipanel()->SetTopmostPopup( GetVPanel(), true );
	SetProportional( true );
	SetTitle( "", false );
	SetFooterEnabled( false );
	Q_memset( m_ButtonText, 0, sizeof( m_ButtonText ) );
	Q_memset( m_HelpText, 0, sizeof( m_HelpText ) );
	Q_memset( m_HelpTextUnicode, 0, sizeof( m_HelpTextUnicode ) );
	Q_memcpy( m_CustomLayouts, s_FooterLayouts + 13, sizeof( m_CustomLayouts ) );
}

void CL4DFooterPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( true );
	SetBgColor( Color( 0, 0, 0, 200 ) );
	m_hHelpFont = scheme->GetFont( "DefaultMedium", true );
	m_hButtonTextFont = scheme->GetFont( "DefaultBold", true );
	m_hButtonFont = scheme->GetFont( "GameUIButtonsTiny", true );
	m_bFontsInitialized = true;
}

int CL4DFooterPanel::ButtonIndex( int button ) const
{
	switch ( button )
	{
	case FB_BUTTON_A: return 0;
	case FB_BUTTON_B: return 1;
	case FB_BUTTON_X: return 2;
	case FB_BUTTON_Y: return 3;
	case FB_DPAD: return 4;
	default: return -1;
	}
}

void CL4DFooterPanel::SetButtonText( int button, const char *text )
{
	const int index = ButtonIndex( button );
	if ( index < 0 ) return;
	if ( !text ) text = "";
	const wchar_t *localized = g_pVGuiLocalize->Find( text );
	if ( localized ) V_wcsncpy( m_ButtonText[index], localized, sizeof( m_ButtonText[index] ) );
	else g_pVGuiLocalize->ConvertANSIToUnicode( text, m_ButtonText[index], sizeof( m_ButtonText[index] ) );
}

void CL4DFooterPanel::SetButtonText( int button, const wchar_t *text )
{
	const int index = ButtonIndex( button );
	if ( index >= 0 ) V_wcsncpy( m_ButtonText[index], text ? text : L"", sizeof( m_ButtonText[index] ) );
}

void CL4DFooterPanel::SetButtons( int buttons, int layout, bool help, const FooterButtonsLayout_t *custom )
{
	Assert( layout >= 0 && layout < ARRAYSIZE( s_FooterLayouts ) );
	if ( layout < 0 || layout >= ARRAYSIZE( s_FooterLayouts ) ) return;
	vgui::Label *label = dynamic_cast<vgui::Label *>( FindChildByName( "LblHelpText", false ) );
	if ( label ) label->SetVisible( help );
	if ( custom && layout >= 13 ) m_CustomLayouts[layout - 13] = *custom;
	m_nLayout = layout;
	const FooterButtonsLayout_t &positions = layout >= 13 ? m_CustomLayouts[layout - 13] : s_FooterLayouts[layout];
	m_nButtons = buttons & positions.allowedButtons;
}

bool CL4DFooterPanel::IsHelpTextEnabled()
{
	vgui::Panel *label = FindChildByName( "LblHelpText", false );
	return label && label->IsVisible();
}

void CL4DFooterPanel::SetHelpText( const char *text )
{
	// G42AE0 deliberately ignores NULL; an empty string clears the help.
	if ( text )
	{
		Q_strncpy( m_HelpText, text, sizeof( m_HelpText ) );
		m_flHelpFadeStart = 0.0f;
	}
}

void CL4DFooterPanel::FadeOutHelpText()
{
	m_flHelpFadeStart = Plat_FloatTime();
}

void CL4DFooterPanel::SetCloudEnabled( bool enabled )
{
	SetControlVisible( "ImageCloud", enabled );
	SetControlVisible( "UsesCloudLabel", enabled );
}

bool CL4DFooterPanel::HasContent()
{
	if ( IsHelpTextEnabled() || ( FooterSteamControllerActive() && m_nButtons ) ) return true;
	vgui::Panel *cloud = FindChildByName( "UsesCloudLabel", false );
	return cloud && cloud->IsVisible();
}

void CL4DFooterPanel::PaintHelpText()
{
	if ( !m_bFontsInitialized ) return;
	if ( !m_hHelpFont )
	{
		DevWarning( "UI Footer: Help fonts not initialized!\n" );
		return;
	}
	vgui::Label *label = dynamic_cast<vgui::Label *>( FindChildByName( "LblHelpText", false ) );
	if ( !label ) return;
	// Preserve G42640's update order (the Label consumes the previous buffer).
	label->SetText( m_HelpTextUnicode );
	const wchar_t *localized = g_pVGuiLocalize->Find( m_HelpText );
	if ( localized ) V_wcsncpy( m_HelpTextUnicode, localized, sizeof( m_HelpTextUnicode ) );
	else g_pVGuiLocalize->ConvertANSIToUnicode( m_HelpText, m_HelpTextUnicode, sizeof( m_HelpTextUnicode ) );
	int textWide, textTall;
	vgui::surface()->GetTextSize( m_hHelpFont, m_HelpTextUnicode, textWide, textTall );
	label->SetPos( ( GetWide() - MIN( label->GetWide(), textWide ) ) / 2,
		vgui::scheme()->GetProportionalScaledValue( FooterSteamControllerActive() ? 5 : 15 ) );
	int alpha = 255;
	if ( m_flHelpFadeStart != 0.0f ) alpha = MAX( 0, (int)( 255.0 - ( Plat_FloatTime() - m_flHelpFadeStart ) * 255.0 ) );
	label->SetAlpha( alpha );
}

void CL4DFooterPanel::PaintButton( int index, const char *icon )
{
	if ( !m_bFontsInitialized ) return;
	if ( !m_hButtonFont || !m_hButtonTextFont )
	{
		DevWarning( "UI Footer: Button fonts not initialized!\n" );
		return;
	}
	wchar_t iconBuffer[32];
	const wchar_t *iconText = g_pVGuiLocalize->Find( icon );
	if ( !iconText )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( icon, iconBuffer, sizeof( iconBuffer ) );
		iconText = iconBuffer;
	}
	const FooterButtonsLayout_t &layout = m_nLayout >= 13 ? m_CustomLayouts[m_nLayout - 13] : s_FooterLayouts[m_nLayout];
	const int *position = layout.position[index];
	int screenWide, screenTall;
	vgui::surface()->GetScreenSize( screenWide, screenTall );
	int x = vgui::scheme()->GetProportionalScaledValue( position[1] );
	const int y = vgui::scheme()->GetProportionalScaledValue( position[2] - 14 );
	bool rightAligned = false;
	switch ( position[0] )
	{
	case 1: x = screenWide - x; rightAligned = true; break;
	case 2: x = screenWide / 2 - x; break;
	case 3: x += screenWide / 2; rightAligned = true; break;
	}
	int iconWide, iconTall, textWide, textTall;
	vgui::surface()->GetTextSize( m_hButtonFont, iconText, iconWide, iconTall );
	vgui::surface()->GetTextSize( m_hButtonTextFont, m_ButtonText[index], textWide, textTall );
	const int rightEdge = (int)( (double)screenWide * 0.95 );
	if ( rightAligned && x + textWide > rightEdge ) x = rightEdge - textWide;
	// Main-approved existing locator/bindpanel Steam-glyph/PNG-decode gap:
	// G43960's controller glyph fetch/decode has no decoded texture. Preserve
	// that failed-decode result, not font art, fabricated pixels or a new
	// key-to-origin mapping. The native ordinary-font arm stays separate.
	if ( !FooterSteamControllerActive() )
	{
		vgui::surface()->DrawSetTextFont( m_hButtonFont );
		vgui::surface()->DrawSetTextPos( x - iconWide - 5, y - iconTall / 2 );
		vgui::surface()->DrawSetTextColor( 255, 255, 255, 255 );
		vgui::surface()->DrawPrintText( iconText, V_wcslen( iconText ) );
	}
	vgui::surface()->DrawSetTextFont( m_hButtonTextFont );
	vgui::surface()->DrawSetTextPos( x, y - textTall / 2 );
	vgui::surface()->DrawSetTextColor( 255, 255, 255, 255 );
	vgui::surface()->DrawPrintText( m_ButtonText[index], V_wcslen( m_ButtonText[index] ) );
}

void CL4DFooterPanel::PaintBackground()
{
	// G43F20 is the native PaintBackground override, not the foreground Paint.
	if ( !HasContent() ) return;
	int y = 0, tall = GetTall();
	const bool controller = FooterSteamControllerActive();
	if ( controller )
	{
		if ( m_nLayout == 13 )
		{
			y = (int)( (float)tall * -0.5f );
			tall = (int)( (float)tall * 1.5f );
		}
		else if ( !IsHelpTextEnabled() )
		{
			y = (int)( (float)tall * 0.14999998f );
			tall = (int)( (float)tall * 0.85000002f );
		}
	}
	DrawSmearBackground( 0, y, GetWide(), tall, true );
	if ( controller )
	{
		static const char *const icons[5] = { "#GameUI_Icons_A_3DButton", "#GameUI_Icons_B_3DButton", "#GameUI_Icons_X_3DButton", "#GameUI_Icons_Y_3DButton", "#GameUI_Icons_CENTER_DPAD" };
		for ( int i = 0; i < ARRAYSIZE( icons ); ++i ) if ( m_nButtons & ( 1 << i ) ) PaintButton( i, icons[i] );
	}
	PaintHelpText();
}

CL4DWorkshopDownPanel::CL4DWorkshopDownPanel( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, false, false, false, false ),
	m_pDownloadLabel( NULL ), m_pDownloadFileLabel( NULL )
{
	vgui::ipanel()->SetTopmostPopup( GetVPanel(), true );
	SetProportional( true );
	SetTitle( "", false );
	SetFooterEnabled( false );
	m_pDownloadProgress = new vgui::ContinuousProgressBar( this, "DownloadProgress" );
	m_pDownloadProgress->SetProgress( 0.0f );
	SetPaintBackgroundEnabled( true );
	SetZPos( 0x40000000 );
	vgui::ipanel()->MoveToBack( GetVPanel() );
	SetVisible( false );
}

void CL4DWorkshopDownPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	m_pDownloadLabel = dynamic_cast<vgui::Label *>( FindChildByName( "DownloadLabel", false ) );
	m_pDownloadFileLabel = dynamic_cast<vgui::Label *>( FindChildByName( "DownloadFileLabel", false ) );
}

void CL4DWorkshopDownPanel::OnThink()
{
	BaseClass::OnThink();
	// G42C90 -> G0AB70: no current UGC download returns false before the
	// progress/filename/ConstructString calls. VWorkshop001 is absent here.
	// Do not manufacture counts, completion, filenames or Steam callbacks.
	SetVisible( false );
}

CL4DFooterPanel *GetL4DFooterPanel( vgui::Panel *descendant )
{
	for ( vgui::Panel *panel = descendant; panel; panel = panel->GetParent() )
	{
		CL4DFooterPanel *footer = dynamic_cast<CL4DFooterPanel *>( panel );
		if ( !footer ) footer = dynamic_cast<CL4DFooterPanel *>( panel->FindChildByName( "FooterPanel", false ) );
		if ( footer ) return footer;
	}
	return NULL;
}

}
