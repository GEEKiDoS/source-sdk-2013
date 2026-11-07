//========= Copyright Valve Corporation, All rights reserved. ============//
// CHudScavengeTimer C2BC3A0..2BCFF5; CHudSurvivalTimer C2C4020..2C5ACB.
#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "iclientmode.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/ui/terror_client_panels.h"
#include "l4d2/hud/terror_hud_round_timers.h"
#include "l4d2/hud/terror_hud_native_numeric.h"
#include "c_recipientfilter.h"
#include "engine/IEngineSound.h"
#include "game_controls/IconPanel.h"
#include "tier1/utlsortvector.h"
#include <vgui/ISurface.h>
#include <vgui_controls/EditablePanel.h>
#include <vgui_controls/Label.h>
#include <vgui_controls/CircularProgressBar.h>
#include <vgui_controls/AnimationController.h>
#include <vgui/ILocalize.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif
static ConVar hud_scavenge_timer_warning_range( "hud_scavenge_timer_warning_range", "10", 0x0000000A );
static ConVar hud_survival_target_time_range( "hud_survival_target_time_range", "5", 0x0000000A );
static void TimerSound( const char *name ) { CLocalPlayerFilter filter; C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, name ); }
static void TimerAnimation( const char *name ) { g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( name ); }
void SplitTerrorSurvivalTime( float time, int &minutes, int &seconds, int &hundredths )
{
	int whole = TerrorHudFtol( floor( (double)time ) );
	hundredths = TerrorHudFtol( floor( ( (double)time - (double)whole + (double)0.005f ) * 100.0 ) );
	if ( hundredths > 99 ) { whole = TerrorHudAdd32( whole, 1 ); hundredths = 0; }
	minutes = whole / 60; seconds = whole % 60;
}
static int TimerText( vgui::Label *label, float time )
{
	int minutes, seconds, hundredths; SplitTerrorSurvivalTime( time, minutes, seconds, hundredths ); if ( label ) { wchar_t text[32]; V_snwprintf( text, ARRAYSIZE( text ), L"%02d:%02d.%02d", minutes, seconds, hundredths ); label->SetText( text ); } return seconds;
}
class CHudScavengeTimer : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudScavengeTimer, vgui::EditablePanel );
public:
	CHudScavengeTimer( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudScavengeTimer" ), m_time( 0 ), m_score( 0 ), m_toBeat( 0 ), m_remaining( 0 ), m_warning( false ), m_overtime( false ), m_digits( NULL ), m_beatDigits( NULL ), m_beatLabel( NULL ) { SetHiddenBits( 0 ); SetPaintBackgroundEnabled( false ); }
	virtual void Init() OVERRIDE { ListenForGameEvent( "round_start" ); ListenForGameEvent( "round_end" ); ListenForGameEvent( "scavenge_round_start" ); ListenForGameEvent( "begin_scavenge_overtime" ); ListenForGameEvent( "player_connect_full" ); }
	virtual bool ShouldDraw() OVERRIDE { return TerrorGameRules() && TerrorGameRules()->IsScavengeMode() && !TerrorGameRules()->IsInIntro(); }
	virtual void Reset() OVERRIDE { const int score = TerrorGameRules() ? TerrorGameRules()->GetScavengeTeamScore( TEAM_INFECTED ) : 0; if ( score != m_toBeat ) { char text[12]; Q_snprintf( text, sizeof( text ), "%d", score ); SetControlString( "ScoreToBeatDigits", text ); } m_time = -1.0f; m_score = m_toBeat = -1; m_remaining = 0; m_warning = false; EndOvertime(); }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { const char *name = event->GetName(); if ( !Q_stricmp( name, "round_start" ) ) InvalidateLayout( true, true ); else if ( !Q_stricmp( name, "round_end" ) ) EndOvertime(); else if ( !Q_stricmp( name, "begin_scavenge_overtime" ) ) { m_overtime = true; TimerAnimation( "StartScavengeOvertime" ); } else if ( !Q_stricmp( name, "player_connect_full" ) ) { C_BasePlayer *p = UTIL_PlayerByUserId( event->GetInt( "userid" ) ); if ( p && p->IsLocalPlayer() ) InvalidateLayout( true, true ); } }
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE { LoadControlSettings( "Resource/UI/HUD/HudScavengeTimer.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetFgColor( GetSchemeColor( "FgColor", scheme ) ); SetBgColor( GetSchemeColor( "BgColor", scheme ) ); vgui::surface()->PrecacheFontCharacters( m_numberFont, L"0123456789:" ); m_digits = dynamic_cast<vgui::Label *>( FindChildByName( "CurrentScavengeTimeDigits" ) ); m_beatDigits = dynamic_cast<vgui::Label *>( FindChildByName( "ScoreToBeatDigits" ) ); m_beatLabel = dynamic_cast<vgui::Label *>( FindChildByName( "ScoreToBeatLabel" ) ); Reset(); }
	virtual void OnThink() OVERRIDE
	{
		CTerrorGameRules *rules = TerrorGameRules(); if ( !g_TerrorModes.IsBaseMode( "scavenge" ) ) return; const float time = rules ? rules->GetRoundTimeRemaining() : 0.0f; if ( rules && m_beatDigits && m_beatLabel ) { m_beatLabel->SetEnabled( rules->IsScavengeSecondHalf() ); m_beatDigits->SetEnabled( rules->IsScavengeSecondHalf() ); }
		if ( m_warning ) { if ( time <= 0.0f ) { m_warning = false; TimerSound( "Scavenge.zero_time" ); } } else if ( time != 0.0f && hud_scavenge_timer_warning_range.GetFloat() >= time ) m_warning = true;
		if ( time != m_time ) { if ( time > m_time && rules && rules->IsRoundInProgress() ) { m_warning = false; EndOvertime(); } m_time = time; TimerText( m_digits, time ); static int lastSecond = -1; const int second = TerrorHudFtol( floor( (double)time ) ) % 60; if ( m_warning && second != lastSecond ) { TimerAnimation( "FlashCurrentScavengeTime" ); TimerSound( "Survival.countdown" ); lastSecond = second; } }
		const int score = rules ? rules->GetScavengeTeamScore( TEAM_SURVIVOR ) : 0; if ( score != m_score ) { m_score = score; char text[12]; Q_snprintf( text, sizeof( text ), "%d", score ); SetControlString( "CurrentScoreDigits", text ); if ( score != 0 && ShouldDraw() ) TimerSound( "Scavenge.point_scored" ); }
		const int beat = rules ? rules->GetScavengeTeamScore( TEAM_INFECTED ) : 0; if ( beat != m_toBeat ) { m_toBeat = beat; char text[12]; Q_snprintf( text, sizeof( text ), "%d", beat ); SetControlString( "ScoreToBeatDigits", text ); } const int remaining = TerrorHudAdd32( score, rules ? rules->GetScavengeItemsRemaining() : 0 ); if ( remaining != m_remaining ) { m_remaining = remaining; char text[12]; Q_snprintf( text, sizeof( text ), "%d", remaining ); SetControlString( "ItemsRemainingDigits", text ); }
	}
private:
	void EndOvertime() { if ( m_overtime ) { m_overtime = false; TimerAnimation( "EndScavengeOvertime" ); } }
	float m_time; int m_score, m_toBeat, m_remaining; bool m_warning, m_overtime; vgui::Label *m_digits, *m_beatDigits, *m_beatLabel;
	CPanelAnimationVar( vgui::HFont, m_numberFont, "NumberFont", "HudNumbers" );
	CPanelAnimationVar( int, m_aliveY, "TimerYPosAlive", "30" ); CPanelAnimationVar( int, m_deadY, "TimerYPosDead", "80" );
};
DECLARE_HUDELEMENT( CHudScavengeTimer );

struct SurvivalGoal { int type; float time; int id; };
struct SurvivalGoalLess
{
	bool Less( const SurvivalGoal &a, const SurvivalGoal &b, void * ) { if ( b.time > a.time ) return true; if ( a.time != b.time ) return false; if ( a.type < b.type ) return true; C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); return b.type == 0 && local && local->GetUserID() == b.id; }
};
class CHudSurvivalTimer : public CHudElement, public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CHudSurvivalTimer, vgui::EditablePanel );
public:
	CHudSurvivalTimer( const char *name ) : CHudElement( name ), BaseClass( g_pClientMode->GetViewport(), "HudSurvivalTimer" ), m_time( 0 ), m_target( 0 ), m_previous( 0 ), m_best( 0 ), m_countdown( false ), m_initialized( false ), m_record( false ), m_at30( false ), m_at10( false ), m_goal( 0 ), m_icon( NULL ), m_digits( NULL ), m_targetDigits( NULL ), m_transition( NULL ), m_ring( NULL ), m_transitionX( 0 ), m_transitionY( 0 ) { SetHiddenBits( 0 ); SetPaintBackgroundEnabled( false ); }
	virtual void Init() OVERRIDE { ListenForGameEvent( "round_start" ); ListenForGameEvent( "player_connect_full" ); }
	virtual bool ShouldDraw() OVERRIDE { return TerrorGameRules() && g_TerrorModes.IsBaseMode( "survival" ); }
	virtual void FireGameEvent( IGameEvent *event ) OVERRIDE { if ( !Q_stricmp( event->GetName(), "round_start" ) ) { InvalidateLayout( true, true ); m_at30 = m_at10 = false; } else if ( !Q_stricmp( event->GetName(), "player_connect_full" ) ) { C_BasePlayer *p = UTIL_PlayerByUserId( event->GetInt( "userid" ) ); if ( p && p->IsLocalPlayer() ) InvalidateLayout( true, true ); } }
	virtual void Reset() OVERRIDE
	{
		m_time = m_target = m_best = -1.0f; m_previous = 0.0f; m_goal = -1; m_countdown = m_initialized = m_record = false; if ( m_ring ) m_ring->SetProgress( 1.0f ); CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return; if ( rules->IsRoundInProgress() ) { m_initialized = true; do { ReachGoal(); if ( m_record ) break; const SurvivalGoal *goal = Goal(); if ( !goal || !( rules->GetTeamRoundDuration( TEAM_SURVIVOR ) > goal->time ) ) break; } while ( true ); } else ShowBest();
	}
protected:
	virtual void ApplySchemeSettings( vgui::IScheme *scheme ) OVERRIDE
	{
		LoadControlSettings( "Resource/UI/HUD/HudSurvivalTimer.res", "GAME" ); BaseClass::ApplySchemeSettings( scheme ); SetFgColor( GetSchemeColor( "FgColor", scheme ) ); SetBgColor( GetSchemeColor( "BgColor", scheme ) ); vgui::surface()->PrecacheFontCharacters( m_numberFont, L"0123456789:" ); m_digits = dynamic_cast<vgui::Label *>( FindChildByName( "CurrentTimeDigits" ) ); m_targetDigits = dynamic_cast<vgui::Label *>( FindChildByName( "TargetTimeDigits" ) ); m_transition = dynamic_cast<vgui::Label *>( FindChildByName( "TargetTransition" ) ); m_ring = dynamic_cast<vgui::CircularProgressBar *>( FindChildByName( "Timer" ) ); m_icon = dynamic_cast<CIconPanel *>( FindChildByName( "GoalImage" ) ); Reset();
	}
	virtual void OnThink() OVERRIDE
	{
		CTerrorGameRules *rules = TerrorGameRules(); if ( !g_TerrorModes.IsBaseMode( "survival" ) ) return; if ( rules && rules->GetRoundStartTime() <= 0.0 ) ShowBest(); const float time = rules ? rules->GetTeamRoundDuration( TEAM_SURVIVOR ) : 0.0f;
		if ( !m_at30 && time >= 1800.0f ) { FireMilestone( "survival_at_30min" ); m_at30 = true; } if ( !m_at10 && time >= 600.0f ) { FireMilestone( "survival_at_10min" ); m_at10 = true; }
		if ( m_ring && m_initialized && m_target > m_previous ) m_ring->SetProgress( ( m_target - time ) / ( m_target - m_previous ) );
		if ( m_countdown ) { if ( time >= m_target ) ReachGoal(); } else if ( time != 0.0f && m_target > time && hud_survival_target_time_range.GetFloat() >= m_target - time ) m_countdown = true;
		if ( time != m_time ) { if ( m_time == 0.0f && time > 0.0f ) BeginGoals(); m_time = time; const int second = TimerText( m_digits, time ); static int lastSecond = -1; if ( m_countdown && second != lastSecond ) { TimerAnimation( "FlashCurrentTime" ); TimerSound( "Survival.countdown" ); lastSecond = second; } }
		if ( m_transition ) m_transition->SetPos( TerrorHudCvttFloat( m_transitionX + m_transitionXOffset ), TerrorHudCvttFloat( m_transitionY + m_transitionYOffset ) );
	}
private:
	const SurvivalGoal *Goal() const { static ConVarRef mode( "mp_gamemode" ); if ( !Q_stricmp( mode.GetString(), "versussurvival" ) ) { static SurvivalGoal versus; versus.type = 3; versus.id = 4; versus.time = TerrorGameRules() ? TerrorGameRules()->GetTeamBestRoundTime() : 0.0f; return &versus; } return m_goals.IsValidIndex( m_goal ) ? &m_goals[m_goal] : NULL; }
	bool NextGoal() { bool advanced = false; const int last = m_goals.Count() - 1; if ( m_goal < last ) { ++m_goal; advanced = true; } while ( m_goal < last && m_goals[m_goal].time == m_goals[m_goal + 1].time ) { ++m_goal; advanced = true; } return advanced; }
	void BuildGoals()
	{
		C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); CTerrorGameRules *rules = TerrorGameRules(); if ( !r || !rules ) return; m_goals.RemoveAll(); float lowest = FLT_MAX; if ( !engine->IsHLTV() ) for ( int i = 1; i <= 32; ++i ) { C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByIndex( i ) ); const float best = r->GetSurvivalRecordTime( i ); if ( p && p->IsPlayer() && p->m_bWasPresentAtSurvivalStart && !p->IsBot() ) { if ( !( best > lowest ) ) lowest = best; if ( best > 0.0f ) { SurvivalGoal goal = { 0, best, p->GetUserID() }; m_goals.Insert( goal ); } } } if ( lowest == FLT_MAX ) lowest = 0.0f; static const float medals[3] = { 240.0f, 420.0f, 600.0f }; for ( int i = 0; i < 3; ++i ) if ( medals[i] > lowest ) { SurvivalGoal goal = { 2, medals[i], i }; m_goals.Insert( goal ); } const float best = rules->GetTeamBestRoundTime(); if ( best > 0.0f ) { SurvivalGoal goal = { 1, best, 3 }; m_goals.Insert( goal ); }
	}
	void BeginGoals() { m_target = 0.0f; BuildGoals(); NextGoal(); const SurvivalGoal *goal = Goal(); if ( goal && goal->time != m_target ) ChangeTarget( goal->time, true ); SetGoalImage(); m_initialized = true; }
	void ChangeTarget( float target, bool animate ) { m_previous = m_target; m_target = target; UpdateTargetText(); if ( animate && m_targetDigits && m_transition ) { m_targetDigits->SetAlpha( 0 ); m_transition->SetAlpha( 255 ); int x, y; m_transition->GetPos( x, y ); m_transitionX = x; m_transitionY = y; m_transitionXOffset = m_transitionYOffset = 0; TimerAnimation( "TargetTimeTransition" ); } }
	void UpdateTargetText()
	{
		if ( !m_targetDigits || !m_transition ) { Warning( "Missing survival target timer element\n" ); return; }
		wchar_t text[32]; m_targetDigits->GetText( text, 16 ); m_transition->SetText( text );
		m_transition->SetBounds( m_targetDigits->GetXPos(), m_targetDigits->GetYPos(), m_targetDigits->GetWide(), m_targetDigits->GetTall() );
		TimerText( m_targetDigits, m_target );
		const SurvivalGoal *goal = Goal(); if ( !goal ) return;
		const wchar_t *description = NULL; wchar_t playerDescription[64], name[32];
		switch ( goal->type )
		{
		case 0:
			{
				C_BasePlayer *player = UTIL_PlayerByUserId( goal->id ); if ( !player ) return;
				g_pVGuiLocalize->ConvertANSIToUnicode( player->GetPlayerName(), name, sizeof( name ) );
				g_pVGuiLocalize->ConstructString( playerDescription, sizeof( playerDescription ), TerrorFindLocalizedTextSafe( "#L4D_SurvivalTimer_DescriptionFmt" ), 1, name );
				description = playerDescription;
			}
			break;
		case 1: description = TerrorFindLocalizedTextSafe( "#L4D_SurvivalTimer_Description_TeamRecord" ); break;
		case 2:
			{
				static const char *const tokens[3] = { "#L4D_SurvivalTimer_Description_Bronze", "#L4D_SurvivalTimer_Description_Silver", "#L4D_SurvivalTimer_Description_Gold" };
				if ( (unsigned int)goal->id < ARRAYSIZE( tokens ) ) description = TerrorFindLocalizedTextSafe( tokens[goal->id] );
			}
			break;
		case 3: description = TerrorFindLocalizedTextSafe( "#L4D360UI_VersusSurvival_TimeToBeat" ); break;
		}
		if ( description ) SetControlString( "NextGoalDescriptor", description );
	}
	void SetGoalImage() { if ( !m_icon ) return; const SurvivalGoal *goal = Goal(); const char *image = ""; if ( goal ) { if ( goal->type == 0 ) image = "icon_player_record_small"; else if ( goal->type == 1 || ( goal->type == 3 && m_target > 0.0f ) ) image = "icon_team_record_small"; else if ( goal->type == 2 ) { static const char *images[3] = { "icon_bronze_medal_small", "icon_silver_medal_small", "icon_gold_medal_small" }; if ( (unsigned int)goal->id < 3u ) image = images[goal->id]; } } m_icon->SetIcon( image ); }
	void ShowBest() { CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return; float best = rules->GetTeamBestRoundTime(); bool team = true; C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); C_BasePlayer *p = C_BasePlayer::GetLocalPlayer(); if ( best <= 0.0f && !engine->IsHLTV() && p && r ) { best = r->GetSurvivalRecordTime( p->entindex() ); team = false; } if ( best == m_best ) return; m_best = best; SetControlString( "NextGoalDescriptor", team ? "#L4D_SurvivalTimer_TeamBest" : "#L4D_SurvivalTimer_YourBest" ); if ( best != m_target ) ChangeTarget( best, false ); if ( m_icon ) { if ( !( best > 0.0f ) ) { SetControlString( "NextGoalDescriptor", "" ); m_icon->SetIcon( "" ); } else m_icon->SetIcon( "icon_team_record_small" ); } }
	void GoalEvent( const char *name, int id, int user ) { C_TerrorPlayer *p = ToTerrorPlayer( UTIL_PlayerByUserId( user ) ); if ( p && p->IsPlayer() && !p->m_bWasPresentAtSurvivalStart ) return; IGameEvent *event = gameeventmanager->CreateEvent( "survival_goal_reached" ); if ( event ) { event->SetString( "goalname", name ); event->SetInt( "id", id ); event->SetInt( "userid", user ); gameeventmanager->FireEventClientSide( event ); char sound[128]; Q_snprintf( sound, sizeof( sound ), "Survival.%s", name ); TimerSound( sound ); } }
	void ReachGoal() { const SurvivalGoal *goal = Goal(); if ( goal ) { if ( goal->type == 0 ) GoalEvent( "player_record", goal->id, goal->id ); else if ( goal->type == 1 ) GoalEvent( "team_record", goal->id, 0 ); else if ( goal->type == 2 ) { C_BasePlayer *p = C_BasePlayer::GetLocalPlayer(); C_TerrorPlayerResource *r = dynamic_cast<C_TerrorPlayerResource *>( g_PR ); if ( p && r ) GoalEvent( goal->time <= r->GetSurvivalRecordTime( p->entindex() ) ? "nomedal" : "medal", goal->id, p->GetUserID() ); } else if ( goal->type == 3 ) GoalEvent( "versus_record", goal->id, 0 ); } if ( NextGoal() ) { goal = Goal(); if ( goal && goal->time != m_target ) ChangeTarget( goal->time, true ); SetGoalImage(); m_countdown = false; } else { TimerAnimation( "RecordTimeTransition" ); m_record = true; m_countdown = false; } }
	void FireMilestone( const char *name ) { IGameEvent *event = gameeventmanager->CreateEvent( name ); if ( event ) gameeventmanager->FireEventClientSide( event ); }
	float m_time, m_target, m_previous, m_best; bool m_countdown, m_initialized, m_record, m_at30, m_at10; int m_goal; CUtlSortVector< SurvivalGoal, SurvivalGoalLess > m_goals; CIconPanel *m_icon; vgui::Label *m_digits, *m_targetDigits, *m_transition; vgui::CircularProgressBar *m_ring; float m_transitionX, m_transitionY;
	CPanelAnimationVar( vgui::HFont, m_numberFont, "NumberFont", "HudNumbers" ); CPanelAnimationVar( int, m_aliveY, "TimerYPosAlive", "30" ); CPanelAnimationVar( int, m_deadY, "TimerYPosDead", "80" ); CPanelAnimationVar( int, m_transitionXOffset, "TargetTransitionXOffset", "0" ); CPanelAnimationVar( int, m_transitionYOffset, "TargetTransitionYOffset", "0" );
};
DECLARE_HUDELEMENT( CHudSurvivalTimer );
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
