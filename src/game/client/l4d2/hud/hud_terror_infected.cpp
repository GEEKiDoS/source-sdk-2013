//========= Copyright Valve Corporation, All rights reserved. ============//
// Native C2BE510..2BF631, C2D24B0..2D45CC. Existing actor/resource/health owners.
#include "cbase.h"
#include "tier1/fmtstr.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/infected/base_ability.h"
#include "l4d2/hud/terror_health_panel.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/hud/hud_terror_localplayer.h"
#include "l4d2/ui/terror_bindpanel.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/hud/terror_pz_endgame.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "in_buttons.h"
#include "game_controls/IconPanel.h"
#include "voice_status.h"
#include <vgui/IVGui.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/CircularProgressBar.h>
#include <vgui_controls/PHandle.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static ConVar debug_zombie_panel( "debug_zombie_panel", "0", 0x00004008, "-1 = Too Far 1 = Tank 2 = Someone else tank" );
static ConVar hud_zombieteam_showself( "hud_zombieteam_showself", "0", 0x00080008, "Show your own status in the zombie team display" );
ConVar hud_zombieteam( "hud_zombieteam", "1", 0x00080008, "Show infected team status on the HUD" );
static C_TerrorPlayerResource *InfectedResource() { return dynamic_cast<C_TerrorPlayerResource *>( g_PR ); }
static C_TerrorPlayer *InfectedLocal() { return ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() ); }
static bool InfectedMenuActive() { static ConVarRef menu( "gameui_ingame_menu_active" ); return menu.GetBool(); }
static vgui::Panel *InfectedPanel( const char *name ) { return g_pClientMode->GetViewport()->FindChildByName( name, false ); }
static bool InfectedPanelVisible( vgui::Panel *panel ) { return panel && panel->IsVisible(); }
static void InfectedLocalizedVariable( vgui::EditablePanel *panel, const char *variable, const char *token )
{
	if ( !token || !*token ) { panel->SetDialogVariable( variable, "" ); return; }
	const wchar_t *text = g_pVGuiLocalize->Find( token ); if ( text ) panel->SetDialogVariable( variable, text );
}

class CHudZombieHealth : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudZombieHealth, vgui::EditablePanel );
public:
	CHudZombieHealth( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudZombieHealth" ), m_pHealth( NULL ), m_pDucking( NULL ), m_iClass( 9 ), m_iHealth( -1 ) { SetHiddenBits( 16 ); }
	virtual void Init() OVERRIDE { vgui::ivgui()->AddTickSignal( GetVPanel(), 50 ); }
	virtual bool ShouldDraw() OVERRIDE { C_TerrorPlayer *p = InfectedLocal(); return p && p->IsAlive() && p->GetTeamNumber() == TEAM_INFECTED && !p->IsGhost() && !p->IsThirdPersonCameraActive() && CHudElement::ShouldDraw(); }
protected:
	virtual void OnTick() OVERRIDE
	{
		C_TerrorPlayer *p = InfectedLocal(); if ( !p ) return;
		const int type = p->GetZombieClass(); if ( type != m_iClass ) { m_iClass = type; InvalidateLayout( true, true ); }
		if ( m_pHealth ) { const int health = p->GetHealth(); if ( health != m_iHealth ) { m_iHealth = health; SetDialogVariable( "HealthNumber", health ); } m_pHealth->SetHealth( health, 0, p->GetMaxHealth(), false, false, true ); }
		if ( m_pDucking ) m_pDucking->SetVisible( ( p->m_Local.m_bDucked || p->m_Local.m_bDucking ) && ( p->GetFlags() & FL_ONGROUND ) && ( p->m_nButtons & IN_DUCK ) );
	}
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		const char *file = NULL; switch ( m_iClass ) { case 1: file = "SmokerHealth"; break; case 2: file = "BoomerHealth"; break; case 3: case 8: file = "HunterHealth"; break; case 4: file = "SpitterHealth"; break; case 5: file = "JockeyHealth"; break; case 6: file = "ChargerHealth"; break; }
		if ( file ) LoadControlSettings( CFmtStr( "Resource/UI/HUD/%s.res", file ), "GAME" ); // Single supported split-screen slot0: no vertical mutation.
		m_pHealth = dynamic_cast<HealthPanel *>( FindChildByName( "Health" ) ); m_pDucking = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "DuckingIcon" ) ); BaseClass::ApplySchemeSettings( scheme ); m_iHealth = -1;
	}
private:
	HealthPanel *m_pHealth; vgui::ImagePanel *m_pDucking; int m_iClass, m_iHealth;
};
DECLARE_HUDELEMENT( CHudZombieHealth );

class CHudGhostPanel : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudGhostPanel, vgui::EditablePanel );
public:
	CHudGhostPanel( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudGhostPanel" ), m_iState( -1 ), m_iBadPlace( -1 ), m_iClass( -1 )
	{
		SetHiddenBits( 64 ); m_pClassImage = new CIconPanel( this, "ClassImage" ); m_pClassName = new vgui::Label( this, "ClassName", "" ); m_pSelectSpawn = new vgui::Label( this, "SelectSpawn", "" ); m_pReady = new vgui::Label( this, "Ready", "" ); m_pInfo = new vgui::Label( this, "Info", "" ); m_pBackground = new vgui::Panel( this, "Background" ); m_pSpawnLabel = new vgui::Label( this, "SpawnLabel", "" ); m_pBind = new CBindPanel( this, "SpawnBind" ); m_pBind->AddActionSignalTarget( this ); vgui::ivgui()->AddTickSignal( GetVPanel(), 0 ); SetDialogVariable( "classname", "" ); SetDialogVariable( "ready", "" ); SetDialogVariable( "info", "" );
	}
	virtual void Reset() OVERRIDE { m_iState = m_iBadPlace = m_iClass = -1; }
	virtual bool ShouldDraw() OVERRIDE
	{
		C_TerrorPlayer *p = InfectedLocal(); if ( !p || !p->IsGhost() ) return false;
		static const char *const names[] = { "spawn_hunter", "spawn_spitter", "spawn_jockey", "spawn_charger", "spawn_smoker", "spawn_boomer", "spawnmode", "scores", "fullscreen_vs_scoreboard", "fullscreen_vs_results", "HudZombiePanel", "team", "fullscreen_scavenge_scoreboard", "CHudVote" };
		for ( int i = 0; i < ARRAYSIZE( names ); ++i ) if ( !m_VisibilityPanels[i].Get() ) m_VisibilityPanels[i] = InfectedPanel( names[i] );
		for ( int i = 0; i < ARRAYSIZE( names ) - 1; ++i ) if ( InfectedPanelVisible( m_VisibilityPanels[i].Get() ) ) return false;
		// C2BEFC2 checks vote existence but queries zombie-panel again. Preserve
		// that native mix-up; omit only a null dependent zombie dereference.
		if ( m_VisibilityPanels[13].Get() && InfectedPanelVisible( m_VisibilityPanels[10].Get() ) ) return false;
		return !InfectedMenuActive() && CHudElement::ShouldDraw();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { BaseClass::ApplySchemeSettings( scheme ); LoadControlSettings( "resource/UI/HudGhostPanel.res", "GAME" ); Reset(); m_pClassName->SetFgColor( m_clrWhite ); m_pSelectSpawn->SetFgColor( m_clrWhite ); m_pBind->SetBind( "+attack", 0 ); }
	virtual void PerformLayout() OVERRIDE { BaseClass::PerformLayout(); m_pBind->InvalidateLayout( true, false ); int x, y; m_pBind->GetPos( x, y ); const int left = x + m_iPadding + m_pBind->GetWide(); m_pSpawnLabel->SetBounds( left, y, GetWide() - left, m_pBind->GetTall() ); }
	virtual void OnCommand( const char *command ) OVERRIDE { BaseClass::OnCommand( command ); if ( !Q_stricmp( command, "KeyChanged" ) ) InvalidateLayout(); }
	virtual void OnTick() OVERRIDE
	{
		C_TerrorPlayer *p = InfectedLocal(); if ( !p || !ShouldDraw() ) return;
		const int type = p->GetZombieClass(); if ( type != m_iClass )
		{
			const char *token, *icon; switch ( type ) { case 1: token = "#L4D_spawn_select_mode_class_SMOKER"; icon = "tip_smoker"; break; case 3: token = "#L4D_spawn_select_mode_class_HUNTER"; icon = "tip_hunter"; break; case 4: token = "#L4D_spawn_select_mode_class_SPITTER"; icon = "tip_spitter"; break; case 5: token = "#L4D_spawn_select_mode_class_JOCKEY"; icon = "tip_jockey"; break; case 6: token = "#L4D_spawn_select_mode_class_CHARGER"; icon = "tip_charger"; break; case 8: token = "#L4D_pz_spawn_btn_tank"; icon = "tip_tank"; break; default: token = "#L4D_spawn_select_mode_class_BOOMER"; icon = "tip_boomer"; break; }
			InfectedLocalizedVariable( this, "classname", token ); m_pClassImage->SetIcon( icon ); m_iClass = type;
		}
		const int state = p->m_ghostSpawnState; if ( state == m_iState ) return; // Clock-only changes do not refresh: native cache bug.
		if ( state == 0 && p->m_ghostSpawnClockCurrentDelay <= 0 ) { m_pReady->SetFgColor( m_clrWhite ); m_pInfo->SetFgColor( m_clrWhite ); m_pBind->SetVisible( true ); m_pSpawnLabel->SetVisible( true ); InfectedLocalizedVariable( this, "ready", "#L4D_Zombie_UI_Action" ); SetDialogVariable( "info", "" ); p->EmitSound( "PZ.Hud.Action" ); m_iBadPlace = -1; }
		else
		{
			const char *ready = "", *info = ""; int bad = 0; m_pBind->SetVisible( false ); m_pSpawnLabel->SetVisible( false );
			if ( state & 512 ) { info = "#L4D_spawn_select_mode_restricted_area"; bad = 512; }
			else if ( state & 1024 ) { info = "#L4D_spawn_select_mode_inside_entity"; bad = 1024; }
			else if ( state & 256 ) { info = "#L4D_spawn_select_mode_too_close"; bad = 256; }
			else if ( state & 128 ) { info = "#L4D_spawn_select_mode_in_sight"; bad = 128; }
			else if ( state & 1 ) ready = "#L4D_Zombie_UI_Spawn_Disabled";
			else if ( state & 32 ) ready = "#L4D_Zombie_UI_Director_TimeOut";
			else if ( state & 2 ) ready = "#L4D_Zombie_UI_Wait_For_Safe_Area";
			else if ( state & 4 ) ready = "#L4D_Zombie_UI_Wait_For_Finale";
			else if ( state & 8 ) ready = "#L4D_Zombie_UI_Wait_For_Tank";
			else if ( state & 16 ) ready = "#L4D_Zombie_UI_Survivor_Escaped";
			else if ( state & 64 ) ready = "#L4D_Zombie_UI_Wait_For_Stampede";
			if ( bad ) { ready = "#L4D_Zombie_UI_BadPlace"; const bool changed = m_iBadPlace != bad; m_iBadPlace = bad; if ( changed ) p->EmitSound( "PZ.Hud.BadPlace" ); }
			else if ( state & 127 ) m_iBadPlace = -1;
			m_pReady->SetFgColor( m_clrRed ); m_pInfo->SetFgColor( m_clrRed ); InfectedLocalizedVariable( this, "ready", ready ); InfectedLocalizedVariable( this, "info", info );
		}
		m_iState = state;
	}
private:
	int m_iState, m_iBadPlace, m_iClass; CIconPanel *m_pClassImage; vgui::Label *m_pClassName, *m_pSelectSpawn, *m_pReady, *m_pInfo, *m_pSpawnLabel; vgui::Panel *m_pBackground; CBindPanel *m_pBind;
	vgui::PHandle m_VisibilityPanels[14];
	CPanelAnimationVar( Color, m_clrWhite, "WhiteText", "FgColor" ); CPanelAnimationVar( Color, m_clrRed, "RedText", "FgColor" ); CPanelAnimationVarAliasType( int, m_iPadding, "padding", "4", "proportional_int" );
};
DECLARE_HUDELEMENT( CHudGhostPanel );

class CHudZombiePanel : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudZombiePanel, vgui::EditablePanel );
public:
	CHudZombiePanel( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudZombiePanel" ), m_iTankPlayer( -1 ), m_pTooFarText( NULL ) { SetHiddenBits( 64 ); m_pTank = new vgui::EditablePanel( this, "TankTakeover" ); m_pTooFar = new vgui::EditablePanel( this, "TooFarFromSurvivors" ); m_pTank->SetDialogVariable( "tanktitle", "" ); m_pTank->SetDialogVariable( "tanktext", "" ); m_pBind = new CBindPanel( m_pTooFar, "UseBind" ); m_pBind->AddActionSignalTarget( this ); }
	virtual void Init() OVERRIDE { vgui::ivgui()->AddTickSignal( GetVPanel(), 50 ); }
	virtual bool ShouldDraw() OVERRIDE
	{
		static const char *const names[] = { "scores", "fullscreen_vs_scoreboard", "fullscreen_vs_results", "team" };
		for ( int i = 0; i < ARRAYSIZE( names ); ++i ) if ( !m_VisibilityPanels[i].Get() ) m_VisibilityPanels[i] = InfectedPanel( names[i] );
		if ( debug_zombie_panel.GetInt() ) return true;
		if ( InfectedPanelVisible( m_VisibilityPanels[1].Get() ) || InfectedPanelVisible( m_VisibilityPanels[2].Get() ) || InfectedPanelVisible( m_VisibilityPanels[3].Get() ) || IsPZEndGamePanelVisible() ) return false;
		C_TerrorPlayer *p = InfectedLocal();
		return p && p->GetTeamNumber() == TEAM_INFECTED && ( ShouldShowTooFar( p ) || ShouldShowTank( p ) || InfectedPanelVisible( m_VisibilityPanels[0].Get() ) ) && CHudElement::ShouldDraw();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "resource/UI/ZombiePanel.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); CIconPanel *icon = dynamic_cast<CIconPanel *>( FindChildByName( "SurvivorsImage", true ) ); if ( icon ) icon->SetIcon( "tip_crouch" ); m_pTooFarText = dynamic_cast<vgui::Label *>( FindChildByName( "TooFarText", true ) ); m_pBind->SetBind( "+use", 0 ); }
	virtual void PerformLayout() OVERRIDE { BaseClass::PerformLayout(); m_pBind->InvalidateLayout( true, false ); }
	virtual void OnCommand( const char *command ) OVERRIDE { BaseClass::OnCommand( command ); if ( !Q_stricmp( command, "KeyChanged" ) ) InvalidateLayout(); }
	virtual void OnTick() OVERRIDE
	{
		C_TerrorPlayer *p = InfectedLocal(); C_TerrorPlayerResource *resource = InfectedResource();
		if ( IsVisible() && ShouldShowTank( p ) )
		{
			m_pTank->SetVisible( true ); const int pending = resource ? resource->GetPendingTankPlayerIndex() : 0;
			if ( m_iTankPlayer != pending )
			{
				if ( ( p && p->entindex() == pending ) || debug_zombie_panel.GetInt() == 1 ) { InfectedLocalizedVariable( m_pTank, "tanktitle", "#L4D_tank_take_control" ); InfectedLocalizedVariable( m_pTank, "tanktext", "#L4D_tank_take_control_tip" ); }
				else { InfectedLocalizedVariable( m_pTank, "tanktitle", "#L4D_tank_awoken" ); const wchar_t *format = g_pVGuiLocalize->Find( "#L4D_tank_is_becoming" ); if ( format && resource ) { wchar_t name[32], text[128]; g_pVGuiLocalize->ConvertANSIToUnicode( resource->GetPlayerName( pending ), name, sizeof( name ) ); g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, name ); m_pTank->SetDialogVariable( "tanktext", text ); } }
				m_iTankPlayer = pending;
			}
		}
		else { m_pTank->SetVisible( false ); m_iTankPlayer = -1; }
		if ( !IsVisible() || !ShouldShowTooFar( p ) ) m_pTooFar->SetVisible( false );
		else if ( !m_pTooFar->IsVisible() ) { m_pTooFar->SetVisible( true ); CIconPanel *icon = dynamic_cast<CIconPanel *>( FindChildByName( "SurvivorsImage", true ) ); if ( icon ) { const char *tip = "tip_crouch"; switch ( p->GetZombieClass() ) { case 1: tip = "tip_smoker"; break; case 2: tip = "tip_boomer"; break; case 3: tip = "tip_hunter"; break; case 4: tip = "tip_spitter"; break; case 5: tip = "tip_jockey"; break; case 6: tip = "tip_charger"; break; case 8: tip = "tip_tank_incap"; break; } icon->SetIcon( tip ); } }
	}
private:
	bool ShouldShowTank( C_TerrorPlayer *p ) const { if ( !p || p->GetTeamNumber() != TEAM_INFECTED || !InfectedResource() || InfectedPanelVisible( m_VisibilityPanels[0].Get() ) ) return false; if ( debug_zombie_panel.GetInt() > 0 ) return true; C_TerrorPlayerResource *r = InfectedResource(); return ( r->GetTankLotteryEntryRatio() > 0.0f || r->GetTankLotterySelectionRatio() > 0.0f ) && r->GetPendingTankPlayerIndex() != 0 && ( r->GetPendingTankPlayerIndex() == p->entindex() || !p->IsGhost() ); }
	bool ShouldShowTooFar( C_TerrorPlayer *p ) const
	{
		if ( !p || p->GetTeamNumber() != TEAM_INFECTED || m_pTank->IsVisible() || InfectedPanelVisible( m_VisibilityPanels[0].Get() ) || ( TerrorGameRules() && TerrorGameRules()->IsScavengeMode() ) ) return false;
		if ( debug_zombie_panel.GetInt() < 0 ) return true;
		if ( p->IsGhost() && (unsigned char)p->GetObserverMode() >= OBS_MODE_FIXED && !( p->GetFlags() & FL_ONGROUND ) )
		{
			KeyValues *mission = NULL; TerrorClient_GetTransitionChapterInfo( false, &mission );
			if ( mission && !Q_strcmp( mission->GetString( "Name" ), "L4D2C7" ) ) return true;
		}
		return p->m_bNativeTooFarFromSurvivors && p->IsAlive();
	}
	int m_iTankPlayer; vgui::EditablePanel *m_pTank, *m_pTooFar; CBindPanel *m_pBind; vgui::Label *m_pTooFarText;
	vgui::PHandle m_VisibilityPanels[4];
	CPanelAnimationVarAliasType( int, m_iPadding, "padding", "4", "proportional_int" );
};
DECLARE_HUDELEMENT( CHudZombiePanel );

class CHudZombieTeamDisplayPlayer : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudZombieTeamDisplayPlayer, vgui::EditablePanel );
public:
	CHudZombieTeamDisplayPlayer( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_iPlayer( 0 ), m_iSpawnTime( -1 ), m_bActive( false ), m_pSkull( NULL ), m_pDead( NULL ), m_pAbility( NULL ) { m_szName[0] = 0; m_pName = new vgui::Label( this, "NameLabel", "" ); m_pName->SetContentAlignment( vgui::Label::a_center ); m_pSpawn = new vgui::Label( this, "SpawnTimeLabel", "" ); m_pSpawn->SetContentAlignment( vgui::Label::a_center ); m_pSkullPlacement = new vgui::Panel( this, "SkullIconPlacement" ); m_pHealth = new HealthPanel( this, "HealthPanel" ); m_pImage = new vgui::ImagePanel( this, "PlayerImage" ); m_pVoice = CreateTerrorTeammateVoicePanel( this ); }
	void SetPlayerIndex( int index ) { m_iPlayer = index; }
	void UpdatePlayer()
	{
		C_TerrorPlayerResource *r = InfectedResource(); C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByIndex( m_iPlayer ) );
		if ( !p || !r || !p->IsPlayer() || !r->IsConnected( m_iPlayer ) || p->GetTeamNumber() != TEAM_INFECTED ) { SetVisible( false ); return; } SetVisible( true );
		if ( Q_strcmp( m_szName, p->GetPlayerName() ) ) { m_pName->SetText( p->GetPlayerName() ); Q_strncpy( m_szName, p->GetPlayerName(), sizeof( m_szName ) ); }
		if ( m_pDead ) m_pDead->SetVisible( !p->IsAlive() && !p->IsGhost() );
		if ( !p->IsAlive() || p->IsIncapacitated() ) { const double remaining = (double)p->GetBecomeGhostAt() - (double)gpGlobals->curtime; const int time = p->m_scrimmageType != 0 ? -1 : ( remaining < 0.0 ? 0 : TerrorHudCvttFloat( (float)remaining ) ); if ( time != m_iSpawnTime ) { m_iSpawnTime = time; m_pSpawn->SetText( CFmtStr( "%d", time ) ); } m_pSpawn->SetVisible( time > 0 ); m_pHealth->SetVisible( false ); m_bActive = false; }
		else { m_bActive = true; m_pSpawn->SetVisible( false ); m_pHealth->SetVisible( true ); const char *type = ""; switch ( p->GetZombieClass() ) { case 1: type = "Smoker"; break; case 2: type = "Boomer"; break; case 3: type = "Hunter"; break; case 4: type = "Spitter"; break; case 5: type = "Jockey"; break; case 6: type = "Charger"; break; case 8: type = "Tank"; break; } m_pImage->SetImage( *type ? CFmtStr( "hud/%sTeamImage_%s", p->IsGhost() ? "Ghost" : "Zombie", type ).Access() : "" ); m_pImage->SetDrawColor( p->IsGhost() ? Color( 206, 219, 225, 255 ) : Color( 255, 255, 255, 255 ) ); }
		m_pImage->SetVisible( m_bActive ); m_pHealth->SetHealth( p->GetHealth(), 0, p->GetMaxHealth(), false, false, false );
		if ( m_pVoice ) m_pVoice->SetVisible( p == C_BasePlayer::GetLocalPlayer() ? GetClientVoiceMgr()->IsLocalPlayerSpeaking() : ( m_iPlayer <= 32 && GetClientVoiceMgr()->IsPlayerSpeaking( m_iPlayer ) ) );
		if ( m_pAbility ) { const bool show = m_bActive && !p->IsGhost() && p->GetZombieClass() != 3; m_pAbility->SetVisible( show ); if ( show ) { C_BaseAbility *ability = p->GetCustomAbility(); m_pAbility->SetProgress( ability ? ability->GetRechargeProgress() : 0.0f ); } }
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { BaseClass::ApplySchemeSettings( scheme ); m_szName[0] = 0; m_iSpawnTime = -1; LoadControlSettings( "resource/UI/HUD/ZombieTeamDisplayPlayer.res", "GAME" ); m_pSkull = gHUD.GetIcon( "icon_skull" ); m_pImage->SetShouldScaleImage( true ); m_pImage->SetImage( "hud/ZombieTeamImage_Hunter" ); m_pImage->SetDrawColor( Color( 255, 255, 255, 255 ) ); m_pDead = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "Dead" ) ); m_pAbility = dynamic_cast<vgui::CircularProgressBar *>( FindChildByName( "AbilityProgress" ) ); }
	virtual void Paint() OVERRIDE { if ( m_pSkull && !m_bActive ) { int x, y, wide, tall; m_pSkullPlacement->GetBounds( x, y, wide, tall ); m_pSkull->DrawSelf( x, y, wide, tall, Color( 255, 255, 255, 255 ) ); } }
private:
	int m_iPlayer, m_iSpawnTime; bool m_bActive; char m_szName[64]; CHudTexture *m_pSkull; vgui::Label *m_pName, *m_pSpawn; vgui::Panel *m_pSkullPlacement, *m_pVoice; HealthPanel *m_pHealth; vgui::ImagePanel *m_pImage, *m_pDead; vgui::CircularProgressBar *m_pAbility;
};
class CHudZombieTeamDisplay : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudZombieTeamDisplay, vgui::EditablePanel );
public:
	CHudZombieTeamDisplay( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), name ) { for ( int i = 0; i < 3; ++i ) m_pPlayer[i] = new CHudZombieTeamDisplayPlayer( this, "ZombieTeamDisplayPlayer" ); vgui::ivgui()->AddTickSignal( GetVPanel(), 100 ); }
	virtual bool ShouldDraw() OVERRIDE { C_TerrorPlayer *p = InfectedLocal(); return p && p->GetTeamNumber() != TEAM_SURVIVOR && !InfectedMenuActive() && hud_zombieteam.GetBool() && CHudElement::ShouldDraw(); }
protected:
	virtual void PerformLayout() OVERRIDE { BaseClass::PerformLayout(); for ( int i = 0; i < 3; ++i ) m_pPlayer[i]->SetPos( i * m_iHorizPanelSpacing, 0 ); }
	virtual void OnTick() OVERRIDE { C_TerrorPlayerResource *r = InfectedResource(); if ( !r ) return; C_TerrorPlayer *local = InfectedLocal(); int slot = 0; for ( int i = 1; i <= gpGlobals->maxClients && slot < 3; ++i ) { if ( r->IsFakePlayer( i ) ) continue; C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByIndex( i ) ); if ( p && p->IsPlayer() && r->IsConnected( i ) && p->GetTeamNumber() == TEAM_INFECTED && ( p != local || hud_zombieteam_showself.GetBool() ) ) m_pPlayer[slot++]->SetPlayerIndex( i ); } while ( slot < 3 ) m_pPlayer[slot++]->SetPlayerIndex( 0 ); for ( int i = 0; i < 3; ++i ) m_pPlayer[i]->UpdatePlayer(); }
private:
	CHudZombieTeamDisplayPlayer *m_pPlayer[3]; CPanelAnimationVarAliasType( int, m_iHorizPanelSpacing, "HorizPanelSpacing", "140", "proportional_int" );
};
DECLARE_HUDELEMENT( CHudZombieTeamDisplay );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
