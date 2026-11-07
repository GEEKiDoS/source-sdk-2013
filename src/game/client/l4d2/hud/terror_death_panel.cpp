//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_death_panel.h"
#include "l4d2/terror_native_qsort.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/clientmode_terror.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui_avatarimage.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/MessageMap.h"
#include "tier0/memdbgon.h"
DECLARE_BUILD_FACTORY( DeathPanel );
static ConVar cl_show_tickets( "cl_show_tickets", "0", 0x0000000A ); // Native raw0 -> effective2, no bounds/help.
struct DeathTicketEntry_t { const char *name; int score, tickets, index; bool local; };
static int CompareDeathTickets( const void *left, const void *right )
{
	const DeathTicketEntry_t &a = *static_cast<const DeathTicketEntry_t *>( left );
	const DeathTicketEntry_t &b = *static_cast<const DeathTicketEntry_t *>( right );
	return a.score == b.score ? a.index < b.index : a.score < b.score;
}
DeathPanel::DeathPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name ), m_data( NULL ), m_lastRefresh( -1.0f ) {}
DeathPanel::~DeathPanel()
{
	if ( m_data ) { m_data->deleteThis(); m_data = NULL; }
	ClientModeTerrorNormal *mode = static_cast<ClientModeTerrorNormal *>( GetClientModeNormal() );
	if ( mode ) mode->RemovePanelOpenFlag( 1 );
}
void DeathPanel::SetVisible( bool state )
{
	if ( BaseClass::IsVisible() == state ) return;
	BaseClass::SetVisible( state );
	ClientModeTerrorNormal *mode = static_cast<ClientModeTerrorNormal *>( GetClientModeNormal() );
	if ( mode ) { if ( state ) mode->AddPanelOpenFlag( 1 ); else mode->RemovePanelOpenFlag( 1 ); }
}
void DeathPanel::SetDeathStats( KeyValues *data )
{
	if ( m_data ) { m_data->deleteThis(); m_data = NULL; }
	if ( data ) m_data = data->MakeCopy(); InvalidateLayout( true, true );
}
void DeathPanel::SetStatistic( const char *control, const char *key )
{
	if ( !m_data ) return;
	char text[32]; Q_snprintf( text, sizeof( text ), "%d", m_data->GetInt( key ) ); SetControlString( control, text );
}
void DeathPanel::UpdateTickets()
{
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR );
	if ( !resource || !C_BasePlayer::GetLocalPlayer() || !IsVisible() ) return;
	if ( m_lastRefresh > 0.0f && gpGlobals->curtime - m_lastRefresh < 1.0f ) return;
	CUtlVectorFixed<DeathTicketEntry_t,33> entries;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		if ( i >= MAX_PLAYERS_ARRAY_SAFE ) continue;
		if ( !resource->IsConnected( i ) || resource->GetTeam( i ) != 3 || resource->IsFakePlayer( i ) ) continue;
		DeathTicketEntry_t entry = { resource->GetPlayerName( i ), resource->GetPlayerScore( i ), resource->GetTankTickets( i ), i, resource->IsLocalPlayer( i ) };
		entries.AddToTail( entry );
	}
	TerrorNativeQSort( entries.Base(), entries.Count(), sizeof( DeathTicketEntry_t ), CompareDeathTickets ); SetControlVisible( "TicketsTitle", cl_show_tickets.GetBool() );
	FOR_EACH_VEC( entries, i )
	{
		const DeathTicketEntry_t &entry = entries[i]; char control[64], text[32];
		Q_snprintf( control, sizeof( control ), "PZBackground%d", i+1 ); SetControlVisible( control, entry.local );
		Q_snprintf( control, sizeof( control ), "PZAvatar%d", i+1 );
		CAvatarImagePanel *avatar = dynamic_cast<CAvatarImagePanel *>( FindChildByName( control ) ); if ( avatar ) { avatar->SetVisible( true ); avatar->SetPlayer( entry.index ); }
		SetControlVisible( control, true );
		Q_snprintf( control, sizeof( control ), "PZName%d", i+1 ); SetControlVisible( control, true ); SetControlString( control, entry.name );
		Q_snprintf( control, sizeof( control ), "PZPoints%d", i+1 ); Q_snprintf( text, sizeof( text ), "%d", entry.score ); SetControlVisible( control, true ); SetControlString( control, text );
		Q_snprintf( control, sizeof( control ), "PZTankTickets%d", i+1 ); Q_snprintf( text, sizeof( text ), "%d", entry.tickets ); SetControlVisible( control, cl_show_tickets.GetBool() ); SetControlString( control, text );
		Q_snprintf( control, sizeof( control ), "PZLine%d", i+2 ); SetControlVisible( control, true );
	}
	for ( int i = entries.Count() + 1; i <= 4; ++i )
	{
		const char *prefix[] = { "PZBackground", "PZAvatar", "PZName", "PZPoints", "PZTankTickets" }; char control[64];
		for ( int j = 0; j < ARRAYSIZE( prefix ); ++j ) { Q_snprintf( control, sizeof( control ), "%s%d", prefix[j], i ); SetControlVisible( control, false ); }
		Q_snprintf( control, sizeof( control ), "PZLine%d", i+1 ); SetControlVisible( control, false );
	}
}
void DeathPanel::ShowStatistics()
{
	if ( !g_PR || !C_BasePlayer::GetLocalPlayer() || !m_data ) return;
	SetVisible( true ); m_lastRefresh = -1.0f; UpdateTickets();
	SetStatistic( "Kills", "kills" ); SetStatistic( "Incaps", "incaps" ); SetStatistic( "TotalPoints", "points" );
	const char *names[] = { "Bill", "Zoey", "Francis", "Louis" }, *keys[] = { "dmgBill", "dmgZoey", "dmgFrancis", "dmgLouis" };
	uint32 total = 0;
	for ( int character = 0; character < 4; ++character )
	{
		C_TerrorPlayer *found = NULL;
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			if ( i >= MAX_PLAYERS_ARRAY_SAFE || !g_PR->IsConnected( i ) ) continue;
			C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( player && !player->IsDormant() && ( player->GetTeamNumber() == 2 || player->GetTeamNumber() == 4 ) && player->GetSurvivorCharacter() == character ) { found = player; break; }
		}
		char title[64], damage[64]; Q_snprintf( title, sizeof( title ), "%sTitle", names[character] ); Q_snprintf( damage, sizeof( damage ), "%sDamage", names[character] );
		if ( found ) { SetControlString( title, found->GetPlayerName() ); SetStatistic( damage, keys[character] ); total += uint32( m_data->GetInt( keys[character] ) ); }
		SetControlVisible( title, found != NULL ); SetControlVisible( damage, found != NULL );
	}
	char damage[32]; Q_snprintf( damage, sizeof( damage ), "%d", int32( total ) ); SetControlString( "DamageDealt", damage );
}
void DeathPanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "resource/UI/DeathInfected.res" ); BaseClass::ApplySchemeSettings( scheme );
	const int zombie = m_data ? m_data->GetInt( "class" ) : 0;
	if ( zombie != 1 && zombie != 2 && zombie != 3 && zombie != 8 ) { SetVisible( false ); return; }
	ShowStatistics();
	static const char *common[] = { "#L4D_Hint_pz_falling", "#L4D_Hint_pz_glows", "#L4D_Hint_pz_ledge_hanger", "#L4D_Hint_pz_bash", "#L4D_Hint_pz_break_doors" };
	static const char *smoker[] = { "#L4D_Hint_smoker_choke", "#L4D_Hint_smoker_choke_committed", "#L4D_Hint_smoker_death_cloud", "#L4D_Hint_smoker_poison" };
	static const char *boomer[] = { "#L4D_Hint_boomer_slow", "#L4D_Hint_boomer_pop", "#L4D_Hint_boomer_bomb", "#L4D_Hint_boomer_vomit", "#L4D_Hint_boomer_vomit_blind", "#L4D_Hint_boomer_it", "#L4D_Hint_boomer_recharge", "#L4D_Hint_boomer_vomit_no_move" };
	static const char *hunter[] = { "#L4D_Hint_hunter_leap", "#L4D_Hint_hunter_leap_aim", "#L4D_Hint_hunter_push", "#L4D_Hint_hunter_wallkick", "#L4D_Hint_hunter_pounce", "#L4D_Hint_hunter_pounce_committed", "#L4D_Hint_hunter_pounce_defense", "#L4D_Hint_hunter_pounce_incaps" };
	static const char *tank[] = { "#L4D_Hint_tank_health", "#L4D_Hint_tank_self_control", "#L4D_Hint_tank_throw", "#L4D_Hint_tank_attack_incaps", "#L4D_Hint_tank_break_walls", "#L4D_Hint_tank_punch_cars" };
	const char **specific = zombie == 1 ? smoker : zombie == 2 ? boomer : zombie == 3 ? hunter : tank;
	const int count = zombie == 1 ? ARRAYSIZE(smoker) : zombie == 2 ? ARRAYSIZE(boomer) : zombie == 3 ? ARRAYSIZE(hunter) : ARRAYSIZE(tank);
	SetControlString( "AsA", zombie == 1 ? "#L4D_pz_AsASmoker" : zombie == 2 ? "#L4D_pz_AsABoomer" : zombie == 3 ? "#L4D_pz_AsAHunter" : "#L4D_pz_AsATank" );
	SetControlString( "Ability1Title", zombie == 1 ? "#L4D_pz_HangCount" : zombie == 2 ? "#L4D_pz_VomitCount" : zombie == 3 ? "#L4D_pz_PounceCount" : "#L4D_pz_TankPunchCount" );
	SetControlString( "Ability2Title", zombie == 1 ? "#L4D_pz_PullCount" : zombie == 2 ? "#L4D_pz_BoomerBombCount" : zombie == 3 ? "#L4D_pz_LungeCount" : "#L4D_pz_TankThrowCount" );
	SetStatistic( "Ability1", zombie == 1 ? "chokes" : zombie == 2 ? "vomits" : zombie == 3 ? "pounces" : "claws" );
	SetStatistic( "Ability2", zombie == 1 ? "pulls" : zombie == 2 ? "splats" : zombie == 3 ? "lunges" : "throws" );
	const int hint = RandomInt( 0, ARRAYSIZE(common) + count - 1 );
	const char *token = hint < ARRAYSIZE(common) ? common[hint] : specific[hint - ARRAYSIZE(common)];
	const wchar_t *localized = g_pVGuiLocalize->Find( token ); if ( localized ) SetControlString( "HintText", localized );
	SetControlVisible( "HintText", true );
}
void DeathPanel::OnThink()
{
	UpdateTickets();
	if ( IsVisible() )
	{
		ClientModeTerrorNormal *mode = static_cast<ClientModeTerrorNormal *>( GetClientModeNormal() );
		if ( mode ) mode->NotifyDeathPanelVisible();
	}
}
