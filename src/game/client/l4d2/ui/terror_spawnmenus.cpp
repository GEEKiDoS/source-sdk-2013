//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/ui/terror_spawnmenus.h"
#include "l4d2/ui/terror_bindpanel.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/terror_shareddefs.h"
#include "l4d2/clientmode_terror.h"
#include "vgui_controls/Label.h"
#include "l4d2/gamerules/terror_mode.h"
#include "game_controls/baseviewport.h"
#include "voice_status.h"
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

// Native CRT1050CA90 passes a NULL default. Create103D3EC0 normalizes it
// to the empty string (numeric0) and rawflags0 to DEVELOPMENTONLY.
static ConVar s_gameui_ingame_menu_active( "gameui_ingame_menu_active", "", 0x0000000A, "Holds the state of the in game menu. Do not set this manually." );

// One named capability lookup for native matchmaking local-user storage.
// Main selected the native no-local-user branch: no invented counters, file,
// ConVar or profile provider. Storage-dependent bodies remain in the evidence.
static const void *TerrorClient_QueryLocalSpawnMenuUserStorage()
{
	return NULL; // No local-user storage in this target.
}

const char *GetTerrorSpawnMenuName( int index )
{
	static const char *names[] = { "spawnmode", "spawn_smoker", "spawn_boomer", "spawn_hunter", "spawn_spitter", "spawn_jockey", "spawn_charger" };
	return index >= 0 && index < ARRAYSIZE( names ) ? names[index] : NULL;
}

CON_COMMAND_F( reset_spawnmenu_counts, "" , 0x00000008)
{
	// Native 2E1280 visits active slots and writes bytes24..30 only if
	// that slot has a local actor AND real local-user profile storage.
	if ( !C_BasePlayer::GetLocalPlayer() ) return;
	if ( !TerrorClient_QueryLocalSpawnMenuUserStorage() ) return;
}

CSpawnModeBar::CSpawnModeBar( IViewPort *viewport ) :
	BaseClass( viewport, "spawnmode_bar" )
{
	// Native +552 is unwritten until SetData. No read before SetData is
	// reachable past the approved no-local-user guard; do not invent a default.
}
void CSpawnModeBar::SetData( KeyValues *data )
{
	m_iZombieClass = data->GetInt( "zombieClass", 0 );
	if ( m_iZombieClass != 3 && m_iZombieClass != 1 && m_iZombieClass != 2 &&
		m_iZombieClass != 6 && m_iZombieClass != 4 && m_iZombieClass != 5 ) m_iZombieClass = 3;
}
void CSpawnModeBar::ShowPanel( bool state )
{
	if ( vgui::Panel::IsVisible() == state ) return;
	BaseClass::ShowPanel( state );
	if ( !state ) return;
	if ( !ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ) ) return;
	if ( !TerrorClient_QueryLocalSpawnMenuUserStorage() ) return;
	// Main-approved elision: native's storage-dependent <5 tutorial opening
	// is unreachable after the real no-local-user guard above.
}

CSpawnBaseMenu::CSpawnBaseMenu( IViewPort *viewport, const char *name ) :
	BaseClass( viewport, name ), m_iZombieClass( 3 )
{
	m_pContinueBind = new CBindPanel( this, "ContinueBind" );
	m_pContinueLabel = new vgui::Label( this, "ContinueLabel", "" );
	m_pBackground = new vgui::Panel( this, "Background" );
	m_pContinueBind->AddActionSignalTarget( this );
	SetKeyBoardInputEnabled( false );
	SetMouseInputEnabled( false );
}
const char *CSpawnBaseMenu::GetLayoutFile() const { return ""; } // 2E2350.
void CSpawnBaseMenu::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	// Supported slot0 is not split-screen; native creates no conditions KV.
	LoadControlSettings( GetLayoutFile() );
}
void CSpawnBaseMenu::SetVisible( bool state )
{
	SetAlpha( 255 ); // Native resets alpha even while hiding.
	vgui::Panel::SetVisible( state );
}
void CSpawnBaseMenu::ShowPanel( bool state )
{
	// Do not Activate(), change focus/input, or checkpoint as the common
	// CTerrorViewportPanel does: retail calls only this SetVisible override.
	if ( vgui::Panel::IsVisible() != state ) SetVisible( state );
}
bool CSpawnBaseMenu::CanShowPanel( const char *name ) const
{
	return Q_stricmp( name, "team" ) != 0;
}
bool CSpawnBaseMenu::IsAllowedToShow() const
{
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	return player && player->GetTeamNumber() == TEAM_INFECTED && player->IsGhost();
}
void CSpawnBaseMenu::CachePanelPointers()
{
	// Main-approved single-slot projection of the two actual retail roots:
	// GetFullscreenTerrorViewport228230 and GetTerrorViewport228210 both
	// resolve the ONE real ClientModeTerrorNormal viewport, never a new root.
	vgui::Panel *root = GetClientModeNormal()->GetViewport();
	if ( !root ) return;
	if ( !m_Scores.Get() ) m_Scores = root->FindChildByName( "scores", false );
	if ( !m_VersusScores.Get() ) m_VersusScores = root->FindChildByName( "fullscreen_vs_scoreboard", false );
	if ( !m_VersusResults.Get() ) m_VersusResults = root->FindChildByName( "fullscreen_vs_results", false );
	if ( !m_ScavengeScores.Get() ) m_ScavengeScores = root->FindChildByName( "fullscreen_scavenge_scoreboard", false );
	if ( !m_ZombiePanel.Get() ) m_ZombiePanel = root->FindChildByName( "HudZombiePanel", false );
	if ( !m_Vote.Get() ) m_Vote = root->FindChildByName( "CHudVote", false );
	if ( !m_Team.Get() ) m_Team = root->FindChildByName( "team", false );
}
bool CSpawnBaseMenu::IsVisible()
{
	CachePanelPointers();
	if ( m_Vote.Get() && m_Vote->IsVisible() ) return false;
	static ConVarRef gameui_ingame_menu_active( "gameui_ingame_menu_active" );
	if ( gameui_ingame_menu_active.GetInt() != 0 ) return false;
	return vgui::Panel::IsVisible();
}
void CSpawnBaseMenu::OnThink()
{
	BaseClass::OnThink();
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player || !player->IsGhost() || player->GetTeamNumber() != TEAM_INFECTED ) { Close(); return; }
	CachePanelPointers();
	if ( ( m_VersusScores.Get() && m_VersusScores->IsVisible() ) ||
		( m_VersusResults.Get() && m_VersusResults->IsVisible() ) ||
		( m_ScavengeScores.Get() && m_ScavengeScores->IsVisible() ) ||
		( m_ZombiePanel.Get() && m_ZombiePanel->IsVisible() ) ||
		( m_Team.Get() && m_Team->IsVisible() ) ) { Close(); return; }
	// 10064EE0 resolves native C_BasePlayer::m_hViewEntity at5164.
	// It is not m_hUseEntity or the observer target.
	if ( ( m_Scores.Get() && m_Scores->IsVisible() ) || player->GetViewEntity() ) SetAlpha( 0 );
	else SetAlpha( 255 );
}
bool CSpawnBaseMenu::HudElementKeyInput( int down, ButtonCode_t key, const char *binding )
{
	if ( !IsVisible() || !binding || !down || Q_stricmp( binding, "+attack" ) ) return true;
	OnCommand( "close" );
	return false; // Native consumes only a visible +attack key-down.
}
void CSpawnBaseMenu::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "KeyChanged" ) ) InvalidateLayout( false, false );
	else if ( !Q_stricmp( command, "close" ) ) Close();
	else BaseClass::OnCommand( command );
}
void CSpawnBaseMenu::PerformLayout()
{
	BaseClass::PerformLayout();
	if ( !m_pContinueBind || !m_pContinueLabel ) return;
	m_pContinueBind->InvalidateLayout( true, false );
	int bindX, bindY, bindWide, bindTall;
	int labelX, labelY, labelWide, labelTall;
	m_pContinueBind->GetBounds( bindX, bindY, bindWide, bindTall );
	m_pContinueLabel->GetBounds( labelX, labelY, labelWide, labelTall );
	int contentWide, contentTall;
	m_pContinueLabel->GetContentSize( contentWide, contentTall );
	if ( contentWide > labelWide )
	{
		// Preserve native bug: old labelWide is passed as the new Y position.
		m_pContinueLabel->SetBounds( labelX, labelWide, contentWide, labelTall );
		m_pContinueLabel->InvalidateLayout( false, false );
	}
	int left = ( m_pBackground->GetWide() - contentWide - bindWide ) / 2;
	if ( left < 0 ) left = 0;
	m_pContinueBind->SetPos( left, bindY );
	m_pContinueLabel->SetPos( bindWide + left, labelY );
}

CSpawnModeMenu::CSpawnModeMenu( IViewPort *viewport ) : BaseClass( viewport, "spawnmode" ) {}
const char *CSpawnModeMenu::GetLayoutFile() const { return "Resource/UI/SpawnModeMenu.res"; }
void CSpawnModeMenu::OnCommand( const char *command )
{
	BaseClass::OnCommand( command );
	if ( Q_stricmp( command, "close" ) ) return;
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player || !player->IsGhost() ) return;
	if ( !TerrorClient_QueryLocalSpawnMenuUserStorage() ) return;
	// Native only opens/counts the current-class tutorial after that guard.
}

// The six native constructors are identical apart from class/name/resource.
// Extra child pointers are never read by native; VGUI already owns them.
static void CreateSpawnActionControls( vgui::Panel *parent )
{
	CBindPanel *one = new CBindPanel( parent, "ActionOneBind" );
	new vgui::Label( parent, "ActionOneLabel", "" );
	CBindPanel *two = new CBindPanel( parent, "ActionTwoBind" );
	new vgui::Label( parent, "ActionTwoLabel", "" );
	one->AddActionSignalTarget( parent );
	two->AddActionSignalTarget( parent );
}
CSpawnHunterMenu::CSpawnHunterMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_hunter" ) { CreateSpawnActionControls( this ); }
const char *CSpawnHunterMenu::GetLayoutFile() const { return "Resource/UI/SpawnHunterMenu.res"; }
CSpawnSmokerMenu::CSpawnSmokerMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_smoker" ) { CreateSpawnActionControls( this ); }
const char *CSpawnSmokerMenu::GetLayoutFile() const { return "Resource/UI/SpawnSmokerMenu.res"; }
CSpawnBoomerMenu::CSpawnBoomerMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_boomer" ) { CreateSpawnActionControls( this ); }
const char *CSpawnBoomerMenu::GetLayoutFile() const { return "Resource/UI/SpawnBoomerMenu.res"; }
CSpawnChargerMenu::CSpawnChargerMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_charger" ) { CreateSpawnActionControls( this ); }
const char *CSpawnChargerMenu::GetLayoutFile() const { return "Resource/UI/SpawnChargerMenu.res"; }
CSpawnJockeyMenu::CSpawnJockeyMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_jockey" ) { CreateSpawnActionControls( this ); }
const char *CSpawnJockeyMenu::GetLayoutFile() const { return "Resource/UI/SpawnJockeyMenu.res"; }
CSpawnSpitterMenu::CSpawnSpitterMenu( IViewPort *viewport ) : BaseClass( viewport, "spawn_spitter" ) { CreateSpawnActionControls( this ); }
const char *CSpawnSpitterMenu::GetLayoutFile() const { return "Resource/UI/SpawnSpitterMenu.res"; }

IViewPortPanel *CreateTerrorSpawnPanel( IViewPort *viewport, const char *name )
{
	if ( !Q_strcmp( name, "spawnmode_bar" ) ) return new CSpawnModeBar( viewport );
	if ( !Q_strcmp( name, "spawnmode" ) ) return new CSpawnModeMenu( viewport );
	if ( !Q_strcmp( name, "spawn_hunter" ) ) return new CSpawnHunterMenu( viewport );
	if ( !Q_strcmp( name, "spawn_charger" ) ) return new CSpawnChargerMenu( viewport );
	if ( !Q_strcmp( name, "spawn_smoker" ) ) return new CSpawnSmokerMenu( viewport );
	if ( !Q_strcmp( name, "spawn_boomer" ) ) return new CSpawnBoomerMenu( viewport );
	if ( !Q_strcmp( name, "spawn_spitter" ) ) return new CSpawnSpitterMenu( viewport );
	if ( !Q_strcmp( name, "spawn_jockey" ) ) return new CSpawnJockeyMenu( viewport );
	return NULL;
}
#pragma float_control( pop )
#if defined( L4D2 ) && defined( CLIENT_DLL )
#include "l4d2/ui/terror_spawnmenus_console.inc"
#include "l4d2/ui/terror_spawnmenus_console_registrations.inc"
#include "l4d2/hud/terror_scores_console.inc"
#include "l4d2/hud/terror_scores_console_registrations.inc"
#endif
