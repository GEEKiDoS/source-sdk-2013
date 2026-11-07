//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_scoreboard_stats.h"
#include "l4d2/hud/terror_hud_round_timers.h" // Batch A owns the sole native C2C4410 numeric helper.
#include "l4d2/hud/terror_health_panel.h"
#include "l4d2/hud/terror_hud_helpers.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/bots/c_survivor_bot.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/weapons/terror_weapon.h"
#include "l4d2/weapons/terror_weapon_parse.h"
#include "l4d2/effects/terror_fx_shared.h"
#include "vgui_avatarimage.h"
#include "game_controls/IconPanel.h"
#include "voice_status.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/Label.h"
#include "tier0/memdbgon.h"

BasePlayerStatsPanel::BasePlayerStatsPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ) {}
static void SelectStatsPanel( vgui::EditablePanel *panel, bool selected )
{
	vgui::Panel *normal = panel->FindChildByName( "PlayerBackground" ); if ( normal ) normal->SetVisible( !selected );
	vgui::Panel *active = panel->FindChildByName( "PlayerBackground_Selected" ); if ( active ) active->SetVisible( selected );
	panel->SetZPos( selected ? 2 : 1 );
}
InfectedStatsPanel::InfectedStatsPanel( vgui::Panel *parent, const char *name, const char *layout ) : BaseClass( parent, name ), m_layout( layout ), m_playerIndex( 0 )
{
	m_avatar = new CAvatarImagePanel( this, "Avatar" ); LoadControlSettings( layout );
}
void InfectedStatsPanel::ApplySchemeSettings( vgui::IScheme *scheme ) { BaseClass::ApplySchemeSettings( scheme ); LoadControlSettings( m_layout ); }
void InfectedStatsPanel::SetSelected( bool selected ) { SelectStatsPanel( this, selected ); }
void InfectedStatsPanel::UpdateStats( KeyValues *data )
{
	m_playerIndex = data->GetInt( "playerindex" );
	bool avatar = !data->GetBool( "isBot" );
	player_info_t info;
	if ( avatar && engine->GetPlayerInfo( m_playerIndex, &info ) ) avatar = !info.fakeplayer;
	m_avatar->SetPlayer( m_playerIndex ); m_avatar->SetVisible( avatar );
	const char *names[] = { "Name", "NoAvatarName" }, *statuses[] = { "Status", "NoAvatarStatus" };
	for ( int i = 0; i < 2; ++i )
	{
		vgui::Label *name = dynamic_cast<vgui::Label *>( FindChildByName( names[i] ) );
		if ( name ) { name->SetText( data->GetString( "name", "" ) ); name->SetVisible( i == 0 ? avatar : !avatar ); }
		vgui::Label *status = dynamic_cast<vgui::Label *>( FindChildByName( statuses[i] ) );
		if ( status ) status->SetVisible( i == 0 ? avatar : !avatar );
	}
	const int ping = data->GetInt( "ping" );
	SetControlVisible( "PingImage", ping > 0 ); SetControlVisible( "PingLabel", ping > 0 );
	if ( ping > 0 ) { SetControlString( "PingImage", ping <= 70 ? "O" : ping <= 140 ? "N" : "M" ); char text[8]; Q_snprintf( text, sizeof( text ), "%d", ping ); SetControlString( "PingLabel", text ); }
	const char *status = data->GetString( "status", "" ); const wchar_t *localized = g_pVGuiLocalize->Find( status );
	if ( localized ) SetControlString( "Status", localized ); else SetControlString( "Status", " " );
	SetControlString( "NoAvatarStatus", status );
	HealthPanel *health = dynamic_cast<HealthPanel *>( FindChildByName( "Health" ) );
	if ( health )
	{
		const bool alive = data->GetBool( "isAlive" );
		health->SetHealth( alive ? data->GetInt( "health" ) : 0, alive ? data->GetInt( "healthBuffer" ) : 0, data->GetInt( "maxHealth" ), alive && data->GetBool( "isGoingToDie" ), alive && data->GetBool( "isIncapacitated" ), false );
	}
	if ( g_PR && m_playerIndex >= 0 && m_playerIndex < MAX_PLAYERS_ARRAY_SAFE ) SetDialogVariable( "score", g_PR->GetPlayerScore( m_playerIndex ) );
}
SurvivorStatsPanel::SurvivorStatsPanel( vgui::Panel *parent, const char *name, const char *layout ) : BaseClass( parent, name ), m_layout( layout ), m_playerIndex( 0 )
{
	m_avatar = new CAvatarImagePanel( this, "SurvivorStatsAvatar" ); m_head = new vgui::ImagePanel( this, "SurvivorStatsHead" );
	m_name[0] = new vgui::Label( this, "SurvivorStatsName", "" ); m_name[1] = new vgui::Label( this, "SurvivorStatsNoAvatarName", "" );
	m_status[0] = new vgui::Label( this, "SurvivorStatsStatus", "" ); m_status[1] = new vgui::Label( this, "SurvivorStatsNoAvatarStatus", "" );
	m_pingImage = new vgui::Label( this, "PingImage", "" ); m_pingLabel = new vgui::Label( this, "PingLabel", "" );
	m_health = new HealthPanel( this, "SurvivorStatsHealth" ); m_voice = new CIconPanel( this, "Voice" );
	m_record = new vgui::Label( this, "SurvivorSurvivalRecord", "" ); m_recordImage = new CIconPanel( this, "SurvivorSurvivalRecordImage" );
	m_slots[0] = m_slots[1] = m_slots[2] = NULL;
	LoadControlSettings( layout );
}
void SurvivorStatsPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme ); LoadControlSettings( m_layout );
	m_slots[0] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_Pills" ) );
	m_slots[1] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_FirstAid" ) );
	m_slots[2] = dynamic_cast<CIconPanel *>( FindChildByName( "IconForSlot_Grenade" ) );
}
void SurvivorStatsPanel::SetSelected( bool selected ) { SelectStatsPanel( this, selected ); }
#pragma float_control( precise, on, push )
void SurvivorStatsPanel::UpdateStats( KeyValues *data )
{
	const bool incapacitated = data->GetInt( "isIncapacitated" ) == 1, alive = data->GetBool( "isAlive" ), scoreboard = data->GetBool( "isScoreboard" );
	const Color color = alive ? Color(255,255,255,255) : Color(128,128,128,255);
	m_playerIndex = data->GetInt( "playerindex" );
	C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( m_playerIndex ) );
	bool avatar = player && !player->IsBot();
	player_info_t info;
	if ( engine && engine->GetPlayerInfo( m_playerIndex, &info ) ) avatar = !info.fakeplayer;
	C_SurvivorBot *bot = dynamic_cast<C_SurvivorBot *>( player );
	int avatarPlayer = m_playerIndex;
	if ( bot && bot->GetHumanSpectatorEntIndex() > 0 ) { avatar = true; avatarPlayer = bot->GetHumanSpectatorEntIndex(); }
	if ( m_avatar ) { m_avatar->SetPlayer( avatarPlayer ); m_avatar->SetVisible( avatar ); m_avatar->SetAvatarColor( color ); }
	for ( int i = 0; i < 2; ++i ) if ( m_name[i] ) { m_name[i]->SetFgColor( color ); m_name[i]->SetText( data->GetString( "name", "" ) ); m_name[i]->SetVisible( i == 0 ? avatar : !avatar ); }
	if ( m_head ) { m_head->SetDrawColor( color ); m_head->SetImage( GetTerrorSurvivorPortrait( data->GetInt( "character" ) ) ); }
	const int ping = data->GetInt( "ping" );
	if ( m_pingImage ) { m_pingImage->SetVisible( ping > 0 ); m_pingImage->SetText( ping <= 70 ? "O" : ping <= 140 ? "N" : "M" ); }
	if ( m_pingLabel ) { char text[8]; Q_snprintf( text, sizeof( text ), "%d", ping ); m_pingLabel->SetVisible( ping > 0 ); m_pingLabel->SetText( text ); }
	const bool idle = bot && bot->GetHumanSpectatorUserID() > 0;
	const char *status = "";
	if ( data->GetString( "customstatus", NULL ) ) status = data->GetString( "customstatus", "" );
	else if ( idle ) status = "#L4D_Scoreboard_Idle";
	else if ( !alive ) status = "#L4D_Scoreboard_Dead";
	else if ( incapacitated ) status = "#L4D_Scoreboard_Incapacitated";
	for ( int i = 0; i < 2; ++i ) if ( m_status[i] ) { m_status[i]->SetFgColor( color ); m_status[i]->SetVisible( i == 0 ? avatar : !avatar ); m_status[i]->SetText( status ); }
	if ( m_health ) m_health->SetHealth( alive ? data->GetInt( "health" ) : 0, alive ? data->GetInt( "healthBuffer" ) : 0, data->GetInt( "maxHealth" ), alive && data->GetBool( "isGoingToDie" ), alive && data->GetBool( "isIncapacitated" ), false );
	const char *keys[] = { "pillsSlot", "firstAidSlot", "grenadeSlot" };
	for ( int i = 0; i < 3; ++i ) if ( m_slots[i] )
	{
		const int weapon = data->GetInt( keys[i] ); m_slots[i]->SetVisible( alive && weapon != 0 );
		const CTerrorWeaponInfo *info = GetTerrorWeaponInfo( weapon ); m_slots[i]->SetIcon( info ? info->iconSmall : NULL );
	}
	if ( m_voice && player )
	{
		const int speaker = idle ? bot->GetHumanSpectatorEntIndex() : m_playerIndex;
		C_BasePlayer *speakingPlayer = UTIL_PlayerByIndex( speaker ); CVoiceStatus *voice = GetClientVoiceMgr();
		m_voice->SetVisible( voice && ( speakingPlayer && speakingPlayer->IsLocalPlayer() ? voice->IsLocalPlayerSpeaking() : speaker > 0 && voice->IsPlayerSpeaking( speaker ) ) );
	}
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR );
	if ( !TerrorGameRules() || !TerrorGameRules()->IsSurvivalMode() || !scoreboard || !resource )
	{
		if ( m_record ) m_record->SetVisible( false ); if ( m_recordImage ) m_recordImage->SetVisible( false );
		return;
	}
	if ( !player ) return;
	if ( player->IsBot() && !idle )
	{
		if ( m_record ) m_record->SetVisible( false ); if ( m_recordImage ) m_recordImage->SetVisible( false );
		return;
	}
	const int recordPlayer = idle ? bot->GetHumanSpectatorEntIndex() : m_playerIndex;
	if ( recordPlayer < 0 || recordPlayer >= MAX_PLAYERS_ARRAY_SAFE ) return;
	if ( m_record )
	{
		float time = resource->GetSurvivalRecordTime( recordPlayer ); if ( time <= 0.0f ) time = 0.0f;
		int minutes, seconds, hundredths; SplitTerrorSurvivalTime( time, minutes, seconds, hundredths );
		char text[32]; Q_snprintf( text, sizeof( text ), "%02d:%02d.%02d", minutes, seconds, hundredths ); m_record->SetText( text ); m_record->SetVisible( true );
	}
	if ( m_recordImage )
	{
		const int medal = resource->GetSurvivalTopMedal( recordPlayer );
		m_recordImage->SetIcon( medal == 0 ? "icon_bronze_medal_small" : medal == 1 ? "icon_silver_medal_small" : medal == 2 ? "icon_gold_medal_small" : "icon_player_record_small" ); m_recordImage->SetVisible( true );
	}
}
#pragma float_control( pop )
