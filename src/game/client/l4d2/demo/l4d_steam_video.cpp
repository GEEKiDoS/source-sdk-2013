//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/demo/l4d_steam_video.h"
#include "l4d2/player/c_terrorplayer.h"
#include "c_playerresource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/ui/terror_client_panels.h"
#include "steam/steam_api.h"
#include "steam/isteamtimeline.h"
#include <vgui/ILocalize.h>
#include "tier0/memdbgon.h"

// C263DD0..C265767. This is the native L4DSteamVideo owner, not an
// achievement, music listener, match-framework sink, or second stats collector.
namespace
{
C_TerrorPlayer *TimelinePlayer( IGameEvent *event, const char *field )
{
	return ToTerrorPlayer( UTIL_PlayerByUserId( event->GetInt( field ) ) );
}
C_BasePlayer *TimelineViewPlayer()
{
	// C64300: observer modes 4 and 5 only; invalid/non-player targets retain local.
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	if ( local && ( local->GetObserverMode() == OBS_MODE_IN_EYE || local->GetObserverMode() == OBS_MODE_CHASE ) )
	{
		C_BaseEntity *target = local->GetObserverTarget();
		if ( target && target->IsPlayer() ) return ToBasePlayer( target );
	}
	return local;
}
bool TimelineInvolvesLocal( C_TerrorPlayer *first, C_TerrorPlayer *second )
{
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
	return local == first || local == second;
}
bool TimelineShouldShow( C_TerrorPlayer *first, C_TerrorPlayer *second )
{
	C_BasePlayer *view = TimelineViewPlayer();
	static ConVarRef timeline_show_all_events( "timeline_show_all_events" );
	return view == first || view == second || timeline_show_all_events.GetBool();
}
int TimelinePriority( C_TerrorPlayer *first, C_TerrorPlayer *second )
{
	return TimelineInvolvesLocal( first, second ) ? 1000 : 1;
}
bool TimelineIsBotInfected( C_TerrorPlayer *player )
{
	return player && g_PR->IsFakePlayer( player->entindex() ) && player->GetTeamNumber() == TEAM_INFECTED;
}
const char *TimelinePlayerName( C_TerrorPlayer *player )
{
	// C264530. Bot infected use the class name, not the bot's player name.
	if ( !player ) return "";
	if ( TimelineIsBotInfected( player ) )
	{
		static const char *const names[] = { "Common", "Smoker", "Boomer", "Hunter", "Spitter", "Jockey", "Charger", "Witch", "Tank" };
		const int zombieClass = player->GetZombieClass();
		return (unsigned int)zombieClass < ARRAYSIZE( names ) ? names[zombieClass] : "";
	}
	return player->GetPlayerName();
}
const char *TimelineCause( C_TerrorPlayer *attacker, C_BaseEntity *attackerEntity )
{
	// C264590; the original shared 32-byte buffer is deliberately retained.
	static char cause[32];
	if ( attacker )
	{
		Q_strncpy( cause, TimelinePlayerName( attacker ), sizeof( cause ) );
		return cause;
	}
	const char *token = "#L4D_Timeline_Recklessness";
	if ( attackerEntity && attackerEntity->MyCombatCharacterPointer() )
	{
		const int zombieClass = attackerEntity->MyCombatCharacterPointer()->GetZombieClass();
		if ( zombieClass == 7 ) token = "#L4D_Timeline_Witch";
		else if ( zombieClass == 0 ) token = "#L4D_Timeline_Common";
	}
	g_pVGuiLocalize->ConvertUnicodeToANSI( g_pVGuiLocalize->Find( token ), cause, sizeof( cause ) );
	return cause;
}
const char *TimelineText( const char *token, const char *first = NULL, const char *second = NULL )
{
	// C2641D0: two zeroed 32-wide-character parameter buffers, one shared result.
	static char text[256];
	wchar_t firstWide[32] = {}, secondWide[32] = {}, formatted[256];
	int count = 0;
	if ( first ) { g_pVGuiLocalize->ConvertANSIToUnicode( first, firstWide, sizeof( firstWide ) ); ++count; }
	if ( second ) { g_pVGuiLocalize->ConvertANSIToUnicode( second, secondWide, sizeof( secondWide ) ); ++count; }
	const wchar_t *wide;
	if ( count )
	{
		g_pVGuiLocalize->ConstructString( formatted, sizeof( formatted ), g_pVGuiLocalize->Find( token ), count, firstWide, secondWide );
		wide = formatted;
	}
	else wide = TerrorFindLocalizedTextSafe( token );
	g_pVGuiLocalize->ConvertUnicodeToANSI( wide, text, sizeof( text ) );
	return text;
}
const char *TimelineStateText( const char *token, const char *name = NULL )
{
	// C2642E0 uses a DIFFERENT shared result from C2641D0.
	static char text[256];
	wchar_t nameWide[32] = {}, formatted[256];
	const wchar_t *wide;
	if ( name )
	{
		g_pVGuiLocalize->ConvertANSIToUnicode( name, nameWide, sizeof( nameWide ) );
		g_pVGuiLocalize->ConstructString( formatted, sizeof( formatted ), g_pVGuiLocalize->Find( token ), 1, nameWide );
		wide = formatted;
	}
	else wide = TerrorFindLocalizedTextSafe( token );
	g_pVGuiLocalize->ConvertUnicodeToANSI( wide, text, sizeof( text ) );
	return text;
}
const char *TimelinePairText( const char *format, C_TerrorPlayer *first, C_TerrorPlayer *second, const char *firstName = NULL )
{
	// C264660. Native bot suffix priority is initiator before receiver.
	const bool firstBot = first && first->GetTeamNumber() == TEAM_INFECTED && g_PR->IsFakePlayer( first->entindex() );
	const bool secondBot = second && second->GetTeamNumber() == TEAM_INFECTED && g_PR->IsFakePlayer( second->entindex() );
	char token[256];
	if ( TimelineInvolvesLocal( first, second ) )
	{
		const bool isInitiator = C_BasePlayer::GetLocalPlayer() == first;
		const char *suffix = isInitiator ? "Initiator" : "Receiver";
		if ( firstBot ) suffix = "Bot_Initiator";
		else if ( secondBot ) suffix = "Bot_Receiver";
		Q_snprintf( token, sizeof( token ), format, suffix );
		return TimelineText( token, isInitiator ? TimelinePlayerName( second ) : ( firstName ? firstName : TimelinePlayerName( first ) ) );
	}
	const char *suffix = firstBot ? "PVP_Bot_Initiator" : secondBot ? "PVP_Bot_Receiver" : "PVP";
	Q_snprintf( token, sizeof( token ), format, suffix );
	return TimelineText( token, firstName ? firstName : TimelinePlayerName( first ), TimelinePlayerName( second ) );
}
void TimelinePairEvent( IGameEvent *event, const char *firstField, const char *secondField, const char *icon, const char *format )
{
	C_TerrorPlayer *first = TimelinePlayer( event, firstField );
	C_TerrorPlayer *second = TimelinePlayer( event, secondField );
	if ( first && second && TimelineShouldShow( first, second ) )
		SteamTimeline()->AddTimelineEvent( icon, TimelinePairText( format, first, second ), "", TimelinePriority( first, second ), 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
}
void TimelineSetChapterDescription()
{
	// C264390; uses the ONE Main-approved mounted mission/chapter projection.
	KeyValues *mission = NULL;
	KeyValues *chapter = TerrorClient_GetTransitionChapterInfo( false, &mission );
	wchar_t missionWide[128], chapterWide[128], combined[128];
	const wchar_t *missionText = NULL, *chapterText = NULL;
	if ( mission )
	{
		const char *title = mission->GetString( "DisplayTitle", "" );
		missionText = g_pVGuiLocalize->Find( title );
		if ( !missionText ) { g_pVGuiLocalize->ConvertANSIToUnicode( title, missionWide, sizeof( missionWide ) ); missionText = missionWide; }
	}
	if ( chapter )
	{
		const char *title = chapter->GetString( "DisplayName", "" );
		chapterText = g_pVGuiLocalize->Find( title );
		if ( !chapterText ) { g_pVGuiLocalize->ConvertANSIToUnicode( title, chapterWide, sizeof( chapterWide ) ); chapterText = chapterWide; }
	}
	const wchar_t *wide = L"";
	if ( missionText )
	{
		wide = missionText;
		if ( chapterText ) { g_pVGuiLocalize->ConstructString( combined, sizeof( combined ), g_pVGuiLocalize->Find( "#L4D_Timeline_State" ), 2, missionText, chapterText ); wide = combined; }
	}
	char description[128];
	g_pVGuiLocalize->ConvertUnicodeToANSI( wide, description, sizeof( description ) );
	SteamTimeline()->SetTimelineStateDescription( description, 0.0f );
}
}

static L4DSteamVideo g_L4DSteamVideo; // Original sole static owner; zero-initializes m_enabled.

L4DSteamVideo::L4DSteamVideo() : CAutoGameSystem( "L4DSteamVideo" )
{
	// The sole parent-owned static instance supplies native zero-initialization.
}

bool L4DSteamVideo::Init()
{
	ISteamTimeline *timeline = SteamTimeline();
	if ( timeline )
	{
		timeline->SetTimelineGameMode( k_ETimelineGameMode_Staging );
		static const char *const events[] =
		{
			"player_connect_full", "vote_passed", "player_incapacitated", "revive_success", "heal_success", "player_death",
			"defibrillator_used", "survivor_rescued", "witch_killed", "tank_spawn", "tank_killed", "choke_start", "lunge_pounce",
			"jockey_ride", "charger_carry_start", "player_now_it", "versus_round_start", "versus_match_finished", "player_left_start_area",
			"finale_start", "finale_vehicle_ready", "finale_vehicle_leaving", "round_start", "round_end", "survival_round_start",
			"survival_goal_reached", "scavenge_round_start", "scavenge_round_halftime", "scavenge_match_finished", "begin_scavenge_overtime", "gascan_pour_completed"
		};
		for ( int i = 0; i < ARRAYSIZE( events ); ++i ) ListenForGameEvent( events[i] );
	}
	m_enabled = timeline != NULL;
	return true;
}
void L4DSteamVideo::Shutdown()
{
	StopListeningForAllEvents();
	m_enabled = false;
}
void L4DSteamVideo::FireGameEvent( IGameEvent *event )
{
	if ( !m_enabled ) return;
	const char *name = event->GetName();
	ISteamTimeline *timeline = SteamTimeline();
	if ( !Q_strcmp( name, "player_incapacitated" ) )
	{
		C_TerrorPlayer *attacker = TimelinePlayer( event, "attacker" );
		C_TerrorPlayer *victim = TimelinePlayer( event, "userid" );
		if ( victim && victim->GetTeamNumber() == TEAM_SURVIVOR )
		{
			const char *cause = TimelineCause( attacker, cl_entitylist->GetEnt( event->GetInt( "attackerentid" ) ) );
			timeline->AddTimelineEvent( "steam_invalid", TimelinePairText( "#L4D_Timeline_Incap_%s", attacker, victim, cause ), "", TimelinePriority( attacker, victim ), 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		}
		return;
	}
	if ( !Q_strcmp( name, "revive_success" ) ) { TimelinePairEvent( event, "userid", "subject", "steam_wrench", "#L4D_Timeline_Revive_%s" ); return; }
	if ( !Q_strcmp( name, "heal_success" ) )
	{
		C_TerrorPlayer *healer = TimelinePlayer( event, "userid" ), *subject = TimelinePlayer( event, "subject" );
		if ( TimelineShouldShow( healer, subject ) )
		{
			const char *title;
			if ( TimelineInvolvesLocal( healer, subject ) ) title = healer == subject ? TimelineText( "#L4D_Timeline_Healed_Self" ) : TimelinePairText( "#L4D_Timeline_Healed_%s", healer, subject );
			else if ( healer == subject ) title = TimelineText( "#L4D_Timeline_Healed_Self_Other", TimelinePlayerName( healer ) );
			else title = TimelineText( "#L4D_Timeline_Healed_PVP", TimelinePlayerName( healer ), TimelinePlayerName( subject ) );
			timeline->AddTimelineEvent( "steam_heart", title, "", 1000, -5.0f, 5.0f, k_ETimelineEventClipPriority_Standard );
		}
		return;
	}
	if ( !Q_strcmp( name, "player_death" ) )
	{
		C_TerrorPlayer *attacker = TimelinePlayer( event, "attacker" ), *victim = TimelinePlayer( event, "userid" );
		if ( !victim ) return;
		const ETimelineEventClipPriority clip = event->GetBool( "victimisbot" ) ? k_ETimelineEventClipPriority_Standard : k_ETimelineEventClipPriority_Featured;
		const char *icon = "steam_death";
		const char *cause = TimelineCause( attacker, cl_entitylist->GetEnt( event->GetInt( "attackerentid" ) ) );
		const char *title = TimelinePairText( "#L4D_Timeline_Kill_%s", attacker, victim, cause );
		const int victimTeam = victim->GetTeamNumber();
		if ( victimTeam != TEAM_SURVIVOR && victimTeam != TEAM_L4D1_SURVIVOR )
		{
			icon = "steam_attack";
			if ( victim->GetZombieClass() == 8 ) title = TimelineText( "#L4D_Timeline_Kill_Tank" );
			else
			{
				if ( !TimelineShouldShow( attacker, victim ) ) return;
				if ( victim->GetZombieClass() == 2 ) icon = "steam_explosion";
			}
		}
		timeline->AddTimelineEvent( icon, title, "", TimelinePriority( attacker, victim ), 0.0f, 0.0f, clip );
		return;
	}
	if ( !Q_strcmp( name, "defibrillator_used" ) ) { TimelinePairEvent( event, "userid", "subject", "steam_bolt", "#L4D_Timeline_Defib_%s" ); return; }
	if ( !Q_strcmp( name, "survivor_rescued" ) ) { TimelinePairEvent( event, "rescuer", "victim", "steam_effect", "#L4D_Timeline_Rescue_%s" ); return; }
	if ( !Q_strcmp( name, "witch_killed" ) )
	{
		C_TerrorPlayer *player = TimelinePlayer( event, "userid" );
		if ( player )
		{
			const char *title = C_BasePlayer::GetLocalPlayer() == player ? TimelineText( "#L4D_Timeline_Kill_Witch" ) : TimelineText( "#L4D_Timeline_Kill_Witch_Other", TimelinePlayerName( player ) );
			timeline->AddTimelineEvent( "steam_attack", title, "", TimelinePriority( player, NULL ), 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		}
		return;
	}
	if ( !Q_strcmp( name, "choke_start" ) ) { TimelinePairEvent( event, "userid", "victim", "steam_combat", "#L4D_Timeline_Choke_%s" ); return; }
	if ( !Q_strcmp( name, "lunge_pounce" ) ) { TimelinePairEvent( event, "userid", "victim", "steam_combat", "#L4D_Timeline_Pounce_%s" ); return; }
	if ( !Q_strcmp( name, "jockey_ride" ) ) { TimelinePairEvent( event, "userid", "victim", "steam_combat", "#L4D_Timeline_Ride_%s" ); return; }
	if ( !Q_strcmp( name, "charger_carry_start" ) ) { TimelinePairEvent( event, "userid", "victim", "steam_combat", "#L4D_Timeline_Carry_%s" ); return; }
	if ( !Q_strcmp( name, "player_now_it" ) ) { TimelinePairEvent( event, "attacker", "userid", "steam_combat", "#L4D_Timeline_Bile_%s" ); return; }
	if ( !Q_strcmp( name, "tank_spawn" ) ) { timeline->AddTimelineEvent( "steam_caution", "", "#L4D_Timeline_Tank_Spawn", 1000, 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard ); return; }
	if ( !Q_strcmp( name, "gascan_pour_completed" ) )
	{
		if ( !TerrorGameRules() ) return;
		C_TerrorPlayer *player = TimelinePlayer( event, "userid" );
		if ( !player ) return;
		const int goal = TerrorGameRules()->GetScavengeItemsGoal();
		const int poured = static_cast< int >( static_cast< unsigned int >( TerrorGameRules()->GetScavengeTeamScore( TEAM_SURVIVOR, -1 ) ) + 1u );
		const int remainingCount = static_cast< int >( static_cast< unsigned int >( goal ) - static_cast< unsigned int >( poured ) );
		char remaining[12], icon[256];
		Q_snprintf( remaining, sizeof( remaining ), "%d", MAX( remainingCount, 0 ) );
		const char *description = TimelineText( "#L4D_Timeline_GasCan_Poured_Desc", remaining );
		const char *title = C_BasePlayer::GetLocalPlayer() == player ? TimelineStateText( "#L4D_Timeline_GasCan_Poured_Title" ) : TimelineStateText( "#L4D_Timeline_GasCan_Poured_Title_Other", player->GetPlayerName() );
		Q_snprintf( icon, sizeof( icon ), "steam_%d", poured );
		timeline->AddTimelineEvent( icon, title, description, TimelinePriority( player, NULL ), 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		return;
	}
	if ( !Q_strcmp( name, "survival_goal_reached" ) )
	{
		const char *goal = event->GetString( "goalname", "" );
		const char *token = "#L4D_Timeline_Survival_Goal_Reached", *icon = "steam_flag";
		if ( !Q_strcmp( goal, "medal" ) ) { token = "#L4D_Timeline_Survival_Medal_Reached"; icon = "steam_achievement"; }
		else if ( Q_strcmp( goal, "nomedal" ) ) { token = "#L4D_Timeline_Survival_Record_Reached"; icon = "steam_trophy"; }
		timeline->AddTimelineEvent( icon, TimelineText( token ), "", 1000, 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		return;
	}
	if ( !Q_strcmp( name, "vote_passed" ) )
	{
		char title[64], description[128];
		g_pVGuiLocalize->ConvertUnicodeToANSI( TerrorFindLocalizedTextSafe( "#L4d_vote_vote_passed" ), title, sizeof( title ) );
		const char *parameter = event->GetString( "param1", "" );
		const char *details = event->GetString( "details", "" );
		wchar_t formatted[512], parameterWide[128];
		const wchar_t *wide;
		if ( Q_strlen( parameter ) > 0 )
		{
			const wchar_t *parameterText;
			if ( parameter[0] == '#' ) parameterText = g_pVGuiLocalize->Find( parameter );
			else { g_pVGuiLocalize->ConvertANSIToUnicode( parameter, parameterWide, sizeof( parameterWide ) ); parameterText = parameterWide; }
			g_pVGuiLocalize->ConstructString( formatted, sizeof( formatted ), g_pVGuiLocalize->Find( details ), 1, parameterText );
			wide = formatted;
		}
		else wide = g_pVGuiLocalize->Find( details );
		g_pVGuiLocalize->ConvertUnicodeToANSI( wide, description, sizeof( description ) );
		timeline->AddTimelineEvent( "steam_completed", title, description, 0, 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		return;
	}
	if ( !Q_strcmp( name, "player_connect_full" ) || !Q_strcmp( name, "player_disconnect" ) )
	{
		C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
		if ( local && local->GetUserID() == event->GetInt( "userid" ) ) TimelineSetChapterDescription();
		return;
	}
	if ( !Q_strcmp( name, "versus_round_start" ) )
	{
		timeline->SetTimelineGameMode( k_ETimelineGameMode_Playing );
		timeline->SetTimelineStateDescription( TimelineStateText( "#L4D_Timeline_State_Versus_Round_Active" ), 0.0f );
		return;
	}
	if ( !Q_strcmp( name, "versus_match_finished" ) || !Q_strcmp( name, "round_end" ) )
	{
		timeline->SetTimelineGameMode( k_ETimelineGameMode_Staging );
		timeline->ClearTimelineStateDescription( 0.0f );
		return;
	}
	if ( !Q_strcmp( name, "player_left_start_area" ) || !Q_strcmp( name, "round_start" ) ) { timeline->SetTimelineGameMode( k_ETimelineGameMode_Playing ); return; }
	if ( !Q_strcmp( name, "finale_start" ) ) { timeline->SetTimelineStateDescription( TimelineStateText( "#L4D_Timeline_State_Finale" ), 0.0f ); return; }
	if ( !Q_strcmp( name, "finale_vehicle_ready" ) ) { timeline->SetTimelineStateDescription( TimelineStateText( "#L4D_Timeline_State_Finale_Vehicle_Ready" ), 0.0f ); return; }
	if ( !Q_strcmp( name, "finale_vehicle_leaving" ) )
	{
		char text[128];
		g_pVGuiLocalize->ConvertUnicodeToANSI( TerrorFindLocalizedTextSafe( "#L4D_Timeline_State_Finale_Vehicle_Leaving" ), text, sizeof( text ) );
		timeline->SetTimelineStateDescription( text, 0.0f );
		return;
	}
	if ( !Q_strcmp( name, "survival_round_start" ) || !Q_strcmp( name, "scavenge_round_start" ) )
	{
		const bool survival = !Q_strcmp( name, "survival_round_start" );
		timeline->SetTimelineGameMode( k_ETimelineGameMode_Playing );
		timeline->SetTimelineStateDescription( TimelineText( survival ? "#L4D_Timeline_State_Survival_Round_Active" : "#L4D_Timeline_State_Scavenge_Round_Active" ), 0.0f );
		timeline->AddTimelineEvent( "steam_timer", TimelineStateText( survival ? "#L4D_Timeline_State_Survival_Round_Start" : "#L4D_Timeline_State_Scavenge_Round_Start" ), "", 1000, 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard );
		return;
	}
	if ( !Q_strcmp( name, "scavenge_round_halftime" ) ) { timeline->AddTimelineEvent( "steam_transfer", TimelineStateText( "#L4D_Timeline_Scavenge_Team_Swap" ), "", 1000, 0.0f, 0.0f, k_ETimelineEventClipPriority_Standard ); return; }
	if ( !Q_strcmp( name, "scavenge_match_finished" ) ) timeline->SetTimelineGameMode( k_ETimelineGameMode_Staging );
	// Native registers tank_killed and begin_scavenge_overtime but has no
	// corresponding callback branch. Do not invent timeline entries for them.
}