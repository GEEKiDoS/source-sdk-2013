//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/cs_textwindow.h"
#include "l4d2/ui/terror_legacy_controls.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "steam/steam_api.h"
#include "vgui_controls/TextEntry.h"
#include "tier0/memdbgon.h"

static bool s_bTerrorTextWindowInputActive;
bool IsTerrorTextWindowInputActive() { return s_bTerrorTextWindowInputActive; }

CCSTextWindow::CCSTextWindow( IViewPort *viewport ) : BaseClass( viewport ),
	m_bHostfilePanel( false ), m_nFadePhase( 0 ), m_flFadeDeadline( 0.0f )
{
	m_szTitle[0] = m_szMessage[0] = 0; // Native1EA890 leaves the initial data empty.
	SetProportional( true ); // Native232F80.
}
void CCSTextWindow::SetData( KeyValues *data )
{
	if ( vgui::Panel::IsVisible() ) return; // Native1EA380 does not replace an open panel's data.
	// Preserve the existing target command whitelist and HTML backend policy.
	int command = data->GetInt( "cmd" );
	if ( !Q_strcmp( data->GetString( "cmd" ), "joingame" ) ) command = TEXTWINDOW_CMD_JOINGAME;
	SetData( data->GetInt( "type" ), data->GetString( "title" ), data->GetString( "msg" ),
		data->GetString( "msg_fallback" ), command, data->GetBool( "unload" ) );
}
void CCSTextWindow::SetData( int type, const char *title, const char *message, const char *messageFallback, int command, bool unload )
{
	Q_strncpy( m_szTitle, title, sizeof( m_szTitle ) );
	Q_strncpy( m_szMessage, message, sizeof( m_szMessage ) );
	Q_strncpy( m_szMessageFallback, messageFallback, sizeof( m_szMessageFallback ) );
	m_nExitCommand = command; m_nContentType = type; m_bUnloadOnDismissal = unload;
	RefreshContents();
}
void CCSTextWindow::Reset()
{
	Q_strcpy( m_szTitle, "Error loading info message." ); m_szMessage[0] = 0;
	m_nExitCommand = TEXTWINDOW_CMD_NONE;
	if ( m_bHostfilePanel ) { m_nFadePhase = 3; SetAlpha( 0 ); }
	else SetAlpha( 255 );
	RefreshContents();
}
void CCSTextWindow::ApplySchemeSettings( vgui::IScheme *scheme )
{
	vgui::Frame::ApplySchemeSettings( scheme );
	LoadControlSettings( m_bHostfilePanel ? "Resource/UI/MiniMOTD.res" : "Resource/UI/TextWindow.res" );
}
void CCSTextWindow::RefreshContents()
{
	if ( !m_bHostfilePanel ) CTextWindow::Update();
	else
	{
		// The mini panel presents its embedded ThirdPartyServerPanel instead.
		SetTitle( m_szTitle, false ); m_pTitleLabel->SetText( m_szTitle );
		m_pHTMLMessage->SetVisible( false ); m_pTextMessage->SetVisible( false );
	}
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CCSTextWindow::AdvanceFade()
{
	if ( !vgui::Panel::IsVisible() || !m_bHostfilePanel ) return;
	const float remaining = (float)( (double)m_flFadeDeadline - (double)gpGlobals->curtime );
	if ( m_nFadePhase == 0 )
	{
		float fraction = ( remaining - 2.5f ) / -2.5f;
		if ( fraction < 0.0f ) fraction = 0.0f;
		else if ( fraction > 1.0f ) fraction = 1.0f;
		const double alpha = -128.0 + 383.0 * (double)fraction;
		const float alphaSnapshot = alpha < 0.0 ? 0.0f : (float)alpha;
		SetAlpha( TerrorHudCvttFloat( alphaSnapshot ) );
		if ( remaining <= 0.0f ) { m_nFadePhase = 1; m_flFadeDeadline = gpGlobals->curtime + 5.0f; }
	}
	else if ( m_nFadePhase == 1 )
	{
		if ( remaining <= 0.0f ) { m_nFadePhase = 2; m_flFadeDeadline = gpGlobals->curtime + 2.5f; }
	}
	else if ( m_nFadePhase == 2 )
	{
		float fraction = remaining / 2.5f;
		if ( fraction < 0.0f ) fraction = 0.0f;
		else if ( fraction > 1.0f ) fraction = 1.0f;
		SetAlpha( TerrorHudFtol( 255.0 * (double)fraction ) );
		if ( remaining <= 0.0f ) { m_nFadePhase = 3; SetVisible( false ); }
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
void CCSTextWindow::Update()
{
	AdvanceFade();
	m_pOK->RequestFocus(); // Native232EA0 calls Panel+48 RequestFocus, not SetVisible.
}
void CCSTextWindow::SetVisible( bool state )
{
	vgui::Panel::SetVisible( state );
	if ( state ) m_pOK->RequestFocus(); // Native232EC0.
}
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
void CCSTextWindow::ShowPanel( bool state )
{
	if ( vgui::Panel::IsVisible() == state ) return;
	m_pViewPort->ShowBackGround( state );
	m_bHostfilePanel = m_szMessage[0] && !Q_strncmp( m_szMessage, "hostfile", 8 );
	s_bTerrorTextWindowInputActive = state && !m_bHostfilePanel;
	if ( state )
	{
		InvalidateLayout( true, true ); RefreshContents(); Activate();
		if ( m_bHostfilePanel )
		{
			m_nFadePhase = 0; m_flFadeDeadline = gpGlobals->curtime + 2.5f;
			SetAlpha( 0 ); SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false );
		}
		else { SetVisible( true ); SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetAlpha( 255 ); }
	}
	else
	{
		SetVisible( false );
		if ( !m_bHostfilePanel ) SetMouseInputEnabled( false );
		// Keep the target HTML backend's existing safe unload-on-dismissal contract.
		if ( m_bUnloadOnDismissal && m_bShownURL && m_pHTMLMessage )
		{
			m_pHTMLMessage->OpenURL( "about:blank", NULL ); m_bShownURL = false;
		}
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif

CTerrorTextWindow::CTerrorTextWindow( IViewPort *viewport ) : BaseClass( viewport ), m_pTerrorOK( NULL )
{
	m_pTerrorText = new CTerrorRichText( this, "TerrorTextMessage" );
	// Original skins its private Chrome scrollbars here. The approved target
	// SteamHTMLSurface backend exposes no equivalent scrollbar-image API.
}
void CTerrorTextWindow::ApplySchemeSettings( vgui::IScheme *scheme )
{
	m_pTerrorOK = NULL; BaseClass::ApplySchemeSettings( scheme );
}
void CTerrorTextWindow::ShowText( const char *text )
{
	if ( m_pTerrorText ) { m_pTerrorText->SetVisible( true ); m_pTerrorText->SetText( text ); m_pTerrorText->GotoTextStart(); }
}
void CTerrorTextWindow::RefreshContents()
{
	if ( m_pTerrorText ) m_pTerrorText->SetVisible( false );
	KeybindLabel *binding = dynamic_cast< KeybindLabel * >( FindChildByName( "NewsKeyBind", true ) );
	if ( binding ) binding->SetText( L"#L4D_motd_key_string", "motd" );
	m_pTerrorOK = dynamic_cast< L4DButton * >( FindChildByName( "L4Dok" ) );
	if ( m_pTerrorOK ) m_pTerrorOK->RequestFocus();
	L4DButton *group = dynamic_cast< L4DButton * >( FindChildByName( "JoinSteamGroup" ) );
	if ( group )
	{
		CTerrorGameRules *rules = TerrorGameRules();
		if ( rules ) group->SetVisible( rules->GetNativeServerSteamGroupID() > 0 );
		group->SetMouseInputEnabled( true );
	}
	BaseClass::RefreshContents();
}
void CTerrorTextWindow::Update()
{
	BaseClass::Update();
	if ( vgui::Panel::IsVisible() && !IsHostfilePanel() && m_pTerrorOK && !m_pTerrorOK->HasFocus() )
	{
		m_pTerrorOK->SetMouseInputEnabled( true ); m_pTerrorOK->RequestFocus();
	}
}
void CTerrorTextWindow::OnCommand( const char *command )
{
	if ( Q_strcmp( command, "joinsteamgroup" ) ) { BaseClass::OnCommand( command ); return; }
	CTerrorGameRules *rules = TerrorGameRules();
	ISteamUtils *utils = steamapicontext ? steamapicontext->SteamUtils() : NULL;
	ISteamFriends *friends = steamapicontext ? steamapicontext->SteamFriends() : NULL;
	if ( !rules || !utils || !friends ) return;
	const CSteamID group( (uint32)rules->GetNativeServerSteamGroupID(), utils->GetConnectedUniverse(), k_EAccountTypeClan );
	char dialog[64]; Q_snprintf( dialog, sizeof( dialog ), "steamid/%llu", group.ConvertToUint64() );
	friends->ActivateGameOverlay( dialog );
}
