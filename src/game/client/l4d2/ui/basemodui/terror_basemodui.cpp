//========= Copyright Valve Corporation, All rights reserved. ============//
// Client-owned L4D360UI windows on the real target GameUI011 override.
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui.h"
#include "l4d2/ui/basemodui/terror_basemodui_controls.h"
#include "l4d2/ui/basemodui/terror_basemodui_flyout.h"
#include "l4d2/ui/basemodui/terror_basemodui_customcampaigns.h"
#include "l4d2/ui/basemodui/terror_basemodui_gamemodes.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_background.h"
#include "l4d2/ui/basemodui/terror_basemodui_addons.h"
#include "l4d2/ui/basemodui/terror_basemodui_options.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/terror_hud_resources.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/clientmode_terror.h"
#include "cdll_client_int.h"
#include "filesystem.h"
#include "eiface.h"
#include "ienginevgui.h"
#include "GameUI/IGameUI.h"
#include "inputsystem/iinputsystem.h"
#include "inputsystem/ButtonCode.h"
#include "steam/isteamcontroller.h"
#include "steam/steam_api.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/materialsystem_config.h"
#include "tier1/fmtstr.h"
#include "vgui/ILocalize.h"
#include "vgui/IScheme.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"
#include "vgui_controls/ImagePanel.h"
#include <wctype.h>
#include "tier0/memdbgon.h"

namespace L4D360UI
{
class CL4DBasePanel;
static CL4DBasePanel *s_pBasePanel;
static CSysModule *s_pGameUIModule;
static IGameUI *s_pGameUI;
static IVEngineServer *s_pEngineServer;

// G15ADB0/G676C0: the SDK keeps the Servers action, but not this command.
// Its public GameUI interface has no native optional-page message route.
CON_COMMAND( openserverbrowser, "Opens server browser" )
{
	if ( s_pGameUI )
	{
		s_pGameUI->SendMainMenuCommand( "OpenServerBrowser" );
	}
}

static bool HasDifficulty( KeyValues *mode )
{
	KeyValues *base = mode ? g_TerrorModes.FindMode( mode->GetString( "base", mode->GetName() ) ) : NULL;
	return base && mode->GetInt( "hasdifficulty", base->GetInt( "hasdifficulty", 0 ) ) != 0;
}

// G0F140/G11510, bounded by G532D0's 256-byte output buffer. Use the real
// descriptor/localization; no private mode titles or profile metadata.
static void FormatGameSettingsTitle( const char *modeName, wchar_t *text, int bytes )
{
	KeyValues *mode = g_TerrorModes.FindMode( modeName );
	const bool addon = !mode || mode->GetInt( "addon", 1 ) != 0;
	char token[128];
	Q_snprintf( token, sizeof( token ), "#L4D360UI_GameSettings_%s", addon ? "addon" : modeName );
	const wchar_t *format = g_pVGuiLocalize->Find( token );
	if ( !addon && format ) { V_wcsncpy( text, format, bytes ); return; }
	wchar_t name[64], upperTitle[128];
	g_pVGuiLocalize->ConvertANSIToUnicode( modeName, name, sizeof( name ) );
	name[0] = towupper( name[0] );
	const wchar_t *title = mode ? mode->GetWString( "DisplayTitle", name ) : name;
	V_wcsncpy( upperTitle, title, sizeof( upperTitle ) );
	for ( int i = 0; upperTitle[i]; ++i ) upperTitle[i] = towupper( upperTitle[i] );
	if ( format ) g_pVGuiLocalize->ConstructString( text, bytes, format, 1, upperTitle );
	else V_wcsncpy( text, upperTitle, bytes );
}

// G53C00 requires an offline MatchSession. The SDK keeps 18 engine slots even
// for solo play; carry the offline launch explicitly instead of maxplayers 1.
static bool ApplyOfflineSettings( KeyValues *settings )
{
	if ( Q_stricmp( settings->GetString( "System/network", "offline" ), "offline" ) ) return false;
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( settings, &mission );
	if ( !chapter ) return false;
	const char *map = chapter->GetString( "map", "" );
	if ( !*map || !s_pEngineServer ) return false;
	// Native E0E69B0 adds maps/ and .bsp to the mission's short name.
	// SDK E0FA130 opens its argument verbatim, as does the BSP21 validator.
	if ( !s_pEngineServer->IsMapValid( CFmtStr( "maps/%s.bsp", map ) ) ) return false;
	ConVar *mode = cvar->FindVar( "mp_gamemode" );
	ConVar *difficulty = cvar->FindVar( "z_difficulty" );
	ConVar *roundLimit = cvar->FindVar( "mp_roundlimit" );
	ConVar *startPlayers = cvar->FindVar( "director_min_start_players" );
	ConVar *offline = cvar->FindVar( "l4d2_offline" );
	if ( !mode || !difficulty || !roundLimit || !startPlayers || !offline ) return false;
	const char *modeName = settings->GetString( "Game/mode", "" );
	if ( *modeName ) mode->SetValue( modeName );
	const char *difficultyName = settings->GetString( "Game/difficulty", NULL );
	if ( difficultyName ) difficulty->SetValue( difficultyName );
	const int rounds = settings->GetInt( "Game/maxrounds", 0 );
	if ( rounds ) roundLimit->SetValue( rounds );
	const int players = settings->GetInt( "Members/numPlayers", -1 );
	if ( players != -1 ) startPlayers->SetValue( players );
	offline->SetValue( 1 );
	engine->ClientCmd_Unrestricted( CFmtStr( "map %s\n", map ) );
	return true;
}

static vgui::Panel *FindCommandButton( vgui::Panel *panel, const char *command )
{
	if ( ::GameModes *modes = dynamic_cast<::GameModes *>( panel ) )
	{
		if ( L4D360HybridButton *button = modes->GetButtonForCommand( command ) ) return button;
	}
	L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( panel );
	if ( button && !Q_stricmp( button->Command(), command ) ) return button;
	for ( int i = 0; i < panel->GetChildCount(); ++i )
	{
		vgui::Panel *found = FindCommandButton( panel->GetChild( i ), command );
		if ( found ) return found;
	}
	return NULL;
}

class MainMenu : public CL4DFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( MainMenu, CL4DFrame );
public:
	MainMenu( vgui::Panel *parent ) : BaseClass( parent, "MainMenu", true, true, true, false )
	{
		SetFooterEnabled( true ); SetPaintBackgroundEnabled( false );
	}
	virtual void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE
	{
		BaseClass::LoadControlSettings( resource, pathID, preloaded, conditions );
		// G69840: actual remote-storage quota gates the native Cloud row.
		FlyoutMenu *options = dynamic_cast<FlyoutMenu *>( FindChildByName( "FlmOptionsFlyout", true ) );
		if ( options )
		{
			ISteamRemoteStorage *storage = steamapicontext ? steamapicontext->SteamRemoteStorage() : NULL;
			uint64 total = 0, available = 0;
			const bool cloud = storage && storage->GetQuota( &total, &available ) && total != 0;
			options->SetControlEnabled( "BtnCloud", cloud, true );
		}
		// G67470's absent-timestamp arm. Main approved the missing blog/news
		// service, not a synthetic timestamp, blog window or news result.
		SetControlVisible( "BtnBlogPost", false, true );
	}
	virtual void OnOpen() OVERRIDE
	{
		if ( !LoadLayout( "resource/ui/l4d360ui/mainmenu.res" ) ) return;
		if ( RefreshBaseModAddonPaths() )
		{
			// G67BB0/G67650: consume the real changed bit, then refresh materials
			// and scheme; unchanged reopen does not reload either.
			if ( materials )
			{
				MaterialLock_t lock = materials->Lock();
				materials->UncacheAllMaterials();
				materials->Unlock( lock );
			}
			InvalidateLayout( false, true );
		}
		BaseClass::OnOpen(); UpdateFooter();
		if ( CheckBaseModAddonAssociation( this ) && GetParent() ) GetParent()->OnCommand( "AddonAssociation" );
	}
	virtual void OnThink() OVERRIDE
	{
		// G67850: these are actual display/input facilities, not session state.
		FlyoutMenu *options = dynamic_cast<FlyoutMenu *>( FindChildByName( "FlmOptionsFlyout", true ) );
		if ( options )
		{
			if ( materials ) options->SetControlEnabled( "BtnBrightness", !materials->GetCurrentConfigForVideoCard().Windowed(), true );
			options->SetControlEnabled( "BtnSteamController", g_pInputSystem && g_pInputSystem->GetNumSteamControllersConnected() != 0, true );
		}
		BaseClass::OnThink();
	}
	virtual vgui::Panel *CreateControlByName( const char *name ) OVERRIDE
	{
		// These discovery controls have no MatchFramework provider on this target.
		if ( !Q_stricmp( name, "QuickJoinPanel" ) || !Q_stricmp( name, "QuickJoinGroupsPanel" ) ) return NULL;
		return BaseClass::CreateControlByName( name );
	}
	virtual void OnKeyCodePressed( vgui::KeyCode code ) OVERRIDE
	{
		const vgui::KeyCode key = GetBaseButtonCode( code );
		// G67340 ignores B/Back. X needs a real ready QuickJoin provider,
		// which this host does not manufacture. Target has no inactive-start
		// producer/alias; do not reinterpret the original numeric code124.
		if ( key == KEY_XBUTTON_B || key == KEY_XBUTTON_BACK || key == KEY_XBUTTON_X ) return;
		BaseClass::OnKeyCodePressed( code );
	}
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE {} // G67320 nullsub.
	virtual void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE { UpdateFooter(); }
	virtual void OnFlyoutMenuCancelled() OVERRIDE {}
private:
	MESSAGE_FUNC_PARAMS( OnMutationModeSelected, "OnMutationModeSelected", data )
	{
		OnCommand( CFmtStr( "ChangeMutation_%s", data->GetString( "campaign", "" ) ) );
	}
	void UpdateFooter()
	{
		CL4DFooterPanel *footer = GetL4DFooterPanel( this );
		if ( !footer ) return;
		footer->SetButtons( FB_BUTTON_A | FB_BUTTON_X, 1, false );
		footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Select" );
		footer->SetButtonText( FB_BUTTON_X, "#L4D360UI_MainMenu_SeeAll" );
	}
};

class GameSettings : public CL4DFrame, public FlyoutMenuListener
{
	DECLARE_CLASS_SIMPLE( GameSettings, CL4DFrame );
public:
	GameSettings( vgui::Panel *parent ) : BaseClass( parent, "GameSettings", true, false, true, false ), m_pSettings( NULL ), m_bUpdatingSettings( false )
	{
		SetCancelButtonEnabled( true ); SetFooterEnabled( true ); SetPaintBackgroundEnabled( true );
	}
	virtual ~GameSettings() { if ( m_pSettings ) m_pSettings->deleteThis(); }
	virtual void SetDataSettings( KeyValues *settings ) OVERRIDE
	{
		if ( m_pSettings ) m_pSettings->deleteThis();
		m_pSettings = settings ? settings->MakeCopy() : NULL;
	}
	bool LoadSettingsLayout()
	{
		if ( !m_pSettings ) return false;
		KeyValues *mode = g_TerrorModes.FindMode( m_pSettings->GetString( "Game/mode", "" ) );
		return LoadLayout( UsesBaseModScriptedSoloLayout( mode ) ? "resource/ui/l4d360ui/gamesettings_scriptedsolo.res" : "resource/ui/l4d360ui/gamesettings.res" );
	}
	virtual void LoadControlSettings( const char *resource, const char *pathID = NULL, KeyValues *preloaded = NULL, KeyValues *conditions = NULL ) OVERRIDE
	{
		KeyValues::AutoDelete modeConditions( new KeyValues( "conditions" ) );
		if ( m_pSettings )
		{
			const char *name = m_pSettings->GetString( "Game/mode", "" );
			KeyValues *mode = g_TerrorModes.FindMode( name );
			modeConditions->SetInt( CFmtStr( "?condition?%s", name ), 1 );
			if ( mode ) modeConditions->SetInt( CFmtStr( "?condition?%s", mode->GetString( "base", name ) ), 1 );
			if ( mode && mode->GetInt( "mutation" ) ) modeConditions->SetInt( "?condition?challenge", 1 );
		}
		BaseClass::LoadControlSettings( resource, pathID, preloaded, conditions ? conditions : modeConditions );
		// G55A40 centers only after an actual resource reload, never on reopen.
		SetPaintBackgroundEnabled( true ); CenterDialogOnScreen();
	}
	virtual void OnOpen() OVERRIDE
	{
		if ( !LoadSettingsLayout() ) return;
		BaseClass::OnOpen(); UpdateSettingsControls(); UpdateFooter();
	}
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme ); UpdateSettingsControls();
	}
	virtual void OnCommand( const char *command ) OVERRIDE;
	virtual void OnNotifyChildFocus( vgui::Panel *child ) OVERRIDE;
	virtual void OnFlyoutMenuClosed( vgui::Panel *returnTo ) OVERRIDE { UpdateFooter(); UpdatePreview(); }
	virtual void OnFlyoutMenuCancelled() OVERRIDE;
protected:
	virtual void PaintBackground() OVERRIDE;
private:
	MESSAGE_FUNC_PARAMS( OnCustomCampaignSelected, "OnCustomCampaignSelected", data )
	{
		if ( !m_pSettings ) return;
		KeyValues *mission = g_TerrorModes.FindMission( data->GetString( "campaign", "" ) );
		const char *mode = m_pSettings->GetString( "Game/mode", "coop" );
		if ( !mission || !mission->FindKey( CFmtStr( "modes/%s/1", mode ) ) ) return;
		m_pSettings->SetString( "Game/campaign", mission->GetName() );
		m_pSettings->SetInt( "Game/chapter", MAX( 1, data->GetInt( "chapter", 0 ) ) );
		UpdateSettingsControls();
	}
	void SetDropDownValue( const char *name, const char *text, const char *command = NULL )
	{
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( name, true ) );
		if ( drop ) drop->SetCurrentSelection( command ? command : text );
	}
	void UpdateSettingsControls();
	void UpdatePreview()
	{
		KeyValues *mission = NULL;
		KeyValues *chapter = m_pSettings ? g_TerrorModes.GetChapterInfo( m_pSettings, &mission ) : NULL;
		vgui::ImagePanel *image = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "ImgLevelImage", true ) );
		if ( image ) image->SetImage( chapter ? chapter->GetString( "image", "maps/unknown" ) : "maps/unknown" );
	}
	void UpdateFooter()
	{
		CL4DFooterPanel *footer = GetL4DFooterPanel( this );
		if ( !footer ) return;
		footer->SetButtons( FB_BUTTON_A | FB_BUTTON_B, 4, false );
		footer->SetButtonText( FB_BUTTON_A, "#L4D360UI_Select" );
		footer->SetButtonText( FB_BUTTON_B, "#L4D360UI_Cancel" );
	}
	KeyValues *m_pSettings;
	vgui::DHANDLE<CustomCampaigns> m_CustomCampaigns;
	bool m_bUpdatingSettings;
};

class CL4DBasePanel : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE( CL4DBasePanel, vgui::Frame );
public:
	CL4DBasePanel() : BaseClass( NULL, "L4D2BasePanel" ), m_bInGame( false ), m_bGameUIWasVisible( enginevgui->IsGameUIVisible() ),
		m_bPauseGameUIWasVisible( enginevgui->IsGameUIVisible() ), m_flBlurScale( 0.0f ), m_flLastBlurTime( 0.0f )
	{
		SetParent( enginevgui->GetPanel( PANEL_GAMEUIDLL ) );
		SetAutoDelete( false ); SetDeleteSelfOnClose( false );
		SetTitleBarVisible( false ); SetMoveable( false ); SetSizeable( false );
		SetPaintBorderEnabled( false ); SetPaintBackgroundEnabled( true ); SetProportional( true );
		SetMouseInputEnabled( true ); SetKeyBoardInputEnabled( true ); SetCursor( vgui::dc_arrow );
		int wide, tall; vgui::surface()->GetScreenSize( wide, tall ); SetBounds( 0, 0, wide, tall );
		// G0D6F0. gameui/valve and platform/vgui remain target GameUI-owned.
		g_pVGuiLocalize->AddFile( "Resource/l4d360ui_%language%.txt", NULL, false );
		g_pVGuiLocalize->AddFile( "Resource/l4d360ui_tu_%language%.txt", NULL, false );
		SetScheme( vgui::scheme()->LoadSchemeFromFile( "resource/l4d360uischeme.res", "L4D360UIScheme" ) );
		m_pFooter = new CL4DFooterPanel( this, "FooterPanel" ); m_pFooter->LoadLayout( "resource/ui/l4d360ui/footerpanel.res" );
		m_pWorkshop = new CL4DWorkshopDownPanel( this, "WorkshopDownloadPanel" ); m_pWorkshop->LoadLayout( "resource/ui/l4d360ui/workshopdownloadpanel.res" );
		ShowRootMenu();
		vgui::ivgui()->AddTickSignal( GetVPanel(), 0 );
	}
	virtual ~CL4DBasePanel()
	{
		vgui::ivgui()->RemoveTickSignal( GetVPanel() );
		static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->SetBlurScale( 0.0f );
	}
	bool IsReady() { return m_hWindow.Get() && GetScheme() != 0; }
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		BaseClass::ApplySchemeSettings( scheme ); SetPaintBackgroundEnabled( true );
		m_Background.ApplySchemeSettings( scheme, GetWide(), GetTall() );
	}
	virtual void PaintBackground() OVERRIDE { m_Background.PaintBackground( GetWide(), GetTall() ); }
	virtual void SetVisible( bool visible ) OVERRIDE
	{
		BaseClass::SetVisible( visible ); if ( !visible ) m_Background.OnGameUIHidden();
	}
	virtual void OnThink() OVERRIDE
	{
		BaseClass::OnThink();
		int wide, tall; vgui::surface()->GetScreenSize( wide, tall );
		if ( GetWide() != wide || GetTall() != tall ) SetBounds( 0, 0, wide, tall );
		if ( ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() ) != m_bInGame ) ShowRootMenu();
		RefreshCurrentWindow();
		const bool visible = enginevgui->IsGameUIVisible();
		if ( visible && !m_bGameUIWasVisible && m_bInGame )
		{
			InGameMainMenu *menu = dynamic_cast<InGameMainMenu *>( m_hWindow.Get() );
			if ( menu ) menu->OnOpen();
		}
		if ( !visible && m_bGameUIWasVisible ) m_Background.OnGameUIHidden();
		m_bGameUIWasVisible = visible;
	}
	virtual void OnCommand( const char *command ) OVERRIDE;
protected:
	virtual void OnTick() OVERRIDE
	{
		// G872F0/G86A10 pause on GameUI activation/hide, including the console.
		// The SDK GameUI only does so for maxClients <= 1. Tick delivery keeps
		// the hide edge observable even while this panel's ancestors are hidden.
		const bool visible = enginevgui->IsGameUIVisible();
		if ( visible != m_bPauseGameUIWasVisible )
		{
			m_bPauseGameUIWasVisible = visible;
			if ( engine->IsInGame() && !engine->IsLevelMainMenuBackground() )
				engine->ServerCmd( visible ? "l4d2_gameui_pause 1\n" : "l4d2_gameui_pause 0\n" );
		}
		UpdateBlur( visible && engine->IsInGame() &&
			!engine->IsLevelMainMenuBackground() && !engine->IsDrawingLoadingImage() );
	}
private:
	void RefreshCurrentWindow()
	{
		if ( m_hWindow.Get() && m_hWindow->IsVisible() && !m_hWindow->IsMarkedForDeletion() ) return;
		m_hWindow = NULL;
		for ( int i = GetChildCount() - 1; i >= 0; --i )
		{
			CL4DFrame *frame = dynamic_cast<CL4DFrame *>( GetChild( i ) );
			if ( frame && frame != m_pFooter && frame != m_pWorkshop && frame->IsVisible() && !frame->IsMarkedForDeletion() ) { m_hWindow = frame; break; }
		}
	}
	void CloseWindows()
	{
		FlyoutMenu::CloseActiveMenu(); m_hWindow = NULL;
		for ( int i = GetChildCount() - 1; i >= 0; --i )
		{
			CL4DFrame *frame = dynamic_cast<CL4DFrame *>( GetChild( i ) );
			// G0A690 closes frames and drops weak selection; VGUI reclaims them
			// after the current input/message stack, never delete-this inline.
			if ( frame && frame != m_pFooter && frame != m_pWorkshop && !frame->IsMarkedForDeletion() ) frame->OnClose();
		}
	}
	void ShowRootMenu()
	{
		CloseWindows(); m_bInGame = engine->IsInGame() && !engine->IsLevelMainMenuBackground();
		if ( m_bInGame ) m_hWindow = CreateBaseModInGameWindow( this, "InGameMainMenu" );
		else
		{
			MainMenu *menu = new MainMenu( this );
			if ( !menu->LoadLayout( "resource/ui/l4d360ui/mainmenu.res" ) ) { delete menu; return; }
			m_hWindow = menu; menu->OnOpen();
		}
	}
	void BeginOffline( const char *modeName );
	void UpdateBlur( bool blur );
	CBasePanelBackground m_Background;
	CL4DFooterPanel *m_pFooter;
	CL4DWorkshopDownPanel *m_pWorkshop;
	vgui::DHANDLE<CL4DFrame> m_hWindow; // Root parent owns deletion; never a second registry.
	bool m_bInGame, m_bGameUIWasVisible, m_bPauseGameUIWasVisible;
	float m_flBlurScale, m_flLastBlurTime;
};

#pragma float_control( precise, on, push )
void CL4DBasePanel::UpdateBlur( bool blur )
{
	// G0D2D0 advances once per UI frame with a positive float wall-clock delta,
	// not by elapsed seconds. Tick delivery continues under hidden ancestors.
	// The SDK lacks the native GameUI -> engine -> client blur forwarding.
	const float now = (float)Plat_FloatTime();
	if ( !( now - m_flLastBlurTime > 0.0f ) )
		return;
	m_flLastBlurTime = now;

	float scale = m_flBlurScale + ( blur ? 0.05f : -0.05f );
	if ( scale > 0.85f )
	{
		scale = 0.85f;
	}
	else if ( scale < 0.0f )
	{
		scale = 0.0f;
	}
	m_flBlurScale = scale;
	static_cast< ClientModeTerrorNormal * >( GetClientModeNormal() )->SetBlurScale( scale );
}
#pragma float_control( pop )

void CL4DBasePanel::BeginOffline( const char *modeName )
{
	char resolved[64]; modeName = ResolveBaseModChallengeMode( modeName, resolved, sizeof( resolved ) );
	KeyValues *mode = g_TerrorModes.FindMode( modeName );
	if ( !mode ) return;
	KeyValues::AutoDelete settings( new KeyValues( "Settings" ) );
	settings->SetString( "System/network", "offline" ); settings->SetString( "Game/mode", mode->GetName() );
	if ( HasDifficulty( mode ) ) settings->SetString( "Game/difficulty", "normal" ); // G113B0 absent-profile default.
	settings->SetInt( "Members/numPlayers", 1 ); settings->SetInt( "Game/chapter", 1 );
	KeyValues *missions = mode->FindKey( "missions" );
	for ( KeyValues *value = missions ? missions->GetFirstValue() : NULL; value; value = value->GetNextValue() )
	{
		KeyValues *mission = static_cast<KeyValues *>( value->GetPtr() );
		if ( mission && !mission->GetInt( "dlcrequired" ) && mission->FindKey( CFmtStr( "modes/%s/1", mode->GetName() ) ) )
		{ settings->SetString( "Game/campaign", mission->GetName() ); break; }
	}
	// G0DDA0 ready: G0DE95 closes normal windows, then G0DF5C opens
	// GameSettings with NULL caller. Do not retain MainMenu or fake an event.
	CloseWindows();
	GameSettings *window = new GameSettings( this ); window->SetDataSettings( settings );
	if ( !window->LoadSettingsLayout() ) { delete window; ShowRootMenu(); return; }
	m_hWindow = window; window->OnOpen();
}

void CL4DBasePanel::OnCommand( const char *command )
{
	RefreshCurrentWindow();
	const char *value;
	if ( !Q_stricmp( command, "Back" ) )
	{
		if ( FlyoutMenu::GetActiveMenu() ) { FlyoutMenu::CloseActiveMenu(); return; }
		if ( m_hWindow.Get() && m_hWindow->GetNavBack() ) { m_hWindow->NavigateBack(); return; }
		if ( m_bInGame && dynamic_cast<InGameMainMenu *>( m_hWindow.Get() ) ) { engine->ClientCmd_Unrestricted( "gameui_hide" ); return; }
		ShowRootMenu(); return;
	}
	if ( ( value = StringAfterPrefix( command, "SoloPlay_" ) ) != NULL ) { BeginOffline( value ); return; }
	if ( !Q_stricmp( command, "SoloPlay" ) ) { BeginOffline( "coop" ); return; }
	if ( !Q_stricmp( command, "OpenServerBrowser" ) ) { s_pGameUI->SendMainMenuCommand( "OpenServerBrowser" ); return; }
	if ( !Q_stricmp( command, "Quit" ) ) { s_pGameUI->SendMainMenuCommand( "Quit" ); return; }
	if ( !Q_stricmp( command, "SteamController" ) )
	{
		ISteamController *controller = g_pInputSystem ? g_pInputSystem->SteamControllerInterface() : NULL;
		ControllerHandle_t handles[STEAM_CONTROLLER_MAX_COUNT];
		if ( controller && controller->GetConnectedControllers( handles ) > 0 ) controller->ShowBindingPanel( handles[0] );
		return;
	}
	CL4DFrame *window = IsNativeOptionsCommand( command ) ? CreateNativeOptionsWindow( this, command ) : CreateBaseModAddonsWindow( this, command );
	if ( window )
	{
		// F's factory and G's derived OnOpen admit their native resources.
		window->SetNavBack( m_hWindow.Get() ); m_hWindow = window; window->OnOpen(); return;
	}
	// I: absence of a real MatchFramework service is not browser/lobby success.
	if ( StringAfterPrefix( command, "QuickMatch_" ) || StringAfterPrefix( command, "QuickMatchServer_" ) || StringAfterPrefix( command, "CustomMatch_" ) || StringAfterPrefix( command, "FriendsMatch_" ) || StringAfterPrefix( command, "GroupServer_" ) || StringAfterPrefix( command, "InviteUI_" ) || StringAfterPrefix( command, "Leaderboards_" ) || !Q_stricmp( command, "StartLobby" ) || !Q_stricmp( command, "Create" ) || !Q_stricmp( command, "SeeAll" ) ) { ShowRootMenu(); return; }
	BaseClass::OnCommand( command );
}

void MainMenu::OnCommand( const char *command )
{
	const char *value;
	if ( ( value = StringAfterPrefix( command, "ChangeMutation_" ) ) != NULL )
	{
		SetBaseModChallengeMode( value );
		::GameModes *modes = dynamic_cast<::GameModes *>( FindChildByName( "BtnGameModes", true ) );
		if ( modes ) modes->UpdateCustomModeHint();
		OnCommand( "PlayChallenge" ); return;
	}
	if ( !Q_stricmp( command, "PlayChallenge" ) ) { OnCommand( GetBaseModChallengeFlyout() ); return; }
	if ( !Q_stricmp( command, "ChooseChallengeMode" ) ) { FlyoutMenu::CloseActiveMenu(); OpenBaseModMutationPicker( GetParent(), this ); return; }
	if ( !Q_stricmp( command, "VersusSoftLock" ) ) command = "FlmVersusFlyout";
	else if ( !Q_stricmp( command, "SurvivalCheck" ) ) command = "FlmSurvivalFlyout";
	else if ( !Q_stricmp( command, "ScavengeCheck" ) ) command = "FlmScavengeFlyout";
	else if ( !Q_stricmp( command, "FlmExtrasFlyoutCheck" ) ) command = "FlmExtrasFlyout_Simple";
	FlyoutMenu *flyout = dynamic_cast<FlyoutMenu *>( FindChildByName( command, true ) );
	if ( flyout )
	{
		flyout->OpenMenu( FindCommandButton( this, command ) ); flyout->SetFlyoutMenuListener( this ); return;
	}
	BaseClass::OnCommand( command );
}

void GameSettings::UpdateSettingsControls()
{
	if ( !m_pSettings ) return;
	m_bUpdatingSettings = true;
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( m_pSettings, &mission );
	DropDownMenu *chapters = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpChapter", true ) );
	ConfigureChapterDropDown( chapters, m_pSettings );
	const char *names[] = { "DrpMission", "DrpChapter", "DrpDifficulty" };
	for ( int i = 0; i < ARRAYSIZE( names ); ++i )
	{
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( names[i], true ) );
		if ( drop && drop->GetFlyout() ) drop->GetFlyout()->SetFlyoutMenuListener( this );
	}
	SetDropDownValue( "DrpMission", mission ? mission->GetString( "DisplayTitle", mission->GetName() ) : "#L4D360UI_Lobby_Unknown_Campaign", mission && mission->GetBool( "BuiltIn" ) ? static_cast<const char *>( CFmtStr( "cmd_campaign_%s", mission->GetName() ) ) : NULL );
	SetDropDownValue( "DrpChapter", chapter ? chapter->GetString( "DisplayName", "" ) : "", CFmtStr( "#L4D360UI_Chapter_%d", m_pSettings->GetInt( "Game/chapter", 1 ) ) );
	SetDropDownValue( "DrpDifficulty", CFmtStr( "#L4D360UI_Difficulty_%s", m_pSettings->GetString( "Game/difficulty", "normal" ) ) );
	KeyValues *mode = g_TerrorModes.FindMode( m_pSettings->GetString( "Game/mode", "" ) );
	SetControlEnabled( "DrpDifficulty", HasDifficulty( mode ), true ); SetControlEnabled( "BtnStartGame", chapter != NULL, true );
	SetControlVisible( "DrpCharacter", false, true ); // No invented profile/avatar persistence.
	UpdatePreview(); m_bUpdatingSettings = false;
}
void GameSettings::OnNotifyChildFocus( vgui::Panel *child )
{
	if ( !m_pSettings ) return;
	vgui::Button *button = dynamic_cast<vgui::Button *>( child );
	KeyValues *data = button ? button->GetCommand() : NULL;
	if ( !data ) return;
	const char *command = data->GetString( "command", "" );
	const char *chapterText = StringAfterPrefix( command, "#L4D360UI_Chapter_" );
	const char *campaign = StringAfterPrefix( command, "cmd_campaign_" );
	const bool custom = !Q_stricmp( command, "cmd_addoncampaign" );
	if ( !chapterText && !campaign && !custom ) return;
	KeyValues *mission = g_TerrorModes.FindMission( campaign ? campaign : m_pSettings->GetString( "Game/campaign", "" ) );
	const char *mode = m_pSettings->GetString( "Game/mode", "coop" );
	const int number = chapterText ? atoi( chapterText ) : 0;
	KeyValues *chapter = mission && number > 0 ? mission->FindKey( CFmtStr( "modes/%s/%d", mode, number ) ) : NULL;
	vgui::ImagePanel *image = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "ImgLevelImage", true ) );
	if ( image ) image->SetImage( custom ? "maps/unknown" : chapter ? chapter->GetString( "image", "maps/any" ) : mission ? mission->GetString( "image", "maps/any" ) : "maps/any" );
}
void GameSettings::OnFlyoutMenuCancelled()
{
	const char *names[] = { "DrpCharacter", "DrpServerType" };
	for ( int i = 0; i < ARRAYSIZE( names ); ++i )
	{
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( names[i], true ) );
		if ( drop && drop->GetCurrentSelection() && drop->GetFlyout() ) OnNotifyChildFocus( drop->GetFlyout()->FindChildButtonByCommand( drop->GetCurrentSelection() ) );
	}
}
void GameSettings::PaintBackground()
{
	if ( !m_pSettings ) return;
	// G532D0: the admitted host constructs only the actual offline settings arm.
	const char *mode = m_pSettings->GetString( "Game/mode", "coop" );
	const char *description = "#L4D360UI_GameSettings_Description";
	if ( Q_stricmp( mode, "coop" ) )
	{
		wchar_t title[128];
		FormatGameSettingsTitle( mode, title, sizeof( title ) );
		DrawDialogBackground( NULL, title, description );
	}
	else
	{
		const bool commentary = !Q_stricmp( m_pSettings->GetString( "Options/play", "" ), "commentary" );
		const char *title = commentary ? "#L4D360UI_GameSettings_Commentary" :
			m_pSettings->GetInt( "Members/numPlayers", 0 ) <= 1 ? "#L4D360UI_GameSettings_Solo" : "#L4D360UI_GameSettings_OfflineCoop";
		DrawDialogBackground( title, NULL, description );
	}
}
void GameSettings::OnCommand( const char *command )
{
	if ( m_bUpdatingSettings || !m_pSettings ) return;
	const char *value;
	if ( StringAfterPrefix( command, "SwitchGameModeTo_" ) )
	{
		// G56080 -> G54BD0 exits at G54C1B/G54C23 unless the real caller is
		// GameLobby9. That absent-service window is never synthesized here.
		return;
	}
	if ( !Q_stricmp( command, "cmd_addoncampaign" ) )
	{
		if ( !m_CustomCampaigns.Get() ) m_CustomCampaigns = OpenCustomCampaigns( GetParent(), this, m_pSettings, this );
		return;
	}
	if ( ( value = StringAfterPrefix( command, "cmd_campaign_" ) ) != NULL )
	{
		if ( g_TerrorModes.FindMission( value ) ) { m_pSettings->SetString( "Game/campaign", value ); m_pSettings->SetInt( "Game/chapter", 1 ); UpdateSettingsControls(); } return;
	}
	if ( ( value = StringAfterPrefix( command, "#L4D360UI_Chapter_" ) ) != NULL ) { m_pSettings->SetInt( "Game/chapter", atoi( value ) ); UpdateSettingsControls(); return; }
	if ( ( value = StringAfterPrefix( command, "#L4D360UI_Difficulty_" ) ) != NULL ) { m_pSettings->SetString( "Game/difficulty", value ); UpdateSettingsControls(); return; }
	const char *name = !Q_stricmp( command, "FlmChapter" ) ? "DrpChapter" : !Q_stricmp( command, "FlmDifficulty" ) ? "DrpDifficulty" : !Q_stricmp( command, "FlmMission" ) || !Q_stricmp( command, "FlmMissionSurvival" ) || !Q_stricmp( command, "FlmMissionExtended" ) ? "DrpMission" : NULL;
	if ( name )
	{
		DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( name, true ) );
		if ( drop ) { if ( !Q_stricmp( name, "DrpChapter" ) ) ConfigureChapterDropDown( drop, m_pSettings ); drop->OnCommand( NULL ); }
		return;
	}
	if ( !Q_stricmp( command, "Done" ) ) { if ( ApplyOfflineSettings( m_pSettings ) ) engine->ClientCmd_Unrestricted( "gameui_hide" ); return; }
	BaseClass::OnCommand( command );
}
} // namespace L4D360UI

bool InitTerrorBaseModUI()
{
	using namespace L4D360UI;
	if ( s_pBasePanel ) return true;
	if ( !enginevgui->GetPanel( PANEL_GAMEUIDLL ) ) return false;
	CreateInterfaceFn engineFactory = Sys_GetFactory( "engine.dll" );
	s_pEngineServer = engineFactory ? static_cast<IVEngineServer *>( engineFactory( INTERFACEVERSION_VENGINESERVER, NULL ) ) : NULL;
	if ( !s_pEngineServer ) return false;
	s_pGameUIModule = Sys_LoadModule( "gameui" );
	CreateInterfaceFn factory = s_pGameUIModule ? Sys_GetFactory( s_pGameUIModule ) : NULL;
	s_pGameUI = factory ? static_cast<IGameUI *>( factory( GAMEUI_INTERFACE_VERSION, NULL ) ) : NULL;
	if ( !s_pGameUI ) { if ( s_pGameUIModule ) Sys_UnloadModule( s_pGameUIModule ); s_pGameUIModule = NULL; s_pEngineServer = NULL; return false; }
	SetNativeOptionsGameUI( s_pGameUI );
	s_pBasePanel = new CL4DBasePanel;
	if ( !s_pBasePanel->IsReady() ) { ShutdownTerrorBaseModUI(); return false; }
	s_pGameUI->SetMainMenuOverride( s_pBasePanel->GetVPanel() ); return true;
}
void ShutdownTerrorBaseModUI()
{
	using namespace L4D360UI;
	if ( s_pGameUI ) s_pGameUI->SetMainMenuOverride( 0 );
	delete s_pBasePanel; s_pBasePanel = NULL;
	SetNativeOptionsGameUI( NULL ); s_pGameUI = NULL; s_pEngineServer = NULL;
	if ( s_pGameUIModule ) Sys_UnloadModule( s_pGameUIModule );
	s_pGameUIModule = NULL;
}
