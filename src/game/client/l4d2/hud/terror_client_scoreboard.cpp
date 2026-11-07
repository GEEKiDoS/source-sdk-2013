//========= Copyright Valve Corporation, All rights reserved. ============//
// CTerrorClientScoreBoardDialog C2D6EE0..2D958F. Sole main/coop scoreboard.
#include "cbase.h"
#include "l4d2/hud/terror_client_scoreboard.h"
#include "l4d2/hud/terror_embedded_scoreboards.h"
#include "l4d2/hud/terror_scoreboard_stats.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/player/c_transitioning_player.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "l4d2/terror_native_qsort.h"
#include "l4d2/hud/hud_terror_statscrawl.h"
#include "cdll_util.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/SectionedListPanel.h>
#include <vgui/IImage.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
extern bool IsInCommentaryMode( void ); // Existing sole real commentary lock vector, C08B7C0.
CTerrorClientScoreBoardDialog::CTerrorClientScoreBoardDialog( IViewPort *viewport ) : BaseClass( viewport ), m_versus( NULL ), m_scavenge( NULL ), m_selected( -1 ), m_playerSlot( -1 ), m_alignment( 4 )
{
	m_hostname[0] = 0; for ( int i = 0; i < 4; ++i ) { char name[16]; Q_snprintf( name, sizeof( name ), "Survivor%d", i + 1 ); m_rows[i] = new SurvivorStatsPanel( this, name, "Resource/UI/ScoreBoardSurvivor.res" ); } for ( int i = 0; i < 5; ++i ) { char name[16]; Q_snprintf( name, sizeof( name ), "Infected%d", i + 1 ); m_rows[4 + i] = new InfectedStatsPanel( this, name, "Resource/UI/ScoreBoardInfectedPlayer.res" ); } ReloadLayout(); ListenForGameEvent( "difficulty_changed" ); ListenForGameEvent( "hostname_changed" ); SetProportional( true ); SetZPos( 100 );
}
void CTerrorClientScoreBoardDialog::DeleteData( CUtlVector<KeyValues *> &data ) { for ( int i = 0; i < data.Count(); ++i ) data[i]->deleteThis(); data.RemoveAll(); }
void CTerrorClientScoreBoardDialog::ReloadLayout()
{
	LoadControlSettings( "Resource/UI/ScoreBoard.res", "GAME" ); m_versus = dynamic_cast<CVersusModeEmbeddedScoreboard *>( FindChildByName( "VersusModeScoreboard" ) ); m_scavenge = dynamic_cast<CScavengeModeEmbeddedScoreboard *>( FindChildByName( "ScavengeModeScoreboard" ) ); for ( int i = 0; i < 9; ++i ) if ( m_rows[i] ) m_rows[i]->InvalidateLayout( false, false ); UpdateServerName();
}
void CTerrorClientScoreBoardDialog::Reset() { BaseClass::Reset(); SetControlVisible( "InfectedBackground", TerrorGameRules() && TerrorGameRules()->IsPlayerControlledZombies() ); }
void CTerrorClientScoreBoardDialog::ApplySchemeSettings( vgui::IScheme *scheme )
{
	ReloadLayout(); BaseClass::ApplySchemeSettings( scheme ); SetBgColor( Color( 0, 0, 0, 0 ) ); SetBorder( scheme->GetBorder( "BaseBorder" ) ); if ( m_pPlayerList ) m_pPlayerList->SetVisible( false ); m_versus = dynamic_cast<CVersusModeEmbeddedScoreboard *>( FindChildByName( "VersusModeScoreboard" ) ); m_scavenge = dynamic_cast<CScavengeModeEmbeddedScoreboard *>( FindChildByName( "ScavengeModeScoreboard" ) ); if ( m_versus ) m_versus->SetPlayerSlot( m_playerSlot ); Update();
}
// Native C1E3FB0/C1E4680. ApplySettings owns the nine-position resource key;
// the SDK's always-center helper is not the native policy.
void CTerrorClientScoreBoardDialog::ApplySettings( KeyValues *settings )
{
	BaseClass::ApplySettings( settings );
	static const char *const names[] = { "north-west", "north", "north-east", "west", "center", "east", "south-west", "south", "south-east" };
	m_alignment = 4;
	for ( int i = 0; i < ARRAYSIZE( names ); ++i ) if ( !Q_stricmp( settings->GetString( "scoreboard_position", "" ), names[i] ) ) { m_alignment = i; break; }
}
void CTerrorClientScoreBoardDialog::PositionScoreboard()
{
	const int dx = TerrorHudSub32( ScreenWidth(), GetWide() ), dy = TerrorHudSub32( ScreenHeight(), GetTall() );
	SetPos( m_alignment % 3 == 0 ? 0 : ( m_alignment % 3 == 1 ? dx / 2 : dx ), m_alignment < 3 ? 0 : ( m_alignment < 6 ? dy / 2 : dy ) );
}
void CTerrorClientScoreBoardDialog::UpdateServerName() { vgui::Panel *label = FindChildByName( "ServerName" ); if ( label ) { PostMessage( label, new KeyValues( "SetText", "text", m_hostname ) ); label->Repaint(); } }
void CTerrorClientScoreBoardDialog::FireGameEvent( IGameEvent *event )
{
	if ( !Q_stricmp( event->GetName(), "difficulty_changed" ) ) { UpdateMission(); return; } if ( !Q_strcmp( event->GetName(), "server_spawn" ) || !Q_strcmp( event->GetName(), "hostname_changed" ) ) Q_strncpy( m_hostname, event->GetString( "hostname", "" ), sizeof( m_hostname ) ); BaseClass::FireGameEvent( event );
}
void CTerrorClientScoreBoardDialog::Update()
{
	UpdateServerName(); UpdateSurvivors(); UpdateInfected(); UpdateSpectators(); UpdateMission(); UpdateMedals(); PositionScoreboard(); Repaint(); m_fNextUpdateTime = gpGlobals->curtime + 1.0f; CTerrorGameRules *rules = TerrorGameRules(); if ( m_versus ) m_versus->SetVisible( rules && rules->IsVersusMode() ); if ( m_scavenge ) m_scavenge->SetVisible( rules && rules->IsScavengeMode() ); CTransitionStatsPanel *transition = GetTransitionStatsPanel(); if ( transition && transition->IsVisible() ) ShowPanel( false );
}
void CTerrorClientScoreBoardDialog::ShowPanel( bool show )
{
	if ( !g_PR && show ) return; if ( !m_pImageList ) InvalidateLayout( true, true ); if ( IsInCommentaryMode() || ( g_pStatsCrawl && g_pStatsCrawl->IsShowingCredits() ) ) show = false; CTransitionStatsPanel *transition = GetTransitionStatsPanel(); if ( transition && transition->IsVisible() ) show = false; SetMouseInputEnabled( false ); SetKeyBoardInputEnabled( false );
	if ( show ) { m_playerSlot = -1; if ( m_versus && TerrorGameRules() && TerrorGameRules()->IsVersusMode() ) m_versus->UpdateScores(); if ( m_scavenge && TerrorGameRules() && TerrorGameRules()->IsScavengeMode() ) m_scavenge->UpdateScores(); UpdateSurvivors(); UpdateInfected(); if ( (unsigned int)m_selected < 9u && m_rows[m_selected] ) m_rows[m_selected]->SetSelected( false ); m_selected = 0; C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); const int index = local ? local->entindex() : 0; for ( int i = 0; i < 9; ++i ) if ( m_rows[i] && m_rows[i]->IsVisible() ) { const int playerIndex = m_rows[i]->GetPlayerIndex(); C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( UTIL_PlayerByIndex( playerIndex ) ); if ( playerIndex == index || ( bot && !bot->IsDormant() && bot->GetHumanSpectatorUserID() > 0 && bot->GetHumanSpectatorEntIndex() == index ) ) { m_rows[i]->SetSelected( true ); m_selected = i; break; } } }
	// Existing generic owner supplies the canonical close-key/reset/show lifecycle.
	BaseClass::ShowPanel( show );
}
KeyValues *CTerrorClientScoreBoardDialog::TransitionData( C_TransitioningPlayer *player )
{
	KeyValues *data = new KeyValues( "data" ); data->SetInt( "playerIndex", player->entindex() ); data->SetInt( "team", player->GetTeamNumber() ); char safeName[MAX_PLAYER_NAME_LENGTH * 2 + 1]; UTIL_MakeSafeName( player->GetPlayerName(), safeName, sizeof( safeName ) ); data->SetString( "name", safeName ); data->SetInt( "deaths", player->GetDeaths() ); data->SetInt( "score", player->GetScore() ); data->SetInt( "transitioning", 1 ); if ( player->GetTeamNumber() != TEAM_SURVIVOR ) data->SetString( "customstatus", "#L4D_Scoreboard_s_Loading" ); data->SetInt( "ping", 0 ); return data;
}
void CTerrorClientScoreBoardDialog::UpdateSurvivors()
{
	DeleteData( m_survivors ); C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); if ( !r ) return;
	for ( int i = 0; i < 4; ++i ) { C_TerrorPlayer *player = GetTerrorPlayerBySurvivorCharacter( i ); if ( !player || !r->IsConnected( player->entindex() ) ) continue; const int index = player->entindex(); KeyValues *data = new KeyValues( "data" ); data->SetInt( "playerIndex", index ); data->SetInt( "health", r->GetHealth( index ) ); data->SetInt( "healthbuffer", TerrorHudFtol( (double)player->GetHealthBuffer() ) ); data->SetInt( "maxhealth", r->GetMaxHealth( index ) ); data->SetInt( "isGoingToDie", player->m_isGoingToDie ); data->SetInt( "isIncapacitated", player->IsIncapacitated() ); data->SetInt( "isAlive", player->m_lifeState == LIFE_ALIVE ); data->SetInt( "character", i ); data->SetInt( "isIT", !( gpGlobals->curtime >= player->GetITTimerTimestamp() ) ); data->SetInt( "firstAidSlot", r->GetFirstAidSlot( index ) ); data->SetInt( "pillsSlot", r->GetPillsSlot( index ) ); data->SetInt( "grenadeSlot", r->GetGrenade( index ) ); data->SetInt( "transitioning", 0 ); data->SetInt( "isScoreboard", 1 ); C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( player ); data->SetString( "name", player->IsBot() && bot && bot->GetHumanSpectatorUserID() <= 0 ? GetTerrorSurvivorDisplayNameToken( i ) : r->GetPlayerName( index ) ); data->SetInt( "ping", r->GetPing( index ) ); m_survivors.AddToTail( data ); }
	const CUtlVector<C_TransitioningPlayer *> &transitioning = GetTransitioningPlayers(); for ( int i = 0; i < transitioning.Count(); ++i ) { C_TransitioningPlayer *p = transitioning[i]; if ( !p || p->IsDormant() || p->GetTeamNumber() != TEAM_SURVIVOR ) continue; KeyValues *data = TransitionData( p ); data->SetInt( "isScoreboard", 1 ); data->SetInt( "health", p->GetHealth() ); data->SetInt( "healthbuffer", p->GetHealthBuffer() ); data->SetInt( "maxhealth", 100 ); data->SetInt( "isGoingToDie", p->IsTransitionDying() ); data->SetInt( "isIncapacitated", p->IsIncapacitated() ); data->SetInt( "isAlive", p->IsTransitionAlive() ); data->SetInt( "character", p->GetCharacter() ); data->SetInt( "isIT", 0 ); data->SetString( "upgrades", "" ); m_survivors.AddToTail( data ); }
	for ( int character = 0; character < 4; ++character ) { bool found = false; for ( int i = 0; i < m_survivors.Count(); ++i ) if ( m_survivors[i]->GetInt( "character" ) == character ) { if ( m_rows[character] ) m_rows[character]->UpdateStats( m_survivors[i] ); found = true; break; } if ( m_rows[character] ) m_rows[character]->SetVisible( found ); }
}
// C2D72B0 is intentionally a bool (0/1), NOT a three-way comparator. Preserve
// its broken comparator AND the original qsort caller, never std::sort/stable_sort.
static int __cdecl NativeInfectedSort( const void *left, const void *right )
{
	KeyValues *a = *static_cast<KeyValues *const *>( left ), *b = *static_cast<KeyValues *const *>( right ); const bool nonpositivePing = a->GetInt( "ping" ) <= 0; if ( nonpositivePing != ( b->GetInt( "ping" ) <= 0 ) ) return nonpositivePing; const int score = a->GetInt( "score" ), otherScore = b->GetInt( "score" ); if ( score > otherScore ) return 1; if ( score < otherScore ) return 0; const int loading = a->GetInt( "transitioning" ), otherLoading = b->GetInt( "transitioning" ); if ( loading != otherLoading ) return loading < otherLoading; return a->GetInt( "playerIndex" ) < b->GetInt( "playerIndex" );
}
void CTerrorClientScoreBoardDialog::UpdateInfected()
{
	DeleteData( m_infected ); CTerrorGameRules *rules = TerrorGameRules(); if ( !rules || !rules->IsPlayerControlledZombies() ) { for ( int i = 4; i < 9; ++i ) if ( m_rows[i] ) m_rows[i]->SetVisible( false ); return; } C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); if ( !r ) return; C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); const bool showStatus = !local || local->GetTeamNumber() != TEAM_SURVIVOR;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i ) if ( r->IsConnected( i ) && r->GetTeam( i ) == TEAM_INFECTED && !r->IsFakePlayer( i ) ) { KeyValues *data = new KeyValues( "data" ); data->SetInt( "playerIndex", i ); data->SetString( "name", r->GetPlayerName( i ) ); data->SetInt( "score", r->GetPlayerScore( i ) ); data->SetInt( "deaths", r->GetDeaths( i ) ); data->SetInt( "ping", r->GetPing( i ) ); data->SetInt( "transitioning", 0 ); if ( showStatus ) { if ( !r->IsAlive( i ) ) data->SetString( "status", "#L4D_Scoreboard_Dead" ); else if ( r->IsGhost( i ) ) data->SetString( "status", "#L4D_Scoreboard_Spawning" ); else if ( r->GetZombieClass( i ) != -1 ) { const char *status = " "; switch ( r->GetZombieClass( i ) ) { case 1: status = "#L4D_spawn_select_mode_class_SMOKER"; break; case 2: status = "#L4D_spawn_select_mode_class_BOOMER"; break; case 3: status = "#L4D_spawn_select_mode_class_HUNTER"; break; case 4: status = "#L4D_spawn_select_mode_class_SPITTER"; break; case 5: status = "#L4D_spawn_select_mode_class_JOCKEY"; break; case 6: status = "#L4D_spawn_select_mode_class_CHARGER"; break; case 8: status = "#L4D_Zombie_UI_Tank"; break; } data->SetString( "status", status ); } } data->SetInt( "showAsAlive", showStatus && r->IsAlive( i ) && !r->IsGhost( i ) ); data->SetInt( "isBot", r->IsFakePlayer( i ) ); m_infected.AddToTail( data ); }
	const CUtlVector<C_TransitioningPlayer *> &transitioning = GetTransitioningPlayers(); for ( int i = 0; i < transitioning.Count(); ++i ) if ( transitioning[i] && !transitioning[i]->IsDormant() && transitioning[i]->GetTeamNumber() == TEAM_INFECTED ) m_infected.AddToTail( TransitionData( transitioning[i] ) ); TerrorNativeQSort( m_infected.Base(), m_infected.Count(), sizeof( KeyValues * ), NativeInfectedSort ); for ( int i = 0; i < 5; ++i ) if ( m_rows[4 + i] ) { const bool show = i < m_infected.Count(); if ( show ) m_rows[4 + i]->UpdateStats( m_infected[i] ); m_rows[4 + i]->SetVisible( show ); }
}
void CTerrorClientScoreBoardDialog::UpdateSpectators()
{
	char names[512] = ""; int count = 0; for ( int i = 1; i <= gpGlobals->maxClients; ++i ) if ( g_PR && g_PR->IsConnected( i ) && g_PR->GetTeam( i ) <= TEAM_SPECTATOR ) { if ( count ) Q_strncat( names, ", ", sizeof( names ) ); Q_strncat( names, g_PR->GetPlayerName( i ), sizeof( names ) ); ++count; } wchar_t output[512] = L""; if ( count ) { wchar_t wideNames[1024], number[16]; g_pVGuiLocalize->ConvertANSIToUnicode( names, wideNames, sizeof( wideNames ) ); const wchar_t *format = g_pVGuiLocalize->Find( count == 1 ? "#L4D_Scoreboard_Spectator" : "#L4D_Scoreboard_Spectators" ); if ( format ) { if ( count == 1 ) g_pVGuiLocalize->ConstructString( output, sizeof( output ), format, 1, wideNames ); else { V_snwprintf( number, ARRAYSIZE( number ), L"%i", count ); g_pVGuiLocalize->ConstructString( output, sizeof( output ), format, 2, number, wideNames ); } } } SetDialogVariable( "spectators", output );
}
void CTerrorClientScoreBoardDialog::UpdateMedals()
{
	CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return; const bool survival = rules->IsSurvivalMode(); const char *controls[] = { "ImgGoldMedal", "ImgSilverMedal", "ImgBronzeMedal", "LblBronzeMedalTime", "LblSilverMedalTime", "LblGoldMedalTime" }; for ( int i = 0; i < ARRAYSIZE( controls ); ++i ) SetControlVisible( controls[i], survival ); if ( survival ) { SetControlString( "LblBronzeMedalTime", "4:00" ); SetControlString( "LblSilverMedalTime", "7:00" ); SetControlString( "LblGoldMedalTime", "10:00" ); }
}
void CTerrorClientScoreBoardDialog::CenterMarker( const char *name, int center ) { vgui::Panel *panel = FindChildByName( name ); if ( panel ) { int x, y; panel->GetPos( x, y ); panel->SetPos( TerrorHudFtol( (double)center - (double)panel->GetWide() * 0.5 ), y ); } }
void CTerrorClientScoreBoardDialog::SetMapImage( vgui::ImagePanel *panel, const char *image )
{
	char path[MAX_PATH]; Q_snprintf( path, sizeof( path ), "../vgui/%s", image ); panel->SetVisible( true ); panel->SetShouldScaleImage( true ); panel->SetImage( path ); const int texture = vgui::scheme()->GetImageID( path, true ); if ( texture != -1 ) { int w, h; vgui::surface()->DrawGetTextureSize( texture, w, h ); panel->SetDrawColor( w == 32 && h == 32 ? Color( 0, 0, 0, 0 ) : Color( 255, 255, 255, 255 ) ); }
}
void CTerrorClientScoreBoardDialog::UpdateMission()
{
	if ( !g_PR ) return; CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return; KeyValues *mission = NULL; KeyValues *chapter = TerrorClient_GetTransitionChapterInfo( false, &mission ); if ( !chapter ) return; // Same approved Game projection/dictionary, no fake MatchSession.
	const bool versus = rules->IsVersusMode(), survival = rules->IsSurvivalMode(), scavenge = rules->IsScavengeMode(); const char *modeToken; if ( versus ) modeToken = "#L4D_Versus"; else if ( survival ) modeToken = "#L4D_Survival"; else if ( scavenge ) modeToken = "#L4D_Scavenge"; else { static const char *levels[4] = { "Easy", "Normal", "Hard", "Impossible" }, *tokens[4] = { "#L4D_DifficultyEasy", "#L4D_DifficultyNormal", "#L4D_DifficultyHard", "#L4D_DifficultyImpossible" }; modeToken = tokens[1]; static ConVarRef difficulty( "z_difficulty" ); for ( int i = 0; i < 4; ++i ) if ( !Q_stricmp( difficulty.GetString(), levels[i] ) ) { modeToken = tokens[i]; break; } }
	const char *title = mission ? mission->GetString( "DisplayTitle", "" ) : ""; wchar_t titleBuffer[64]; const wchar_t *wideTitle = g_pVGuiLocalize->Find( title ); if ( !wideTitle ) { g_pVGuiLocalize->ConvertANSIToUnicode( title, titleBuffer, sizeof( titleBuffer ) ); wideTitle = titleBuffer; } const wchar_t *mode = g_pVGuiLocalize->Find( modeToken ), *realism = rules->IsRealismMode() ? g_pVGuiLocalize->Find( "#L4D_Realism" ) : NULL; wchar_t text[100]; V_snwprintf( text, ARRAYSIZE( text ), L"%ls, %ls %ls", wideTitle ? wideTitle : L"", realism ? realism : L"", mode ? mode : L"" ); SetControlString( "MissionTitle", text ); SetControlVisible( "MissionObjective", !versus && !survival && !scavenge );
	// Native's copied Game subtree walks real dictionary chapters. Keep the one
	// approved projection as a consumed query object, not a session substitute.
	KeyValues *settings = new KeyValues( "settings" ); KeyValues::AutoDelete freeSettings( settings ); settings->SetString( "Game/mode", g_TerrorModes.GetName() ); if ( mission ) settings->SetString( "Game/campaign", mission->GetName() ); int count = 1; const int current = chapter->GetInt( "chapter", 1 ); if ( !versus && !survival && !scavenge ) { count = current; for ( int next = current + 1; ; ++next ) { settings->SetInt( "Game/chapter", next ); if ( !g_TerrorModes.GetChapterInfo( settings, NULL ) ) break; count = next; } }
	SetControlVisible( "ImgLevelLargeImage", false ); SetControlVisible( "ImgLevelLargeImageFrame", false ); if ( count >= 2 )
	{
		SetControlVisible( "CurrentMap", current < count ); SetControlVisible( "CurrentMapArrow", current < count ); bool includesLast = true; int offset = 0; if ( count > 5 ) { if ( current > count - 5 ) offset = count - 5; else { offset = current - 1; includesLast = false; } } SetControlVisible( "OpponentMap", false ); SetControlVisible( "RescueMap", includesLast && !versus && !survival && !scavenge ); SetControlVisible( "RescueMapArrow", includesLast && !versus && !survival && !scavenge );
		for ( int i = 1; i <= 5; ++i ) { char field[16]; Q_snprintf( field, sizeof( field ), "Map%d", i ); vgui::ImagePanel *panel = dynamic_cast<vgui::ImagePanel *>( FindChildByName( field ) ); if ( !panel ) continue; const int mapIndex = offset + i; settings->SetInt( "Game/chapter", mapIndex ); KeyValues *map = g_TerrorModes.GetChapterInfo( settings, NULL ); panel->SetVisible( map != NULL ); if ( map ) { SetMapImage( panel, map->GetString( "Image", "" ) ); int x, y; panel->GetPos( x, y ); const int center = TerrorHudFtol( (double)panel->GetWide() * 0.5 + x ); if ( mapIndex == current ) { CenterMarker( "CurrentMap", center ); CenterMarker( "CurrentMapArrow", center ); } if ( mapIndex == count ) { CenterMarker( "RescueMap", center ); CenterMarker( "RescueMapArrow", center ); } } }
	}
	else
	{
		const char *controls[] = { "CurrentMap", "CurrentMapArrow", "OpponentMap", "RescueMap", "RescueMapArrow", "Map1", "Map2", "Map3", "Map4", "Map5" }; for ( int i = 0; i < ARRAYSIZE( controls ); ++i ) SetControlVisible( controls[i], false ); if ( !versus && !scavenge ) { vgui::ImagePanel *panel = dynamic_cast<vgui::ImagePanel *>( FindChildByName( "ImgLevelLargeImage" ) ); if ( panel ) { SetMapImage( panel, chapter->GetString( "Image", "" ) ); const bool visible = panel->GetDrawColor().a() != 0; SetControlVisible( "ImgLevelLargeImageFrame", visible ); } }
	}
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
