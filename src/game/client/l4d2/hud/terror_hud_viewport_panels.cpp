//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_hud_viewport_panels.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/player/c_transitioning_player.h"
#include "iclientmode.h"
#include "tier0/memdbgon.h"

CTerrorMessageBox::CTerrorMessageBox( IViewPort *viewport, const char *name ) : BaseClass( viewport, name ), m_data( NULL ) {}
CTerrorMessageBox::~CTerrorMessageBox() { if ( m_data ) m_data->deleteThis(); }
void CTerrorMessageBox::SetData( KeyValues *data )
{
	if ( m_data ) m_data->deleteThis();
	m_data = data->MakeCopy(); InvalidateLayout( true, true );
}
void CTerrorMessageBox::Update()
{
	if ( m_data ) LoadControlSettings( m_data->GetString( "res", "" ) );
	InvalidateLayout( true, true );
}
void CTerrorMessageBox::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme ); SetKeyBoardInputEnabled( false );
	if ( m_data )
	{
		LoadControlSettings( m_data->GetString( "res", "" ) );
		const char *title = m_data->GetString( "title", NULL ); if ( title ) SetControlString( "title", title );
		const char *text = m_data->GetString( "text", NULL ); if ( text ) SetControlString( "text", text );
	}
}
CTerrorInfoWindow::CTerrorInfoWindow( IViewPort *viewport, const char *name ) : BaseClass( viewport, name ) {}
CTerrorTeamMenu::CTerrorTeamMenu( IViewPort *viewport ) : BaseClass( viewport, "team" ) {}
void CTerrorTeamMenu::ApplySchemeSettings( vgui::IScheme *scheme ) { LoadControlSettings( "Resource/UI/TeamMenu.res" ); BaseClass::ApplySchemeSettings( scheme ); }
void CTerrorTeamMenu::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "survivor" ) ) { Close(); engine->ClientCmd( "jointeam Survivor\n" ); }
	else if ( !Q_stricmp( command, "infected" ) ) { Close(); engine->ClientCmd( "jointeam Infected\n" ); }
	else BaseClass::OnCommand( command );
}
void CTerrorTeamMenu::OnThink()
{
	// C2E8580/87B0: keep separate bot, available-character and human sets.
	bool available[4] = { true,true,true,true };
	int survivorBots = 0, infectedSlots = 4;
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR );
	if ( resource ) for ( int i = 0; i <= 32; ++i )
	{
		if ( !resource->IsConnected( i ) ) continue;
		if ( resource->GetTeam( i ) == 3 && !resource->IsFakePlayer( i ) ) --infectedSlots;
		if ( resource->GetTeam( i ) != 2 ) continue;
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		const int character = player && !player->IsDormant() ? player->GetSurvivorCharacter() : 8;
		if ( character >= 0 && character < 4 ) available[character] = false;
		if ( resource->IsFakePlayer( i ) ) ++survivorBots;
	}
	const CUtlVector<C_TransitioningPlayer *> &transitioning = GetTransitioningPlayers();
	FOR_EACH_VEC( transitioning, i )
	{
		C_TransitioningPlayer *player = transitioning[i];
		if ( !player || player->IsDormant() ) continue;
		if ( player->GetTeamNumber() == 3 ) --infectedSlots;
		else if ( player->GetTeamNumber() == 2 )
		{
			const int character = player->GetCharacter(); if ( character >= 0 && character < 4 ) available[character] = false;
		}
	}
	bool survivorFull = survivorBots == 0 && !available[0] && !available[1] && !available[2] && !available[3];
	bool infectedFull = infectedSlots <= 0;
	C_TerrorPlayer *local = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( local && local->GetTeamNumber() == 2 ) survivorFull = false;
	else if ( local && local->GetTeamNumber() == 3 ) infectedFull = false;
	bool allowed = true;
	if ( resource && local )
	{
		const int rule = resource->GetTeamSwitchRule( local->entindex() );
		const char *label = rule == 1 ? "#L4D_spectator_team_change_limit" : rule == 2 ? "#L4D_spectator_waiting_for_players" : rule == 3 ? "#L4D_spectator_team_on_team_game" : NULL;
		if ( label ) { SetControlString( "noswitchlabel", label ); allowed = false; }
	}
	SetControlVisible( "survivorbutton", !survivorFull && allowed ); SetControlVisible( "survivorbackground", !survivorFull && allowed );
	SetControlVisible( "survivorfulllabel", survivorFull && allowed ); SetControlVisible( "survivorfullbackground", survivorFull && allowed );
	SetControlVisible( "infectedbutton", !infectedFull && allowed ); SetControlVisible( "infectedbackground", !infectedFull && allowed );
	SetControlVisible( "infectedfulllabel", infectedFull && allowed ); SetControlVisible( "infectedfullbackground", infectedFull && allowed );
	SetControlVisible( "fulltitle", allowed ); SetControlVisible( "noswitchlabel", !allowed ); SetControlVisible( "noswitchtitle", !allowed );
}
CSurvivalModeShutdown::CSurvivalModeShutdown( IViewPort *viewport ) : BaseClass( NULL, "survival_shutting_down" ) { SetParent( g_pClientMode->GetViewport()->GetVPanel() ); }
void CSurvivalModeShutdown::ApplySchemeSettings( vgui::IScheme *scheme ) { LoadControlSettings( "Resource/UI/SurvivalModeShutdown.res" ); BaseClass::ApplySchemeSettings( scheme ); }
CFullscreenVersusModeResults::CFullscreenVersusModeResults( IViewPort *viewport ) : BaseClass( viewport, "fullscreen_vs_results" ) {}
void CFullscreenVersusModeResults::ApplySchemeSettings( vgui::IScheme *scheme ) { LoadControlSettings( "Resource/UI/FullscreenVersusModeResults.res" ); BaseClass::ApplySchemeSettings( scheme ); }
CVsModeShutdown::CVsModeShutdown( IViewPort *viewport ) : BaseClass( NULL, "vs_shutting_down" ) { SetParent( g_pClientMode->GetViewport()->GetVPanel() ); }
void CVsModeShutdown::ApplySchemeSettings( vgui::IScheme *scheme ) { LoadControlSettings( "Resource/UI/VsModeShutdown.res" ); BaseClass::ApplySchemeSettings( scheme ); }
IViewPortPanel *CreateTerrorHudViewportPanel( IViewPort *viewport, const char *name )
{
	if ( !Q_strcmp( name, "team" ) ) return new CTerrorTeamMenu( viewport );
	if ( !Q_strcmp( name, "message_box" ) ) return new CTerrorMessageBox( viewport );
	if ( !Q_strcmp( name, "info_window" ) ) return new CTerrorInfoWindow( viewport );
	if ( !Q_strcmp( name, "fullscreen_vs_results" ) ) return new CFullscreenVersusModeResults( viewport );
	if ( !Q_strcmp( name, "vs_shutting_down" ) ) return new CVsModeShutdown( viewport );
	if ( !Q_strcmp( name, "survival_shutting_down" ) ) return new CSurvivalModeShutdown( viewport );
	return NULL;
}
