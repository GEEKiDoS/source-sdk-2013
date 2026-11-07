//========= Copyright Valve Corporation, All rights reserved. ============//
// Native GameUI presentation hosted by the client. MatchFramework is absent.
#include "cbase.h"
#include "l4d2/ui/basemodui/terror_basemodui_ingame.h"
#include "l4d2/ui/basemodui/terror_basemodui_footer.h"
#include "l4d2/ui/basemodui/terror_basemodui_background.h"
#include "l4d2/gamerules/terror_client_game_details.h"
#include "l4d2/gamerules/terror_mode.h"
#include "c_playerresource.h"
#include "tier1/fmtstr.h"
#include "tier3/tier3.h"
#include "inputsystem/IInputSystem.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/materialsystem_config.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

namespace L4D360UI
{
namespace
{
void SetFooter( vgui::Panel *panel, int buttons, bool help, const char *accept, const char *cancel, int layout = 4 )
{
	CL4DFooterPanel *footer = GetL4DFooterPanel( panel );
	if ( !footer ) return;
	footer->SetButtons( buttons, layout, help );
	if ( accept ) footer->SetButtonText( FB_BUTTON_A, accept );
	if ( cancel ) footer->SetButtonText( FB_BUTTON_B, cancel );
}

// C0AC290: -1 means the actual local entity, otherwise resolve a USERID.
bool GetPlayerTeam( int userID, int &team )
{
	C_BasePlayer *player = NULL;
	if ( userID == -1 ) player = C_BasePlayer::GetLocalPlayer();
	else
	{
		for ( int index = 1; index <= gpGlobals->maxClients; ++index )
		{
			C_BasePlayer *candidate = UTIL_PlayerByIndex( index );
			if ( candidate && candidate->GetUserID() == userID ) { player = candidate; break; }
		}
	}
	if ( !player ) return false;
	team = player->GetTeamNumber();
	return true;
}

// C0AC630: player entity, actual team, non-dormant, non-bot. Not alive-only.
int CountTeamPlayers( int team )
{
	int count = 0;
	for ( int index = 1; index <= gpGlobals->maxClients; ++index )
	{
		C_BasePlayer *player = UTIL_PlayerByIndex( index );
		if ( player && player->IsPlayer() && player->GetTeamNumber() == team && !player->IsDormant() && !player->IsBot() ) ++count;
	}
	return count;
}

// C0AC3B0/C063860: exclude bots, self, other real local split-screen
// entities and player-resource fake players; spectators share survivor side.
bool CanKickPlayer( int index )
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	C_BasePlayer *player = UTIL_PlayerByIndex( index );
	if ( !local || !player || player->IsBot() || player == local || !g_PR || g_PR->IsFakePlayer( index ) ) return false;
	for ( int slot = 0; slot < C_BasePlayer::MAX_LOCAL_PLAYER_SLOTS; ++slot )
		if ( C_BasePlayer::GetLocalPlayer( slot ) == player ) return false;
	const int team = local->GetTeamNumber(), otherTeam = player->GetTeamNumber();
	return ( team > 0 && team <= 2 && ( otherTeam == 1 || otherTeam == 2 ) ) || ( team == 3 && otherTeam == 3 );
}

bool IsSingleChapter( const char *name )
{
	KeyValues *mode = g_TerrorModes.FindMode( name );
	KeyValues *base = mode ? g_TerrorModes.FindMode( mode->GetString( "base", name ) ) : NULL;
	return mode && mode->GetInt( "singlechapter", base ? base->GetInt( "singlechapter", 0 ) != 0 : 0 ) != 0;
}

void CloseSelector( CL4DFrame *frame )
{
	FlyoutMenu::CloseActiveMenu();
	engine->ClientCmd_Unrestricted( "gameui_hide" );
	frame->CL4DFrame::NavigateBack();
}

vgui::Panel *CreateInGameControl( vgui::Panel *parent, const char *name )
{
	vgui::Panel *control = CreateBaseModFlyoutControl( parent, name );
	if ( control ) return control;
	return !Q_stricmp( name, "L4DMenuBackground" ) ? new ::L4DMenuBackground( parent, "" ) : NULL;
}

vgui::Button *FindCommand( vgui::Panel *parent, const char *command )
{
	L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( parent );
	if ( button && !Q_stricmp( button->Command(), command ) ) return button;
	for ( int i = 0; i < parent->GetChildCount(); ++i )
	{
		vgui::Button *found = FindCommand( parent->GetChild( i ), command );
		if ( found ) return found;
	}
	return NULL;
}

const char *DemoUISuffix()
{
	ConVarRef demo( "demo_ui_enable", true );
	return demo.IsValid() ? demo.GetString() : "";
}

void LeaveGame()
{
	// G5ED80's genuine absent-session arm. No CloseSession substitute.
	engine->ClientCmd_Unrestricted( "gameui_hide" );
	engine->ClientCmd_Unrestricted( "disconnect" );
	engine->ClientCmd_Unrestricted( "gameui_activate" );
}
}

GenericConfirmation::UsageData_t::UsageData_t() :
	pWindowTitle( NULL ), pWindowTitleW( NULL ), pMessageText( NULL ), pMessageTextW( NULL ),
	bOkButtonEnabled( false ), pfnOkCallback( NULL ), bCancelButtonEnabled( false ), pfnCancelCallback( NULL ),
	bCheckBoxEnabled( false ), pCheckBoxText( NULL ), pCheckBoxCvarName( NULL )
{
}

GenericConfirmation::GenericConfirmation( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, false, false ), m_pOKCommand( NULL ), m_pCancelCommand( NULL )
{
	SetFooterEnabled( true );
}
GenericConfirmation::~GenericConfirmation()
{
	if ( m_pOKCommand ) m_pOKCommand->deleteThis();
	if ( m_pCancelCommand ) m_pCancelCommand->deleteThis();
}
void GenericConfirmation::SetOKCommand( KeyValues *command )
{
	if ( m_pOKCommand ) m_pOKCommand->deleteThis();
	m_pOKCommand = command;
}
void GenericConfirmation::SetCancelCommand( KeyValues *command )
{
	if ( m_pCancelCommand ) m_pCancelCommand->deleteThis();
	m_pCancelCommand = command;
}
void GenericConfirmation::SetUsageData( const UsageData_t &data )
{
	m_Usage = data;
	m_Title = data.pWindowTitle ? data.pWindowTitle : "";
	m_Message = data.pMessageText ? data.pMessageText : "";
	m_CheckText = data.pCheckBoxText ? data.pCheckBoxText : "";
	m_CheckCvar = data.pCheckBoxCvarName ? data.pCheckBoxCvarName : "";
	m_WideTitle.RemoveAll(); m_WideMessage.RemoveAll();
	if ( data.pWindowTitleW )
	{
		m_WideTitle.SetCount( Q_wcslen( data.pWindowTitleW ) + 1 );
		Q_memcpy( m_WideTitle.Base(), data.pWindowTitleW, m_WideTitle.Count() * sizeof( wchar_t ) );
	}
	if ( data.pMessageTextW )
	{
		m_WideMessage.SetCount( Q_wcslen( data.pMessageTextW ) + 1 );
		Q_memcpy( m_WideMessage.Base(), data.pMessageTextW, m_WideMessage.Count() * sizeof( wchar_t ) );
	}
	m_Usage.pWindowTitle = m_Title.String(); m_Usage.pMessageText = m_Message.String();
	m_Usage.pWindowTitleW = m_WideTitle.Count() ? m_WideTitle.Base() : NULL;
	m_Usage.pMessageTextW = m_WideMessage.Count() ? m_WideMessage.Base() : NULL;
	m_Usage.pCheckBoxText = m_CheckText.String(); m_Usage.pCheckBoxCvarName = m_CheckCvar.String();
	SetOkButtonEnabled( data.bOkButtonEnabled ); SetCancelButtonEnabled( data.bCancelButtonEnabled );
	UpdateControls();
}
void GenericConfirmation::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( true );
	CenterDialogOnScreen();
	UpdateControls();
}
vgui::Panel *GenericConfirmation::CreateControlByName( const char *name )
{
	if ( !Q_stricmp( name, "CvarToggleCheckButton_GameUI" ) ) return new vgui::CheckButton( this, "", "" );
	return BaseClass::CreateControlByName( name );
}
void GenericConfirmation::OnOpen()
{
	BaseClass::OnOpen();
	vgui::CheckButton *check = dynamic_cast<vgui::CheckButton *>( FindChildByName( "CheckButton", true ) );
	ConVarRef value( m_CheckCvar.String(), true );
	if ( check && m_Usage.bCheckBoxEnabled && value.IsValid() ) check->SetSelected( value.GetBool() );
	UpdateControls();
	vgui::Panel *focus = FindChildByName( m_Usage.bOkButtonEnabled ? "BtnOK" : "BtnCancel", true );
	if ( focus ) focus->NavigateTo();
}
void GenericConfirmation::PerformLayout()
{
	BaseClass::PerformLayout();
	UpdateControls();
}
void GenericConfirmation::UpdateControls()
{
	const bool controller = g_pInputSystem && g_pInputSystem->IsSteamControllerActive();
	SetControlVisible( "IconForwardArrow", m_Usage.bOkButtonEnabled && !controller, true );
	SetControlVisible( "BtnOK", m_Usage.bOkButtonEnabled && !controller, true );
	SetControlVisible( "IconBackArrow", m_Usage.bCancelButtonEnabled && !controller, true );
	SetControlVisible( "BtnCancel", m_Usage.bCancelButtonEnabled && !controller, true );
	vgui::Label *message = dynamic_cast<vgui::Label *>( FindChildByName( "LblMessage", true ) );
	if ( message )
	{
		if ( m_Usage.pMessageTextW ) message->SetText( m_Usage.pMessageTextW );
		else message->SetText( m_Message.String() );
	}
	vgui::CheckButton *check = dynamic_cast<vgui::CheckButton *>( FindChildByName( "CheckButton", true ) );
	if ( check ) check->SetVisible( m_Usage.bCheckBoxEnabled );
	SetControlVisible( "LblCheckButton", m_Usage.bCheckBoxEnabled, true );
	SetControlString( "LblCheckButton", m_CheckText.String() );
	SetFooter( this, ( m_Usage.bOkButtonEnabled ? FB_BUTTON_A : 0 ) | ( m_Usage.bCancelButtonEnabled ? FB_BUTTON_B : 0 ), false, "#L4D360UI_OK", "#L4D360UI_Cancel" );
}
void GenericConfirmation::PaintBackground()
{
	DrawDialogBackground( m_Usage.pWindowTitle, m_Usage.pWindowTitleW, NULL, NULL, NULL, false );
}
void GenericConfirmation::Complete( bool accepted )
{
	if ( accepted ? !m_Usage.bOkButtonEnabled : !m_Usage.bCancelButtonEnabled ) return;
	vgui::CheckButton *check = dynamic_cast<vgui::CheckButton *>( FindChildByName( "CheckButton", true ) );
	ConVar *value = m_Usage.bCheckBoxEnabled ? cvar->FindVar( m_CheckCvar.String() ) : NULL;
	if ( check && value ) value->SetValue( check->IsSelected() );
	void ( *callback )() = accepted ? m_Usage.pfnOkCallback : m_Usage.pfnCancelCallback;
	KeyValues *command = accepted ? m_pOKCommand : m_pCancelCommand;
	KeyValues *signal = command ? command->MakeCopy() : NULL;
	vgui::surface()->PlaySound( accepted ? "UI/menu_accept.wav" : "UI/menu_back.wav" );
	BaseClass::NavigateBack();
	if ( signal ) PostActionSignal( signal );
	if ( callback ) callback();
}
void GenericConfirmation::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "OK" ) ) { Complete( true ); return; }
	if ( !Q_stricmp( command, "Cancel" ) || !Q_stricmp( command, "Back" ) ) { Complete( false ); return; }
	BaseClass::OnCommand( command );
}
void GenericConfirmation::OnKeyCodePressed( vgui::KeyCode code )
{
	if ( code == KEY_ENTER || code == KEY_SPACE || code == KEY_XBUTTON_A || code == STEAMCONTROLLER_A ) { Complete( true ); return; }
	if ( code == KEY_ESCAPE || code == KEY_XBUTTON_B || code == STEAMCONTROLLER_B ) { Complete( false ); return; }
	BaseClass::OnKeyCodePressed( code );
}
vgui::Panel *GenericConfirmation::NavigateBack() { Complete( false ); return GetNavBack(); }

InGameDifficultySelect::InGameDifficultySelect( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, true, true ), m_bInitialized( false ) { SetFooterEnabled( true ); }
void InGameDifficultySelect::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( true );
	if ( m_bInitialized ) UpdateCurrentDifficulty();
}
void InGameDifficultySelect::OnOpen()
{
	BaseClass::OnOpen();
	SetFooter( this, FB_NONE, true, NULL, NULL, 0 );
	m_bInitialized = true;
	UpdateCurrentDifficulty();
}
void InGameDifficultySelect::UpdateCurrentDifficulty()
{
	ConVarRef difficulty( "z_difficulty", true );
	if ( !difficulty.IsValid() ) return;
	const char *current = difficulty.GetString();
	wchar_t *text = g_pVGuiLocalize->Find( CFmtStr( "#L4D360UI_Difficulty_%s", current ) );
	if ( text )
	{
		wchar_t formatted[200];
		g_pVGuiLocalize->ConstructString( formatted, sizeof( formatted ), g_pVGuiLocalize->Find( "#L4D360UI_GameSettings_Current_Difficulty" ), 1, text );
		SetControlString( "LblCurrentDifficulty", formatted );
	}
	vgui::Panel *button = FindChildByName( CFmtStr( "Btn%s", current ), true );
	if ( button ) { button->SetEnabled( false ); if ( IsVisible() ) button->NavigateTo(); }
}
void InGameDifficultySelect::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "Easy" ) || !Q_stricmp( command, "Normal" ) || !Q_stricmp( command, "Hard" ) || !Q_stricmp( command, "Impossible" ) )
	{
		ConVarRef difficulty( "z_difficulty", true );
		engine->ClientCmd_Unrestricted( "gameui_hide" );
		if ( difficulty.IsValid() && Q_stricmp( command, difficulty.GetString() ) ) engine->ClientCmd( CFmtStr( "callvote ChangeDifficulty %s;", command ) );
		CL4DFrame::NavigateBack(); return;
	}
	if ( !Q_stricmp( command, "Cancel" ) || !Q_stricmp( command, "Back" ) ) { CloseSelector( this ); return; }
}
vgui::Panel *InGameDifficultySelect::NavigateBack() { CloseSelector( this ); return NULL; }

InGameKickPlayerList::InGameKickPlayerList( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, true, true ), m_bInitialized( false ) { SetFooterEnabled( true ); }
void InGameKickPlayerList::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( true );
	if ( m_bInitialized ) UpdateRoster();
}
void InGameKickPlayerList::OnOpen()
{
	BaseClass::OnOpen();
	SetFooter( this, FB_NONE, true, NULL, NULL, 0 );
	m_bInitialized = true;
	UpdateRoster();
}
void InGameKickPlayerList::UpdateRoster()
{
	int team;
	if ( !GetPlayerTeam( -1, team ) )
	{
		// Native G5EA80's missing-local branch really names rows 0..2.
		for ( int i = 0; i < 3; ++i ) SetControlVisible( CFmtStr( "BtnPlayer%d", i ), false, true );
		return;
	}
	m_UserIDs.RemoveAll();
	for ( int index = 1; index <= engine->GetMaxClients(); ++index )
	{
		player_info_t info;
		if ( engine->GetPlayerInfo( index, &info ) && !info.fakeplayer && CanKickPlayer( index ) ) m_UserIDs.AddToTail( info.userID );
	}
	SetControlVisible( "LblDescription", m_UserIDs.Count() > 0, true );
	SetControlVisible( "LblNoPlayers", m_UserIDs.Count() == 0, true );
	for ( int row = 0; row < 3; ++row )
	{
		L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( FindChildByName( CFmtStr( "BtnPlayer%d", row + 1 ), true ) );
		if ( !button ) continue;
		player_info_t info;
		const int userID = row < m_UserIDs.Count() ? m_UserIDs[row] : 0;
		const int index = userID ? engine->GetPlayerForUserID( userID ) : 0;
		const bool valid = index > 0 && engine->GetPlayerInfo( index, &info );
		button->SetVisible( valid );
		if ( valid ) { button->SetText( info.name ); button->SetCommand( CFmtStr( "KickPlayer%d", userID ) ); }
	}
}
void InGameKickPlayerList::OnCommand( const char *command )
{
	if ( !Q_strnicmp( command, "KickPlayer", 10 ) && Q_strlen( command ) > 10 )
	{
		const int userID = atoi( command + 10 );
		if ( engine->GetPlayerForUserID( userID ) > 0 ) engine->ClientCmd( CFmtStr( "callvote kick %d", userID ) );
		CloseSelector( this ); return;
	}
	if ( !Q_stricmp( command, "Cancel" ) || !Q_stricmp( command, "Back" ) ) { CloseSelector( this ); return; }
}
vgui::Panel *InGameKickPlayerList::NavigateBack() { CloseSelector( this ); return NULL; }

InGameChapterSelect::InGameChapterSelect( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name, false, true ), m_Chapter( 0 ), m_bChangingMission( false )
{
	m_Campaign[0] = 0;
	SetFooterEnabled( true );
}
vgui::Panel *InGameChapterSelect::CreateControlByName( const char *name )
{
	vgui::Panel *control = CreateInGameControl( this, name );
	return control ? control : BaseClass::CreateControlByName( name );
}
void InGameChapterSelect::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( true );
	CenterDialogOnScreen();
	if ( m_Campaign[0] ) InitializeControls();
}
void InGameChapterSelect::OnOpen()
{
	BaseClass::OnOpen();
	SetFooter( this, FB_BUTTON_A | FB_BUTTON_B, true, "#L4D360UI_Select", "#L4D360UI_Cancel" );
	InitializeControls();
}
void InGameChapterSelect::InitializeControls()
{
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	if ( !details ) return;
	KeyValues::AutoDelete release( details );
	const char *modeName = details->GetString( "Game/mode", "coop" );
	const bool singleChapter = IsSingleChapter( modeName );
	if ( m_Campaign[0] )
	{
		details->SetString( "Game/campaign", m_Campaign );
		details->SetInt( "Game/chapter", m_Chapter );
	}
	if ( !singleChapter ) details->SetInt( "Game/chapter", 1 );
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, &mission );
	if ( !mission || !mission->GetInt( "BuiltIn", 0 ) ) { mission = NULL; chapter = NULL; }
	KeyValues *mode = g_TerrorModes.FindMode( modeName );
	KeyValues *references = mode ? mode->FindKey( "missions" ) : NULL;
	if ( !mission )
	{
		for ( KeyValues *entry = references ? references->GetFirstValue() : NULL; entry; entry = entry->GetNextValue() )
		{
			KeyValues *candidate = static_cast<KeyValues *>( entry->GetPtr() );
			if ( !candidate || !candidate->GetInt( "BuiltIn", 0 ) ) continue;
			KeyValues *first = candidate->FindKey( CFmtStr( "modes/%s/1", modeName ) );
			if ( first ) { mission = candidate; chapter = first; break; }
		}
	}
	DropDownMenu *missions = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpMission", true ) );
	DropDownMenu *chapters = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpChapter", true ) );
	if ( !missions || !chapters ) return;
	missions->SetFlyout( CFmtStr( "FlmMission%s", mode ? mode->GetString( "base", modeName ) : modeName ) );
	FlyoutMenu *flyout = missions->GetFlyout();
	if ( flyout )
	{
		flyout->SetFlyoutMenuListener( this );
		flyout->BeginDynamicRows();
		int row = 0;
		for ( KeyValues *entry = references ? references->GetFirstValue() : NULL; entry; entry = entry->GetNextValue() )
		{
			KeyValues *candidate = static_cast<KeyValues *>( entry->GetPtr() );
			if ( !candidate || !candidate->GetInt( "BuiltIn", 0 ) || !candidate->FindKey( CFmtStr( "modes/%s/1", modeName ) ) ) continue;
			flyout->AddDynamicRow( CFmtStr( "BtnCampaign%d", ++row ), candidate->GetString( "DisplayTitle", "" ), CFmtStr( "cmd_campaign_%s", candidate->GetName() ), candidate->GetInt( "dlcrequired", 0 ) == 0 );
		}
		flyout->EndDynamicRows();
	}
	chapters->SetVisible( singleChapter );
	if ( mission && chapter )
	{
		Q_strncpy( m_Campaign, mission->GetName(), sizeof( m_Campaign ) );
		m_Chapter = chapter->GetInt( "chapter", 1 );
		missions->SetCurrentSelection( CFmtStr( "cmd_campaign_%s", m_Campaign ) );
		UpdateChapterRows();
		chapters->SetCurrentSelection( CFmtStr( "#L4D360UI_Chapter_%d", m_Chapter ) );
		UpdateLevelImage();
	}
	if ( IsVisible() ) missions->NavigateTo();
}
void InGameChapterSelect::SelectMission( const char *campaign )
{
	if ( m_bChangingMission || !Q_stricmp( m_Campaign, campaign ) ) return;
	KeyValues *mission = g_TerrorModes.FindMission( campaign );
	if ( !mission || mission->GetInt( "dlcrequired", 0 ) ) return;
	m_bChangingMission = true;
	Q_strncpy( m_Campaign, campaign, sizeof( m_Campaign ) );
	m_Chapter = 1;
	UpdateChapterRows();
	DropDownMenu *chapters = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpChapter", true ) );
	if ( chapters ) chapters->SetCurrentSelection( "#L4D360UI_Chapter_1" );
	UpdateLevelImage();
	m_bChangingMission = false;
}
void InGameChapterSelect::UpdateChapterRows()
{
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	if ( !details ) return;
	KeyValues::AutoDelete release( details );
	DropDownMenu *drop = dynamic_cast<DropDownMenu *>( FindChildByName( "DrpChapter", true ) );
	if ( !drop ) return;
	drop->SetFlyout( "FlmChapterFlyout" );
	FlyoutMenu *flyout = drop->GetFlyout();
	KeyValues *mission = g_TerrorModes.FindMission( m_Campaign );
	KeyValues *chapters = mission ? mission->FindKey( CFmtStr( "modes/%s", details->GetString( "Game/mode", "coop" ) ) ) : NULL;
	if ( !flyout || !chapters ) return;
	flyout->SetFlyoutMenuListener( this );
	flyout->BeginDynamicRows();
	const int count = chapters->GetInt( "chapters", 0 );
	for ( int number = 1; number <= MAX( 2, count ); ++number )
	{
		KeyValues *chapter = chapters->FindKey( CFmtStr( "%d", number ) );
		flyout->AddDynamicRow( CFmtStr( "BtnChapter%d", number ), chapter ? chapter->GetString( "DisplayName", "" ) : "#L4D360UI_Chapter_Only1Avail", CFmtStr( "#L4D360UI_Chapter_%d", number ), chapter != NULL );
	}
	flyout->EndDynamicRows();
	drop->SetVisible( IsSingleChapter( details->GetString( "Game/mode", "coop" ) ) );
}
void InGameChapterSelect::UpdateLevelImage( const char *campaign, int chapterNumber, const char *imageOverride )
{
	vgui::ImagePanel *image = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "ImgLevelImage", true ) );
	if ( !image ) return;
	if ( imageOverride ) { image->SetImage( imageOverride ); return; }
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	if ( !details ) return;
	KeyValues::AutoDelete release( details );
	details->SetString( "Game/campaign", campaign ? campaign : m_Campaign );
	int number = chapterNumber >= 0 ? chapterNumber : m_Chapter;
	if ( !IsSingleChapter( details->GetString( "Game/mode", "coop" ) ) ) number = 0;
	// G52FA0/G53120 resolve chapter1 to obtain the mission, but chapter0
	// presentation is the actual mission image (or native maps/any), not the
	// first chapter's image. Hover does not mutate the admitted selection.
	details->SetInt( "Game/chapter", number ? number : 1 );
	KeyValues *mission = NULL;
	KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, &mission );
	image->SetImage( chapter ? ( number ? chapter->GetString( "image", "" ) : mission->GetString( "image", "maps/any" ) ) : "maps/any" );
}
void InGameChapterSelect::SubmitVote()
{
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	if ( !details ) return;
	KeyValues::AutoDelete release( details );
	if ( !IsSingleChapter( details->GetString( "Game/mode", "" ) ) ) m_Chapter = 1;
	details->SetString( "Game/campaign", m_Campaign );
	details->SetInt( "Game/chapter", m_Chapter );
	vgui::Panel *drop = FindChildByName( "DrpChapter", true );
	if ( drop && drop->IsVisible() )
	{
		KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, NULL );
		if ( chapter ) engine->ClientCmd( CFmtStr( "callvote ChangeChapter %s;", chapter->GetString( "map", "" ) ) );
	}
	else
	{
		m_Chapter = 1;
		engine->ClientCmd( CFmtStr( "callvote ChangeMission %s;", m_Campaign ) );
	}
	CloseSelector( this );
}
void InGameChapterSelect::OnCommand( const char *command )
{
	const char *value = StringAfterPrefix( command, "cmd_campaign_" );
	if ( value ) { SelectMission( value ); return; }
	value = StringAfterPrefix( command, "#L4D360UI_Chapter_" );
	if ( value ) { m_Chapter = atoi( value ); UpdateLevelImage(); return; }
	if ( !Q_stricmp( command, "Select" ) ) { SubmitVote(); return; }
	if ( !Q_stricmp( command, "Cancel" ) || !Q_stricmp( command, "Back" ) ) { CloseSelector( this ); return; }
}
vgui::Panel *InGameChapterSelect::NavigateBack() { CloseSelector( this ); return NULL; }
void InGameChapterSelect::PaintBackground()
{
	vgui::Panel *chapter = FindChildByName( "DrpChapter", true );
	const bool selectChapter = chapter && chapter->IsVisible();
	DrawDialogBackground( selectChapter ? "#L4D360UI_ChapterSelect_Title" : "#L4D360UI_ChangeScenario", NULL,
		selectChapter ? "#L4D360UI_ChapterSelect_Description" : "#L4D360UI_ChangeScenario_Tip" );
}
void InGameChapterSelect::OnNotifyChildFocus( vgui::Panel *child )
{
	L4D360HybridButton *button = dynamic_cast<L4D360HybridButton *>( child );
	if ( !button ) return;
	const char *command = button->Command();
	const char *value = StringAfterPrefix( command, "cmd_campaign_" );
	if ( value ) { UpdateLevelImage( value, 0 ); return; }
	value = StringAfterPrefix( command, "#L4D360UI_Chapter_" );
	if ( value ) { UpdateLevelImage( m_Campaign, atoi( value ) ); return; }
	if ( !Q_stricmp( command, "cmd_addoncampaign" ) ) UpdateLevelImage( NULL, 0, "maps/unknown" );
}
void InGameChapterSelect::OnFlyoutMenuClosed( vgui::Panel *returnTo ) { UpdateLevelImage(); }
void InGameChapterSelect::OnFlyoutMenuCancelled() { UpdateLevelImage(); }

InGameMainMenu::InGameMainMenu( vgui::Panel *parent, const char *name ) : BaseClass( parent, name, false, true, false ) { SetFooterEnabled( true ); }
InGameMainMenu::~InGameMainMenu()
{
	if ( m_hDialog.Get() ) delete m_hDialog.Get();
}
vgui::Panel *InGameMainMenu::CreateControlByName( const char *name )
{
	vgui::Panel *control = CreateInGameControl( this, name );
	return control ? control : BaseClass::CreateControlByName( name );
}
void InGameMainMenu::ApplySchemeSettings( vgui::IScheme *scheme )
{
	vgui::Panel *voteButton = FindChildByName( "BtnCallAVote", true );
	const bool hadVoteButton = voteButton != NULL;
	const bool voteEnabled = hadVoteButton && voteButton->IsEnabled();
	BaseClass::ApplySchemeSettings( scheme );
	UpdateFlyoutControls();
	UpdateControls();
	if ( hadVoteButton ) SetControlEnabled( "BtnCallAVote", voteEnabled, true );
}
void InGameMainMenu::PaintBackground()
{
	// G5FC10 draws only the resource-owned smear, not Frame's opaque fill.
	vgui::Panel *background = FindChildByName( "PnlBackground", false );
	if ( background )
	{
		int x, y, wide, tall;
		background->GetBounds( x, y, wide, tall );
		DrawSmearBackground( x, y, wide, tall, false );
	}
}
void InGameMainMenu::OnOpen()
{
	BaseClass::OnOpen();
	SetFooter( this, FB_BUTTON_A | FB_BUTTON_B, false, "#L4D360UI_Select", "#L4D360UI_Done" );
	KeyValues *details = TerrorBuildConsumedClientGameProjection();
	const bool credits = details && !Q_stricmp( details->GetString( "Game/campaign", "" ), "credits" );
	if ( details ) details->deleteThis();
	ConVarRef voteActive( "cl_vote_active", true );
	ConVarRef rematchActive( "cl_rematch_vote_in_progress", true );
	SetControlEnabled( "BtnCallAVote", !engine->IsPlayingDemo() && !credits &&
		( !voteActive.IsValid() || !voteActive.GetBool() ) &&
		( !rematchActive.IsValid() || !rematchActive.GetBool() ), true );
	UpdateFlyoutControls();
	UpdateControls();
	vgui::Panel *button = FindChildByName( "BtnReturnToGame", true );
	if ( button ) button->NavigateTo();
}
void InGameMainMenu::UpdateFlyoutControls()
{
	FlyoutMenu *flyout = dynamic_cast<FlyoutMenu *>( FindChildByName( "FlmVoteFlyout", true ) );
	if ( flyout )
	{
		flyout->SetFlyoutMenuListener( this );
		vgui::Button *button = flyout->FindChildButtonByCommand( "ReturnToLobby" );
		if ( button ) button->SetEnabled( false ); // G5FDA0 absent network defaults offline.
		button = flyout->FindChildButtonByCommand( "BootPlayer" );
		if ( button ) button->SetEnabled( false );
	}
	for ( int index = 0; index < GetChildCount(); ++index )
	{
		FlyoutMenu *menu = dynamic_cast<FlyoutMenu *>( GetChild( index ) );
		if ( menu ) menu->SetFlyoutMenuListener( this );
	}
	FlyoutMenu *versus = dynamic_cast<FlyoutMenu *>( FindChildByName( "FlmVoteFlyoutVersus", true ) );
	if ( versus && g_TerrorModes.IsBaseMode( "scavenge" ) )
	{
		vgui::Button *restart = versus->FindChildButtonByCommand( "RestartScenario" );
		if ( restart ) restart->SetEnabled( false );
	}
}
void InGameMainMenu::OnThink()
{
	BaseClass::OnThink();
	UpdateControls();
}
void InGameMainMenu::UpdateControls()
{
	// Main-approved no-framework presentation: no actual MatchSession means
	// network offline / mode coop / state lobby, never inferred LIVE.
	SetControlEnabled( "BtnInviteFriends", false, true );
	SetControlEnabled( "BtnLeaderboard", false, true );
	KeyValues *mode = g_TerrorModes.FindMode( "coop" );
	int team = 0;
	const bool idle = mode && mode->GetInt( "maxplayers", 8 ) <= 4 && GetPlayerTeam( -1, team ) && team == 2 && CountTeamPlayers( team ) > 1;
	SetControlEnabled( "BtnGoIdle", idle, true );
	FlyoutMenu *options = dynamic_cast<FlyoutMenu *>( FindChildByName( "FlmOptionsFlyout", true ) );
	if ( options )
	{
		if ( materials ) options->SetControlEnabled( "BtnBrightness", !materials->GetCurrentConfigForVideoCard().Windowed(), true );
		if ( g_pInputSystem ) options->SetControlEnabled( "BtnSteamController", g_pInputSystem->GetNumSteamControllersConnected() != 0, true );
		vgui::Button *enableSplit = options->FindChildButtonByCommand( "EnableSplitscreen" );
		vgui::Button *disableSplit = options->FindChildButtonByCommand( "DisableSplitscreen" );
		if ( enableSplit ) enableSplit->SetVisible( true );
		if ( disableSplit ) disableSplit->SetVisible( false );
	}
}
void InGameMainMenu::OpenDialog( CL4DFrame *dialog )
{
	FlyoutMenu::CloseActiveMenu();
	if ( m_hDialog.Get() ) delete m_hDialog.Get();
	m_hDialog = dialog;
	dialog->SetNavBack( this );
	if ( !dialog->LoadLayout() ) { delete dialog; return; }
	dialog->OnOpen();
}
bool InGameMainMenu::HandleCommand( const char *command )
{
	if ( !Q_stricmp( command, "ReturnToGame" ) || !Q_stricmp( command, "Back" ) ) { engine->ClientCmd_Unrestricted( "gameui_hide" ); return true; }
	if ( !Q_stricmp( command, "GoIdle" ) )
	{
		engine->ClientCmd_Unrestricted( "gameui_hide" );
		engine->ClientCmd_Unrestricted( "go_away_from_keyboard" ); return true;
	}
	if ( !Q_stricmp( command, "BootPlayer" ) ) { OpenDialog( new InGameKickPlayerList( GetParent() ) ); return true; }
	if ( !*DemoUISuffix() && ( !Q_stricmp( command, "ChangeScenario" ) || !Q_stricmp( command, "ChangeChapter" ) ) ) { OpenDialog( new InGameChapterSelect( GetParent() ) ); return true; }
	if ( !Q_stricmp( command, "ChangeDifficulty" ) ) { OpenDialog( new InGameDifficultySelect( GetParent() ) ); return true; }
	const char *vote = !Q_stricmp( command, "RestartScenario" ) ? "callvote RestartGame;" : !Q_stricmp( command, "ReturnToLobby" ) ? "callvote ReturnToLobby;" : !Q_stricmp( command, "ChangeAllTalk" ) ? "callvote ChangeAllTalk;" : NULL;
	if ( vote ) { engine->ClientCmd_Unrestricted( "gameui_hide" ); engine->ClientCmd( vote ); return true; }
	const bool splitChange = !Q_stricmp( command, "EnableSplitscreen" ) || !Q_stricmp( command, "DisableSplitscreen" );
	if ( !Q_stricmp( command, "ExitToMainMenu" ) || splitChange )
	{
		GenericConfirmation *confirm = new GenericConfirmation( GetParent() );
		GenericConfirmation::UsageData_t usage;
		usage.pWindowTitle = "#L4D360UI_LeaveMultiplayerConf";
		usage.pMessageText = splitChange ? "#L4D360UI_MainMenu_SplitscreenChangeConfMsg" : "#L4D360UI_LeaveMultiplayerConfMsg";
		usage.bOkButtonEnabled = usage.bCancelButtonEnabled = true;
		usage.pfnOkCallback = LeaveGame;
		confirm->SetUsageData( usage );
		OpenDialog( confirm ); return true;
	}
	const char *flyoutName = command;
	if ( !Q_stricmp( command, "FlmVoteFlyout" ) )
	{
		KeyValues *mode = g_TerrorModes.GetCurrentMode();
		const char *name = mode ? mode->GetName() : "";
		const char *base = mode ? mode->GetString( "base", name ) : name;
		if ( !Q_stricmp( name, "mutation15" ) ) flyoutName = "FlmVoteFlyoutVersusSurvival";
		else if ( !Q_stricmp( base, "versus" ) || !Q_stricmp( base, "scavenge" ) ) flyoutName = "FlmVoteFlyoutVersus";
		else if ( !Q_stricmp( base, "survival" ) ) flyoutName = "FlmVoteFlyoutSurvival";
		// SurvivalSolo requires a real session with Members/numPlayers==1.
	}
	FlyoutMenu *flyout = dynamic_cast<FlyoutMenu *>( FindChildByName( flyoutName, true ) );
	vgui::Button *anchor = flyout ? FindCommand( this, command ) : NULL;
	if ( flyout && anchor ) { flyout->OpenMenu( anchor ); return true; }
	return false;
}
void InGameMainMenu::OnCommand( const char *command )
{
	if ( !HandleCommand( command ) && GetParent() ) GetParent()->OnCommand( command );
}
vgui::Panel *InGameMainMenu::NavigateBack() { engine->ClientCmd_Unrestricted( "gameui_hide" ); return NULL; }
void InGameMainMenu::OnNotifyChildFocus( vgui::Panel *child ) { (void)child; } // Native G5FBD0 is empty.
void InGameMainMenu::OnFlyoutMenuClosed( vgui::Panel *returnTo )
{
	SetFooter( this, FB_BUTTON_A | FB_BUTTON_B, false, "#L4D360UI_Select", "#L4D360UI_Done" );
}
void InGameMainMenu::OnFlyoutMenuCancelled() {} // Native G5FBE0 is empty.

CL4DFrame *CreateBaseModInGameWindow( vgui::Panel *parent, const char *name )
{
	if ( !Q_stricmp( name, "InGameMainMenu" ) )
	{
		InGameMainMenu *menu = new InGameMainMenu( parent, name );
		const char *suffix = DemoUISuffix();
		const bool loaded = *suffix ? menu->LoadLayout( CFmtStr( "resource/ui/l4d360ui/ingamemainmenu_%s.res", suffix ) ) :
			menu->LoadLayout( "resource/ui/l4d360ui/ingamemainmenu.res" );
		if ( !loaded ) { delete menu; return NULL; }
		menu->OnOpen(); return menu;
	}
	return NULL;
}
} // namespace L4D360UI
