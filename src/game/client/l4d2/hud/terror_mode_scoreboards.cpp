//========= Copyright Valve Corporation, All rights reserved. ============//
// Pinned client c0a8e1e88f7312db: C2DEC80..2E09E0, C2E0AE0..2E1010,
// C2E3770..2E4160, C2EC450..2EEEAE. Native addresses are provenance only.
#include "cbase.h"
#include "l4d2/hud/terror_mode_scoreboards.h"
#include "l4d2/ui/terror_legacy_controls.h"
#include "l4d2/hud/terror_versus_progress.h"
#include "l4d2/hud/terror_hud_round_timers.h"
#include "l4d2/stats/terror_client_stats.h"
#include "l4d2/player/c_terrorplayer.h"
#include "l4d2/player/c_terror_player_resource.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui_avatarimage.h"
#include "game_controls/IconPanel.h"
#include "c_recipientfilter.h"
#include "engine/IEngineSound.h"
#include "filesystem.h"
#include "vgui/IVGui.h"
#include "vgui/ISurface.h"
#include "vgui/IScheme.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/MessageMap.h"
#include <emmintrin.h>
#include <limits.h>
#include "tier0/memdbgon.h"
#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#if _MSC_VER >= 1933
#pragma fp_contract( off )
#endif
#endif

DECLARE_BUILD_FACTORY( CScavengeModeScoreboard );
DECLARE_BUILD_FACTORY( CSurvivalModeScoreboard );
DECLARE_BUILD_FACTORY( CVersusModeScoreboard );

#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar scavenge_scoreboard_survivor_score_effect_delay( "scavenge_scoreboard_survivor_score_effect_delay", "0.25", 0xa, "" );
#else
static ConVar scavenge_scoreboard_survivor_score_effect_delay( "scavenge_scoreboard_survivor_score_effect_delay", "0.25", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar scavenge_scoreboard_round_time_effect_delay( "scavenge_scoreboard_round_time_effect_delay", "0.05", 0xa, "" );
#else
static ConVar scavenge_scoreboard_round_time_effect_delay( "scavenge_scoreboard_round_time_effect_delay", "0.05", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar scavenge_scoreboard_round_time_effect_duration( "scavenge_scoreboard_round_time_effect_duration", "3.0", 0xa, "" );
#else
static ConVar scavenge_scoreboard_round_time_effect_duration( "scavenge_scoreboard_round_time_effect_duration", "3.0", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar scavenge_scoreboard_round_sound_effect_interval( "scavenge_scoreboard_round_sound_effect_interval", "0.1", 0xa, "" );
#else
static ConVar scavenge_scoreboard_round_sound_effect_interval( "scavenge_scoreboard_round_sound_effect_interval", "0.1", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_scoreanim_effectdelay( "hud_winpanel_scoreanim_effectdelay", "1.0", 0xa, "" );
#else
static ConVar hud_winpanel_scoreanim_effectdelay( "hud_winpanel_scoreanim_effectdelay", "1.0", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_scoreanim_effectdelay_mult( "hud_winpanel_scoreanim_effectdelay_mult", "0.7", 0xa, "" );
#else
static ConVar hud_winpanel_scoreanim_effectdelay_mult( "hud_winpanel_scoreanim_effectdelay_mult", "0.7", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_scoreanim_scoredelay( "hud_winpanel_scoreanim_scoredelay", "1.8", 0xa, "" );
#else
static ConVar hud_winpanel_scoreanim_scoredelay( "hud_winpanel_scoreanim_scoredelay", "1.8", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_scoreanim_chingdelay( "hud_winpanel_scoreanim_chingdelay", "0.2", 0xa, "" );
#else
static ConVar hud_winpanel_scoreanim_chingdelay( "hud_winpanel_scoreanim_chingdelay", "0.2", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_tiebreak_intro( "hud_winpanel_tiebreak_intro", "0.5", 0xa, "" );
#else
static ConVar hud_winpanel_tiebreak_intro( "hud_winpanel_tiebreak_intro", "0.5", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_tiebreak_anim( "hud_winpanel_tiebreak_anim", "2.5", 0xa, "" );
#else
static ConVar hud_winpanel_tiebreak_anim( "hud_winpanel_tiebreak_anim", "2.5", FCVAR_DEVELOPMENTONLY );
#endif
#if defined( L4D2 ) && defined( CLIENT_DLL )
static ConVar hud_winpanel_tiebreak_outro( "hud_winpanel_tiebreak_outro", "1.5", 0xa, "" );
#else
static ConVar hud_winpanel_tiebreak_outro( "hud_winpanel_tiebreak_outro", "1.5", FCVAR_DEVELOPMENTONLY );
#endif

static int ScoreAdd( int a, int b ) { return (int)( (unsigned int)a + (unsigned int)b ); }
static int ScoreSubtract( int a, int b ) { return (int)( (unsigned int)a - (unsigned int)b ); }
static int ScoreMultiply( int a, int b ) { return (int)( (unsigned int)a * (unsigned int)b ); }
static int ScoreInteger( double value ) { return _mm_cvttsd_si32( _mm_set_sd( value ) ); }
// C002CC30 uses float SSE division and an x87 final product/sum.
static double ScoreRemap( float value, float end, float low, float high )
{
	if ( end == 0.0f ) return value < end ? low : high;
	float fraction = value / end;
	if ( fraction < 0.0f ) fraction = 0.0f;
	else if ( fraction > 1.0f ) fraction = 1.0f;
	return (double)low + ( (double)high - low ) * fraction;
}
static void ScoreSound( const char *name )
{
	CLocalPlayerFilter filter;
	C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, name );
}
static void ScoreAnimationEvent()
{
	IGameEvent *event = gameeventmanager->CreateEvent( "start_score_animation" );
	if ( event ) gameeventmanager->FireEventClientSide( event );
}
static void FormatScoreTime( float time, char *text, int bytes )
{
	int minutes, seconds, hundredths;
	SplitTerrorSurvivalTime( time, minutes, seconds, hundredths );
	Q_snprintf( text, bytes, "%02d:%02d.%02d", minutes, seconds, hundredths );
}
static void SetScoreText( vgui::EditablePanel *panel, const char *name, int value )
{
	if ( !panel ) return;
	char text[32]; Q_snprintf( text, sizeof( text ), "%d", value );
	panel->SetControlString( name, text );
}
static vgui::Label *ScoreLabel( vgui::Panel *parent, const char *name )
{
	return parent ? dynamic_cast<vgui::Label *>( parent->FindChildByName( name ) ) : NULL;
}

CScoreboardEntry::CScoreboardEntry( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name ), m_selected( false ), m_haveNameMetrics( false ), m_nameWidth( 0 )
{
}
void CScoreboardEntry::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/SurvivalModeScoreboardEntry.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
}
void CScoreboardEntry::SetSelected( bool selected ) { m_selected = selected; }
void CScoreboardEntry::SetPlayerName( const char *name, bool presentAtStart )
{
	vgui::Label *label = ScoreLabel( this, "SurvivorName" );
	if ( !label || !name ) return;
	label->SetText( name ); label->SetAlpha( presentAtStart ? 255 : 32 );
	wchar_t text[512]; label->GetText( text, sizeof( text ) );
	int tall; vgui::surface()->GetTextSize( label->GetFont(), text, m_nameWidth, tall );
	m_haveNameMetrics = true;
}
void CScoreboardEntry::SetPlayerIndex( int index )
{
	CAvatarImagePanel *avatar = dynamic_cast<CAvatarImagePanel *>( FindChildByName( "SurvivorAvatar" ) );
	if ( avatar ) { avatar->SetVisible( index > 0 ); avatar->SetPlayer( index ); }
}
void CScoreboardEntry::SetTime( const char *time ) { if ( time ) SetControlString( "SurvivorTime", time ); }
void CScoreboardEntry::SetMedal( const char *icon )
{
	CIconPanel *panel = dynamic_cast<CIconPanel *>( FindChildByName( "SurvivorMedal" ) );
	if ( panel && icon ) panel->SetIcon( icon );
}
void CScoreboardEntry::PaintBackground()
{
	BaseClass::PaintBackground();
	if ( !m_selected ) return;
	int wide, tall; GetSize( wide, tall );
	vgui::surface()->DrawSetColor( Color( 0, 0, 0, 255 ) );
	vgui::surface()->DrawFilledRectFade( 0, 0, wide, tall, 0, 255, true );
	if ( m_haveNameMetrics )
	{
		const int nameEnd = ScoreAdd( m_nameWidth, vgui::scheme()->GetProportionalScaledValueEx( GetScheme(), 60 ) );
		const int quarter = (int)( (float)nameEnd * 0.25f );
		vgui::surface()->DrawSetColor( Color( 120, 20, 20, 255 ) );
		vgui::surface()->DrawFilledRectFade( 0, 0, quarter, tall, 0, 150, true );
		vgui::surface()->DrawFilledRectFade( quarter, 0, nameEnd, tall, 150, 0, true );
	}
	vgui::surface()->DrawSetColor( Color( 240, 0, 0, 255 ) );
	const int half = (int)( (float)wide * 0.5f );
	vgui::surface()->DrawFilledRectFade( 0, 0, half, 2, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( half, 0, wide, 2, 255, 0, true );
	vgui::surface()->DrawFilledRectFade( 0, tall - 2, half, tall, 0, 255, true );
	vgui::surface()->DrawFilledRectFade( half, tall - 2, wide, tall, 255, 0, true );
}

const CScavengeModeScoreboard::Effect_t CScavengeModeScoreboard::s_firstHalf[4] = { {1,0.5f},{2,1.5f},{4,1.0f},{11,1.0f} };
const CScavengeModeScoreboard::Effect_t CScavengeModeScoreboard::s_nextRound[4] = { {1,0.5f},{3,0.0f},{9,1.0f},{11,1.0f} };
const CScavengeModeScoreboard::Effect_t CScavengeModeScoreboard::s_nextRoundTie[8] = { {1,0.5f},{3,1.5f},{5,1.0f},{7,0.5f},{8,2.5f},{6,0.0f},{9,1.0f},{11,1.0f} };
const CScavengeModeScoreboard::Effect_t CScavengeModeScoreboard::s_matchTie[8] = { {1,0.5f},{3,1.5f},{5,1.0f},{7,0.5f},{8,2.5f},{6,0.0f},{10,1.0f},{11,1.0f} };
const CScavengeModeScoreboard::Effect_t CScavengeModeScoreboard::s_matchFinal[4] = { {1,0.5f},{3,1.5f},{10,1.0f},{11,1.0f} };

CScavengeModeScoreboard::CScavengeModeScoreboard( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name ), m_final( NULL ), m_current( NULL ), m_tie( NULL ), m_counter( 0 ),
	m_targetScore( 0 ), m_targetSlot( 0 ), m_effectKind( 0 ), m_effectIndex( -1 ), m_effectEnd( 0 ),
	m_nextIncrement( 0 ), m_effectStart( 0 ), m_incrementDelay( 0 ), m_timeRate( 0 ), m_nextSound( 0 ), m_soundInterval( 0 ), m_effects( NULL )
{
	m_time[0] = m_time[1] = NULL;
	m_teamID[0] = m_teamID[1] = m_matchScore[0] = m_matchScore[1] = 0;
	m_roundTime[0] = m_roundTime[1] = 0.0f;
	for ( int i = 0; i < 5; ++i )
	{
		char field[32]; Q_snprintf( field, sizeof( field ), "Round%dPanel", i + 1 );
		m_rounds[i] = new CScavengeRoundPanel( this, field );
	}
	m_final = new CScavengeRoundPanel( this, "FinalScorePanel" );
	m_tie = new vgui::EditablePanel( this, "TieBreakerPanel" );
	SetTickInterval( 250 );
}
CScavengeModeScoreboard::~CScavengeModeScoreboard() { vgui::ivgui()->RemoveTickSignal( GetVPanel() ); }
void CScavengeModeScoreboard::SetTickInterval( int milliseconds ) { vgui::ivgui()->AddTickSignal( GetVPanel(), milliseconds ); }
void CScavengeModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/ScavengeModeScoreboard.res", "GAME" );
	m_tie->LoadControlSettings( "Resource/UI/scavengetiebreakerpanel.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false );
	for ( int i = 0; i < 5; ++i ) m_rounds[i]->MakeReadyForUse();
	m_final->MakeReadyForUse(); m_tie->MakeReadyForUse();
	m_time[0] = ScoreLabel( m_tie, "YourTeamTime" ); m_time[1] = ScoreLabel( m_tie, "EnemyTeamTime" );
	RefreshRoundInfo();
}
void CScavengeModeScoreboard::RefreshMatchScore()
{
	CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return;
	m_final->SetWinnerColors( rules->GetScavengeWinningTeam( -1 ), m_teamID[0], m_teamID[1] );
	m_final->SetScore( 1, m_matchScore[0] ); m_final->SetScore( 2, m_matchScore[1] );
	SetControlVisible( "RoundInfoMessage", true );
}
void CScavengeModeScoreboard::RefreshRoundInfo()
{
	CTerrorGameRules *rules = TerrorGameRules(); if ( !rules || !g_PR ) return;
	m_effects = NULL; m_current = NULL;
	m_tie->SetVisible( false ); SetControlVisible( "RoundInfoMessage", false ); SetControlVisible( "RoundTipMessage", false );
	const int team = g_PR->GetTeam( engine->GetLocalPlayer() ), other = ( team == TEAM_SURVIVOR ) + 2;
	m_teamID[0] = rules->GetScavengeTeamIndex( team ); m_teamID[1] = rules->GetScavengeTeamIndex( other );
	m_matchScore[0] = rules->GetScavengeMatchScore( team ); m_matchScore[1] = rules->GetScavengeMatchScore( other );
	const int winner = rules->GetScavengeWinningTeam( -1 ), current = rules->GetScavengeRoundNumber(), limit = rules->GetScavengeRoundLimit();
	wchar_t text[260], number[6];
	const wchar_t *format = g_pVGuiLocalize->Find( "#L4D_Scavenge_Round_Current" );
	if ( format ) { V_snwprintf( text, ARRAYSIZE( text ), L"%ls: %d", format, current ); SetControlString( "RoundLabel", text ); }
	format = g_pVGuiLocalize->Find( "#L4D_Scavenge_RoundLimit" );
	if ( format ) { V_snwprintf( number, ARRAYSIZE( number ), L"%d", limit ); g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, number ); SetControlString( "RoundLimitLabel", text ); }
	const bool second = rules->IsScavengeSecondHalf();
	if ( second )
	{
		const int required = limit / 2 + 1;
		const bool firstWon = m_matchScore[0] >= required, secondWon = m_matchScore[1] >= required;
		const int firstScore = rules->GetScavengeTeamScore( TEAM_SURVIVOR, -1 ), secondScore = rules->GetScavengeTeamScore( TEAM_INFECTED, -1 );
		m_effects = firstScore == secondScore ? s_matchTie : s_matchFinal;
		if ( ( !firstWon && !secondWon ) || ( m_matchScore[0] == m_matchScore[1] && ScoreSubtract( limit, current ) > 0 ) )
		{
			const char *tip = "#L4D_Scavenge_NextRound";
			m_effects = firstScore == secondScore ? s_nextRoundTie : s_nextRound;
			if ( firstScore == secondScore )
			{
				if ( firstScore == 0 ) m_tie->SetControlString( "TieBreakerMethodLabel", "#L4D_Scavenge_TieBreaker_Description_Zero" );
				else if ( firstScore == 1 ) m_tie->SetControlString( "TieBreakerMethodLabel", "#L4D_Scavenge_TieBreaker_Description_Singular" );
				else
				{
					format = g_pVGuiLocalize->Find( "#L4D_Scavenge_TieBreaker_Description" );
					if ( format ) { V_snwprintf( number, ARRAYSIZE( number ), L"%d", firstScore ); g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, number ); m_tie->SetControlString( "TieBreakerMethodLabel", text ); }
				}
			}
			if ( winner )
			{
				const bool won = winner == m_teamID[0];
				SetControlString( "RoundInfoMessage", won ? "#L4D_Scavenge_RoundWon" : "#L4D_Scavenge_RoundLost" );
				tip = won ? "#L4D_Scavenge_EnemyFirst" : "#L4D_Scavenge_YouFirst";
			}
			else SetControlString( "RoundInfoMessage", "#L4D_Scavenge_Tie" );
			V_snwprintf( number, ARRAYSIZE( number ), L"%d", ScoreAdd( current, 1 ) );
			format = g_pVGuiLocalize->Find( tip );
			if ( format ) { g_pVGuiLocalize->ConstructString( text, sizeof( text ), format, 1, number ); SetControlString( "RoundTipMessage", text ); }
		}
		else
		{
			SetControlString( "RoundTipMessage", "" );
			SetControlString( "RoundInfoMessage", m_matchScore[0] > m_matchScore[1] ? "#L4D_Scavenge_MatchWon" : m_matchScore[0] < m_matchScore[1] ? "#L4D_Scavenge_MatchLost" : "#L4D_Scavenge_MatchTied" );
		}
	}
	else
	{
		SetControlString( "RoundTipMessage", "#L4D_Scavenge_TeamFlip" );
		SetControlString( "RoundInfoMessage", rules->GetScavengeItemsRemaining() <= 0 ? "#L4D_Scavenge_Map_Cleared" : rules->GetRoundTimeRemaining() > 0.0 ? "#L4D_Scavenge_Survivors_Wiped" : "#L4D_Scavenge_Time_Expired" );
		SetControlVisible( "RoundInfoMessage", true ); m_effects = s_firstHalf;
	}
	int lastX = 0, lastY = 0;
	for ( int i = 0; i < 5; ++i )
	{
		CScavengeRoundPanel *panel = m_rounds[i]; const int round = i + 1;
		int first = -1, enemy = -1;
		if ( round <= current )
		{
			first = rules->GetScavengeScore( m_teamID[0] == 1, round ); enemy = rules->GetScavengeScore( m_teamID[0] != 1, round );
			if ( round < current ) panel->SetWinnerColors( rules->GetScavengeWinningTeam( round ), m_teamID[0], m_teamID[1] );
			else
			{
				m_current = panel;
				if ( second )
				{
					if ( team == TEAM_SURVIVOR ) { m_targetSlot = 1; m_targetScore = first; first = -1; }
					else { m_targetSlot = 2; m_targetScore = enemy; enemy = -1; }
				}
				else
				{
					panel->SetScoreColor( 1, "White" ); panel->SetScoreColor( 2, "White" );
					m_targetScore = rules->GetScavengeTeamScore( TEAM_SURVIVOR, round ); m_targetSlot = ( team != TEAM_SURVIVOR ) + 1;
					first = enemy = -1;
				}
			}
		}
		panel->SetVisible( round <= limit );
		if ( round <= limit ) { panel->SetScore( 1, first ); panel->SetScore( 2, enemy ); panel->GetPos( lastX, lastY ); }
	}
	m_final->SetPos( ScoreInteger( (double)m_final->GetWide() * 1.5 + lastX ), lastY );
	m_final->SetScoreColor( 1, "White" ); m_final->SetScoreColor( 2, "White" );
	m_final->SetScore( 1, ScoreSubtract( m_matchScore[0], second && ( winner == 0 || winner == m_teamID[0] ) ) );
	m_final->SetScore( 2, ScoreSubtract( m_matchScore[1], second && ( winner == 0 || winner == m_teamID[1] ) ) );
	if ( m_effects )
	{
		m_counter = 0; m_effectStart = m_nextSound = 0.0f; m_effectIndex = -1; m_effectEnd = gpGlobals->curtime;
		AnimateScores(); ScoreAnimationEvent();
	}
}
void CScavengeModeScoreboard::NextEffect()
{
	++m_effectIndex;
	const Effect_t &effect = m_effects[m_effectIndex];
	m_effectEnd = gpGlobals->curtime + effect.delay; m_effectStart = 0.0f;
	switch ( effect.type )
	{
	case 2: case 3:
		m_counter = 0; m_effectStart = gpGlobals->curtime;
		m_incrementDelay = scavenge_scoreboard_survivor_score_effect_delay.GetFloat();
		m_nextIncrement = gpGlobals->curtime + m_incrementDelay; m_effectKind = 0;
		break;
	case 4: SetControlVisible( "RoundTipMessage", true ); ScoreSound( "ScavengeSB.DisplayChange" ); break;
	case 5:
		if ( m_time[0] && m_time[1] )
		{
			for ( int i = 0; i < 2; ++i ) { m_time[i]->SetFgColor( Color( 255,255,255,255 ) ); m_time[i]->SetText( "00:00.00" ); }
			m_tie->SetVisible( true ); ScoreSound( "ScavengeSB.OpenTiePanel" );
		}
		break;
	case 6: m_tie->SetVisible( false ); SetControlVisible( "RoundTipMessage", true ); break;
	case 7:
		{
			SetTickInterval( 20 ); const int team = g_PR->GetTeam( engine->GetLocalPlayer() );
			m_roundTime[0] = TerrorGameRules()->GetTeamRoundDuration( team );
			m_roundTime[1] = TerrorGameRules()->GetTeamRoundDuration( ( team == TEAM_SURVIVOR ) + 2 );
			m_timeRate = ( m_roundTime[0] > m_roundTime[1] ? m_roundTime[0] : m_roundTime[1] ) / scavenge_scoreboard_round_time_effect_duration.GetFloat();
			m_effectStart = gpGlobals->curtime; m_incrementDelay = scavenge_scoreboard_round_time_effect_delay.GetFloat();
			m_soundInterval = scavenge_scoreboard_round_sound_effect_interval.GetFloat();
			m_nextIncrement = gpGlobals->curtime + m_incrementDelay; m_effectKind = 1;
		}
		break;
	case 8:
		if ( m_time[0] && m_time[1] )
		{
			const int winner = TerrorGameRules()->GetScavengeWinningTeam( -1 );
			for ( int i = 0; i < 2; ++i ) m_time[i]->SetFgColor( winner == 0 || winner == m_teamID[i] ? Color( 0,255,0,255 ) : Color( 145,145,145,255 ) );
			m_final->SetWinnerColors( winner, m_teamID[0], m_teamID[1] ); ScoreSound( "ScavengeSB.DisplayChange" );
		}
		break;
	case 9:
		RefreshMatchScore(); SetControlVisible( "RoundTipMessage", true ); ScoreSound( "ScavengeSB.MatchScoreIncrement" ); break;
	case 10:
		RefreshMatchScore();
		m_final->SetWinnerColors( m_matchScore[0] > m_matchScore[1] ? m_teamID[0] : m_matchScore[0] < m_matchScore[1] ? m_teamID[1] : 0, m_teamID[0], m_teamID[1] );
		ScoreSound( "ScavengeSB.MatchScoreFinal" ); break;
	case 11: SetTickInterval( 250 ); m_effects = NULL; break;
	}
}
void CScavengeModeScoreboard::AnimateScores()
{
	if ( !TerrorGameRules() || !m_effects ) return;
	const float now = gpGlobals->curtime;
	if ( m_effectStart == 0.0f ) { if ( now >= m_effectEnd ) NextEffect(); return; }
	if ( !( now > m_nextIncrement ) ) return;
	bool complete;
	if ( m_effectKind == 0 )
	{
		if ( m_counter < m_targetScore ) m_counter = ScoreAdd( m_counter, 1 );
		if ( m_counter <= m_targetScore )
		{
			if ( m_current ) m_current->SetScore( m_targetSlot, m_counter );
			ScoreSound( m_counter == 0 ? "ScavengeSB.ZeroScore" : "ScavengeSB.RoundScoreIncrement" );
		}
		complete = m_counter >= m_targetScore;
	}
	else
	{
		const float elapsed = now - m_effectStart, increment = m_timeRate * elapsed;
		float shown[2];
		for ( int i = 0; i < 2; ++i )
		{
			const float limit = m_roundTime[i] <= increment ? m_roundTime[i] : increment;
			shown[i] = (float)ScoreRemap( elapsed, m_nextIncrement - m_effectStart, 0.0f, limit );
			if ( m_time[i] ) { char text[32]; FormatScoreTime( shown[i], text, sizeof( text ) ); m_time[i]->SetText( text ); }
		}
		if ( now > m_nextSound ) { ScoreSound( "ScavengeSB.RoundTimeIncrement" ); m_nextSound = now + m_soundInterval; }
		complete = !( shown[0] < m_roundTime[0] || shown[1] < m_roundTime[1] );
	}
	if ( complete ) { m_effectStart = 0.0f; m_effectEnd = now + m_effects[m_effectIndex].delay; }
	else m_nextIncrement = now + m_incrementDelay;
}
void CScavengeModeScoreboard::OnTick()
{
	if ( IsVisible() && ( !GetParent() || GetParent()->IsVisible() ) && m_effects ) AnimateScores();
}

CSurvivalModeScoreboard::CSurvivalModeScoreboard( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	for ( int i = 0; i < 4; ++i )
	{
		char field[32]; Q_snprintf( field, sizeof( field ), "Survivor%dEntry", i + 1 );
		m_entries[i] = new CScoreboardEntry( this, field );
	}
}
CSurvivalModeScoreboard::~CSurvivalModeScoreboard()
{
}
void CSurvivalModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/SurvivalModeScoreboard.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false );
	RefreshRoundInfo();
}
void CSurvivalModeScoreboard::RefreshRoundInfo()
{
	CTerrorGameRules *rules = TerrorGameRules();
	C_TerrorPlayerResource *resource = dynamic_cast<C_TerrorPlayerResource *>( g_PR );
	if ( !rules || !resource ) return;
	char time[12]; FormatScoreTime( (float)rules->GetTeamRoundDuration( TEAM_SURVIVOR ), time, sizeof( time ) );
	SetControlString( "FinalTimeDigits", time );
	for ( int i = 0; i < 4; ++i )
	{
		m_entries[i]->SetVisible( true ); m_entries[i]->SetSelected( false );
		m_entries[i]->SetPlayerName( "", false ); m_entries[i]->SetPlayerIndex( 0 );
		m_entries[i]->SetTime( "" ); m_entries[i]->SetMedal( "" );
	}
	static ConVarRef mode( "mp_gamemode" );
	if ( !Q_stricmp( mode.GetString(), "versussurvival" ) )
	{
		SetControlVisible( "FinalTimeDigits", false );
		const float duration = (float)rules->GetTeamRoundDuration( TEAM_SURVIVOR );
		const wchar_t *text = g_pVGuiLocalize->Find( rules->GetTeamBestRoundTime() <= duration ?
			"#L4D_Scavenge_TeamFlip" : "#L4D360UI_VersusSurvival_SurvivorsDefeated" );
		if ( text ) SetControlString( "FinalTimeLabel", text );
	}
	int row = 0;
	for ( int i = 1; i <= 32; ++i )
	{
		if ( !resource->IsConnected( i ) || resource->GetTeam( i ) == TEAM_INFECTED ) continue;
		C_TerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->IsPlayer() || player->IsBot() ) continue;
		const float record = resource->GetSurvivalRecordTime( i );
		if ( !( record >= 0.0f ) ) continue;
		FormatScoreTime( record, time, sizeof( time ) );
		// Native walks beyond its four-pointer array for an oversized scripted
		// roster. Only that undefined row access is skipped; stats still capture.
		if ( row < 4 )
		{
			CScoreboardEntry *entry = m_entries[row];
			entry->SetPlayerName( resource->GetPlayerName( i ), player->m_bWasPresentAtSurvivalStart );
			entry->SetPlayerIndex( i );
			if ( player->m_bWasPresentAtSurvivalStart )
			{
				entry->SetTime( time );
				const int medal = resource->GetSurvivalTopMedal( i );
				entry->SetMedal( medal == 0 ? "icon_bronze_medal_small" : medal == 1 ? "icon_silver_medal_small" : medal == 2 ? "icon_gold_medal_small" : "icon_player_record_small" );
			}
			else { entry->SetTime( "" ); entry->SetMedal( "" ); }
		}
		++row;
	}
	// Same existing per-consumer collector and native scope/team; not M6's
	// separate telemetry singleton, and not a scoreboard-owned stat mirror.
	CTerrorPlayerStats stats; stats.CapturePlayers( 0, TEAM_SURVIVOR );
	static const char *controls[8] = { "CommonKillsDigits", "HunterKillsDigits", "SmokerKillsDigits", "BoomerKillsDigits", "ChargerKillsDigits", "JockeyKillsDigits", "SpitterKillsDigits", "TankKillsDigits" };
	static const int statistics[8] = { 28,20,22,21,23,24,25,27 };
	for ( int i = 0; i < 8; ++i ) SetScoreText( this, controls[i], stats.SumStatistic( statistics[i] ) );
}

CVersusModeScoreboard::CVersusModeScoreboard( vgui::Panel *parent, const char *name ) :
	BaseClass( parent, name ), m_left( NULL ), m_right( NULL ), m_survivor( NULL ), m_infected( NULL ), m_tie( NULL ),
	m_campaignLeft( NULL ), m_campaignRight( NULL ), m_campaignSurvivor( NULL ), m_campaignInfected( NULL ), m_progress( NULL ),
	m_phase( 0 ), m_total( 0 ), m_oldTotal( 0 ), m_steps( 0 ), m_lastSoundScore( 0 ), m_phaseEnd( 0 ), m_phaseStart( 0 ), m_nextSound( 0 ),
	m_secondHalf( false ), m_tiebreak( false ), m_localSurvivor( false ), m_animationStarted( false )
{
	ResetDisplay( m_display ); ResetDisplay( m_previous );
	m_damage[0] = m_damage[1] = NULL;
	m_baseCampaign[0] = m_baseCampaign[1] = m_baseChapter[0] = m_baseChapter[1] = 0;
	m_damageTarget[0] = m_damageTarget[1] = 0;
	SetTickInterval( 250 );
	m_left = new vgui::EditablePanel( this, "LeftScorePanel" );
	m_right = new vgui::EditablePanel( this, "RightScorePanel" );
	m_tie = new vgui::EditablePanel( this, "TieBreakerPanel" );
}
CVersusModeScoreboard::~CVersusModeScoreboard() { vgui::ivgui()->RemoveTickSignal( GetVPanel() ); }
void CVersusModeScoreboard::SetTickInterval( int milliseconds ) { vgui::ivgui()->AddTickSignal( GetVPanel(), milliseconds ); }
void CVersusModeScoreboard::ResetDisplay( Display_t &display )
{
	for ( int i = 0; i < 4; ++i ) { display.progress[i][0] = -1; display.progress[i][1] = -2; }
	display.completion = display.survivors = display.defibs = display.chapter = display.enemyChapter = display.campaign = display.enemyCampaign = -1;
	display.enemyCompletion = display.enemySurvivors = display.enemyDefibs = display.damage = display.enemyDamage = display.damageWinner = display.enemyDamageWinner = -1;
}
void CVersusModeScoreboard::SetData( KeyValues *data )
{
	int team[2] = { 0,0 }, campaign[2] = { 0,0 };
	m_tiebreak = false;
	if ( data )
	{
		team[0] = data->GetInt( "t1" ); team[1] = data->GetInt( "t2" );
		campaign[0] = data->GetInt( "c1" ); campaign[1] = data->GetInt( "c2" );
		m_tiebreak = data->GetInt( "tiebreak" ) != 0;
	}
	CTerrorGameRules *rules = TerrorGameRules(); if ( !rules ) return;
	const int first = rules->GetScavengeTeamIndex( TEAM_SURVIVOR ) == 1 ? 0 : 1;
	m_baseCampaign[0] = team[first]; m_baseCampaign[1] = team[1-first];
	m_baseChapter[0] = campaign[first]; m_baseChapter[1] = campaign[1-first];
	StartScoreAnimation();
}
void CVersusModeScoreboard::StartScoreAnimation()
{
	// Native slot -1 resolves the active local slot. The target supports slot0.
	if ( !C_BasePlayer::GetLocalPlayer() || !TerrorGameRules() ) return;
	m_total = m_steps = m_lastSoundScore = 0; m_secondHalf = TerrorGameRules()->IsScavengeSecondHalf();
	m_phase = 1; m_phaseEnd = m_phaseStart = m_nextSound = 0.0f; m_animationStarted = true;
	SetTickInterval( 50 ); ScoreAnimationEvent();
	ResetDisplay( m_display ); ResetDisplay( m_previous );
}
void CVersusModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/VersusModeScoreboard.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false ); SetPaintBorderEnabled( false );
	m_campaignLeft = ScoreLabel( this, "CampaignScore_Yours_Score" );
	m_campaignRight = ScoreLabel( this, "CampaignScore_Enemy_Score" );
	m_left->LoadControlSettings( "resource/ui/versusmodescoreboard_survivor.res", "GAME" );
	m_right->LoadControlSettings( "resource/ui/versusmodescoreboard_survivor.res", "GAME" );
	const int empty[4][2] = { {0,-1},{0,-1},{0,-1},{0,-1} };
	for ( int side = 0; side < 2; ++side )
	{
		vgui::EditablePanel *panel = side ? m_right : m_left;
		CVersusModeLevelProgressBar *progress = dynamic_cast<CVersusModeLevelProgressBar *>( panel->FindChildByName( "CompletionProgressBar" ) );
		if ( progress ) progress->SetProgressData( empty, false );
	}
	C_BasePlayer *local = C_BasePlayer::GetLocalPlayer(); if ( !local ) return;
	m_localSurvivor = local->GetTeamNumber() == TEAM_SURVIVOR || local->GetTeamNumber() == TEAM_SPECTATOR;
	m_survivor = m_localSurvivor ? m_left : m_right; m_infected = m_localSurvivor ? m_right : m_left;
	m_campaignSurvivor = m_localSurvivor ? m_campaignLeft : m_campaignRight;
	m_campaignInfected = m_localSurvivor ? m_campaignRight : m_campaignLeft;
	SetScoreText( m_survivor, "Category_Chapter_Score", m_baseChapter[0] );
	SetScoreText( m_infected, "Category_Chapter_Score", m_baseChapter[1] );
	m_infected->SetControlEnabled( "Category_Completion", m_secondHalf );
	m_infected->SetControlVisible( "Category_Completion_Score", m_secondHalf );
	if ( !m_secondHalf )
	{
		m_infected->SetControlEnabled( "Category_SurvivalBonus", false );
		m_infected->SetControlVisible( "Category_SurvivalBonus_Score", false );
		m_infected->SetControlEnabled( "Category_Chapter", false );
		m_infected->SetControlVisible( "Category_Chapter_Score", false );
		m_infected->SetControlEnabled( "Category_DefibPenalty", false );
		m_infected->SetControlVisible( "Category_DefibPenalty_Score", false );
	}
	vgui::ImagePanel *image = dynamic_cast<vgui::ImagePanel *>( m_infected->FindChildByName( "TeamImage" ) );
	if ( image ) { image->SetEnabled( true ); image->SetImage( "select_pz" ); }
	if ( m_campaignSurvivor ) { char text[32]; Q_snprintf( text, sizeof( text ), "%d", m_baseCampaign[0] ); m_campaignSurvivor->SetText( text ); }
	if ( m_campaignInfected ) { char text[32]; Q_snprintf( text, sizeof( text ), "%d", m_baseCampaign[1] ); m_campaignInfected->SetText( text ); }
	m_progress = dynamic_cast<CVersusModeLevelProgressBar *>( m_survivor->FindChildByName( "CompletionProgressBar" ) );
	if ( m_progress ) { m_progress->SetEnabled( true ); m_progress->SetProgressData( empty, m_localSurvivor ); }
	if ( m_secondHalf && TerrorGameRules() )
	{
		CVersusModeLevelProgressBar *enemy = dynamic_cast<CVersusModeLevelProgressBar *>( m_infected->FindChildByName( "CompletionProgressBar" ) );
		if ( enemy )
		{
			enemy->SetEnabled( true );
			int records[4][2];
			for ( int i = 0; i < 4; ++i ) { records[i][0] = TerrorGameRules()->GetVersusDistancePerSurvivor( i, TEAM_INFECTED ); records[i][1] = TerrorGameRules()->GetVersusSurvivorDeathDistance( i, TEAM_INFECTED ); }
			enemy->SetProgressData( records, false );
		}
	}
	m_tie->LoadControlSettings( "resource/ui/versusmodescoreboard_tiebreak.res", "GAME" );
	m_damage[0] = ScoreLabel( m_tie, "YourTeamDamage" ); m_damage[1] = ScoreLabel( m_tie, "EnemyTeamDamage" );
	ResetDisplay( m_previous ); PublishScores();
}
void CVersusModeScoreboard::AnimateScores()
{
	CTerrorGameRules *rules = TerrorGameRules();
	if ( !rules || !m_animationStarted || m_phase > 12 ) return;
	const float now = gpGlobals->curtime;
	static ConVarRef survivalBonus( "vs_survival_bonus" ), defibPenalty( "vs_defib_penalty" ), tiebreakBonus( "vs_tiebreak_bonus" );
	if ( m_phaseEnd > now )
	{
		if ( m_phaseStart == 0.0f ) return;
		const float elapsed = now - m_phaseStart;
		if ( m_phase == 10 )
		{
			if ( !C_BasePlayer::GetLocalPlayer() ) return;
			const int maximum = m_damageTarget[0] > m_damageTarget[1] ? m_damageTarget[0] : m_damageTarget[1];
			const int shown = ScoreInteger( ScoreRemap( elapsed, ( m_phaseEnd - m_phaseStart ) - 1.0f, 0.0f, (float)maximum ) );
			m_display.damage = shown <= m_damageTarget[0] ? MAX( shown, 0 ) : m_damageTarget[0];
			m_display.enemyDamage = shown <= m_damageTarget[1] ? MAX( shown, 0 ) : m_damageTarget[1];
			if ( m_damageTarget[0] != m_damageTarget[1] )
			{
				if ( m_display.damage == maximum ) m_display.damageWinner = 1;
				else if ( m_display.enemyDamage == maximum ) m_display.enemyDamageWinner = 1;
			}
			if ( now > m_nextSound && shown < maximum ) { ScoreSound( "Hint.LittleReward" ); m_nextSound = now + hud_winpanel_scoreanim_chingdelay.GetFloat(); }
			return;
		}
		int value = 0;
		bool haveValue = true;
		if ( m_steps )
		{
			const int difference = ScoreSubtract( m_total, m_oldTotal );
			// Native IDIV's sole exceptional signed quotient.
			if ( difference == INT_MIN && m_steps == -1 ) haveValue = false;
			else
			{
				const float duration = (float)( (double)m_phaseEnd - m_phaseStart - 0.8 );
				const int step = ScoreInteger( ScoreRemap( elapsed, duration, 0.0f, (float)m_steps ) );
				value = ScoreAdd( m_oldTotal, ScoreMultiply( difference / m_steps, step ) );
			}
		}
		else
		{
			const float duration = (float)( (double)m_phaseEnd - m_phaseStart - 0.2 );
			value = ScoreInteger( ScoreRemap( elapsed, duration, (float)m_oldTotal, (float)m_total ) );
		}
		if ( haveValue )
		{
			if ( value != m_lastSoundScore )
			{
				m_lastSoundScore = value;
				if ( now > m_nextSound ) { ScoreSound( m_phase == 7 ? "ScavengeSB.RoundTimeIncrement" : "Hint.LittleReward" ); m_nextSound = now + hud_winpanel_scoreanim_chingdelay.GetFloat(); }
			}
			// Native489 is reset to false and never written true in this family.
			// The live animation therefore always updates the survivor columns,
			// even for an infected local viewer; do not 'repair' its perspective.
			m_display.chapter = ScoreAdd( value, m_baseChapter[0] );
			m_display.campaign = ScoreAdd( value, m_baseCampaign[0] );
		}
		if ( m_phase == 3 )
		{
			int maximum = 0;
			for ( int i = 0; i < 4; ++i ) { const int distance = rules->GetVersusDistancePerSurvivor( i, TEAM_SURVIVOR ); if ( distance > maximum ) maximum = distance; }
			const float duration = (float)( (double)m_phaseEnd - m_phaseStart - 0.2 );
			float amount;
			if ( duration == 0.0f ) amount = elapsed < duration ? 0.0f : (float)maximum;
			else
			{
				float fraction = elapsed / duration;
				if ( fraction < 0.0f ) fraction = 0.0f;
				else if ( fraction > 1.0f ) fraction = 1.0f;
				amount = fraction * (float)maximum;
			}
			for ( int i = 0; i < 4; ++i )
			{
				const int distance = rules->GetVersusDistancePerSurvivor( i, TEAM_SURVIVOR );
				int shown = ScoreInteger( amount ); if ( distance < shown ) shown = distance;
				const int death = rules->GetVersusSurvivorDeathDistance( i, TEAM_SURVIVOR );
				m_display.progress[i][0] = shown;
				if ( shown < death || death == -1 ) m_display.progress[i][1] = -1;
				else m_display.progress[i][0] = m_display.progress[i][1] = death;
			}
		}
		return;
	}
	m_phaseStart = 0.0f;
	switch ( m_phase )
	{
	case 1:
		{
			const int completion = rules->GetVersusCompletionScore( TEAM_SURVIVOR );
			m_oldTotal = m_total; m_total = ScoreAdd( m_total, completion ); m_display.completion = completion;
			m_phaseEnd = now + hud_winpanel_scoreanim_effectdelay.GetFloat();
			if ( m_secondHalf )
			{
				m_display.enemyCompletion = rules->GetVersusCompletionScore( TEAM_INFECTED );
				m_display.enemySurvivors = rules->GetVersusSurvivalMultiplier( TEAM_INFECTED );
				m_display.enemyDefibs = rules->GetVersusDefibsUsed( TEAM_INFECTED );
			}
			++m_phase;
		}
		return;
	case 2: case 4: case 6:
		m_phaseStart = now; m_phaseEnd = now + hud_winpanel_scoreanim_scoredelay.GetFloat(); ++m_phase; return;
	case 3:
		m_steps = rules->GetVersusSurvivalMultiplier( TEAM_SURVIVOR ); m_oldTotal = m_total;
		m_total = ScoreAdd( m_total, ScoreMultiply( m_steps, survivalBonus.GetInt() ) );
		m_phaseEnd = now + hud_winpanel_scoreanim_effectdelay.GetFloat(); m_display.survivors = m_steps; ++m_phase; return;
	case 5:
		{
			m_steps = rules->GetVersusDefibsUsed( TEAM_SURVIVOR ); m_oldTotal = m_total;
			const int total = ScoreSubtract( m_total, ScoreMultiply( m_steps, defibPenalty.GetInt() ) );
			m_total = total < 0 ? 0 : total;
			m_phaseEnd = now + hud_winpanel_scoreanim_effectdelay.GetFloat(); m_display.defibs = m_steps; ++m_phase;
		}
		return;
	case 7:
		if ( m_tiebreak ) { m_phaseEnd = now + hud_winpanel_tiebreak_intro.GetFloat(); m_tie->SetVisible( true ); ++m_phase; }
		else m_phase = 12;
		return;
	case 8:
		{
			m_phaseEnd = now + hud_winpanel_scoreanim_effectdelay.GetFloat();
			m_display.damage = m_display.enemyDamage = 0; m_damageTarget[0] = m_damageTarget[1] = 0;
			C_BasePlayer *local = C_BasePlayer::GetLocalPlayer();
			if ( local )
			{
				const int team = local->GetTeamNumber() == TEAM_INFECTED ? TEAM_INFECTED : TEAM_SURVIVOR;
				m_damageTarget[0] = rules->GetVersusChapterDamage( team );
				m_damageTarget[1] = rules->GetVersusChapterDamage( ( team == TEAM_SURVIVOR ) + 2 );
			}
			++m_phase;
		}
		return;
	case 9: m_phaseStart = now; m_phaseEnd = now + hud_winpanel_tiebreak_anim.GetFloat(); ++m_phase; return;
	case 10:
		{
			m_phaseEnd = now + hud_winpanel_tiebreak_outro.GetFloat();
			if ( C_BasePlayer::GetLocalPlayer() && m_damageTarget[0] != m_damageTarget[1] )
			{
				const bool yours = m_damageTarget[0] > m_damageTarget[1];
				const int bonus = tiebreakBonus.GetInt();
				m_tie->SetControlVisible( yours ? "YourTeamBonus" : "EnemyTeamBonus", true );
				char text[32]; Q_snprintf( text, sizeof( text ), "+ %d", bonus ); m_tie->SetControlString( yours ? "YourTeamBonus" : "EnemyTeamBonus", text );
				if ( yours == m_localSurvivor )
				{
					m_display.completion = ScoreAdd( m_display.completion, bonus );
					const int team = rules->GetScavengeTeamIndex( TEAM_SURVIVOR );
					m_display.chapter = rules->GetVersusScore( team, false ); m_display.campaign = rules->GetVersusScore( team, true );
				}
				else
				{
					m_display.enemyCompletion = ScoreAdd( m_display.enemyCompletion, bonus );
					m_display.enemyChapter = ScoreAdd( m_baseChapter[1], bonus );
					m_display.enemyCampaign = rules->GetVersusScore( rules->GetScavengeTeamIndex( TEAM_INFECTED ), true );
				}
				ScoreSound( "Hint.LittleReward" );
			}
			++m_phase;
		}
		return;
	case 11: m_phaseEnd = now; m_tie->SetVisible( false ); ++m_phase; return;
	default:
		// C27B4C0's existing mounted mission-dictionary projection is the
		// same owner used by the transition/results panels, not a map heuristic.
		if ( m_secondHalf && !TerrorClient_GetTransitionChapterInfo( true, NULL ) && gViewPortInterface )
		{
			gViewPortInterface->ShowPanel( "fullscreen_vs_scoreboard", false );
			gViewPortInterface->ShowPanel( "fullscreen_vs_results", true );
		}
		return;
	}
}
void CVersusModeScoreboard::PublishScores()
{
	if ( !m_survivor || !m_infected ) return;
	if ( m_progress && Q_memcmp( m_display.progress, m_previous.progress, sizeof( m_display.progress ) ) )
		m_progress->SetProgressData( m_display.progress, m_localSurvivor );
	static ConVarRef survivalBonus( "vs_survival_bonus" ), defibPenalty( "vs_defib_penalty" );
	for ( int side = 0; side < 2; ++side )
	{
		vgui::EditablePanel *panel = side ? m_infected : m_survivor;
		const int completion = side ? m_display.enemyCompletion : m_display.completion;
		const int oldCompletion = side ? m_previous.enemyCompletion : m_previous.completion;
		if ( completion != oldCompletion ) SetScoreText( panel, "Category_Completion_Score", completion );
		static const char *const categories[2] = { "Category_SurvivalBonus", "Category_DefibPenalty" };
		static const char *const scores[2] = { "Category_SurvivalBonus_Score", "Category_DefibPenalty_Score" };
		for ( int category = 0; category < 2; ++category )
		{
			const int value = category ? ( side ? m_display.enemyDefibs : m_display.defibs ) : ( side ? m_display.enemySurvivors : m_display.survivors );
			const int previous = category ? ( side ? m_previous.enemyDefibs : m_previous.defibs ) : ( side ? m_previous.enemySurvivors : m_previous.survivors );
			if ( value == previous ) continue;
			panel->SetControlEnabled( categories[category], true );
			vgui::Label *label = ScoreLabel( panel, scores[category] );
			if ( label )
			{
				label->SetVisible( true );
				char text[64]; if ( value > 0 ) Q_snprintf( text, sizeof( text ), "%dx%d", value, category ? defibPenalty.GetInt() : survivalBonus.GetInt() );
				else Q_strncpy( text, "0", sizeof( text ) );
				label->SetText( text );
			}
		}
	}
	if ( m_display.chapter != m_previous.chapter ) SetScoreText( m_survivor, "Category_Chapter_Score", m_display.chapter );
	if ( m_display.enemyChapter != m_previous.enemyChapter ) SetScoreText( m_infected, "Category_Chapter_Score", m_display.enemyChapter );
	if ( m_display.campaign != m_previous.campaign && m_campaignSurvivor ) { char text[32]; Q_snprintf( text, sizeof( text ), "%d", m_display.campaign ); m_campaignSurvivor->SetText( text ); }
	if ( m_display.enemyCampaign != m_previous.enemyCampaign && m_campaignInfected ) { char text[32]; Q_snprintf( text, sizeof( text ), "%d", m_display.enemyCampaign ); m_campaignInfected->SetText( text ); }
	for ( int side = 0; side < 2; ++side ) if ( m_damage[side] )
	{
		const int damage = side ? m_display.enemyDamage : m_display.damage, previousDamage = side ? m_previous.enemyDamage : m_previous.damage;
		const int winner = side ? m_display.enemyDamageWinner : m_display.damageWinner, previousWinner = side ? m_previous.enemyDamageWinner : m_previous.damageWinner;
		if ( damage != previousDamage ) { char text[32]; Q_snprintf( text, sizeof( text ), "%d", damage ); m_damage[side]->SetText( text ); }
		if ( winner != previousWinner && winner > 0 ) m_damage[side]->SetFgColor( Color( 0,255,0,255 ) );
	}
	m_previous = m_display;
}
void CVersusModeScoreboard::OnTick()
{
	if ( IsVisible() && ( !GetParent() || GetParent()->IsVisible() ) ) { AnimateScores(); PublishScores(); }
}

CFullscreenScavengeModeScoreboard::CFullscreenScavengeModeScoreboard( IViewPort *viewport ) :
	BaseClass( viewport, "fullscreen_scavenge_scoreboard" ), m_scoreboard( NULL )
{
}
void CFullscreenScavengeModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/FullscreenScavengeModeScoreboard.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
	m_scoreboard = dynamic_cast<CScavengeModeScoreboard *>( FindChildByName( "ScavengeModeScoreboard" ) );
	if ( m_scoreboard ) m_scoreboard->MakeReadyForUse();
	vgui::Panel *background = FindChildByName( "ScoreboardImgBackground" );
	if ( background ) { int x,y,w,h; background->GetBounds( x,y,w,h ); background->SetBounds( 0,y,x+w,h ); }
}
void CFullscreenScavengeModeScoreboard::ShowPanel( bool state )
{
	if ( !IsVisible() && state )
	{
		m_scoreboard = dynamic_cast<CScavengeModeScoreboard *>( FindChildByName( "ScavengeModeScoreboard" ) );
		if ( m_scoreboard ) m_scoreboard->RefreshRoundInfo();
	}
	BaseClass::ShowPanel( state );
}
CFullscreenSurvivalModeScoreboard::CFullscreenSurvivalModeScoreboard( IViewPort *viewport ) :
	BaseClass( viewport, "fullscreen_survival_scoreboard" ), m_scoreboard( NULL )
{
}
void CFullscreenSurvivalModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "Resource/UI/FullscreenSurvivalModeScoreboard.res", "GAME" );
	BaseClass::ApplySchemeSettings( scheme );
	m_scoreboard = dynamic_cast<CSurvivalModeScoreboard *>( FindChildByName( "SurvivalModeScoreboard" ) );
	vgui::Panel *background = FindChildByName( "ScoreboardImgBackground" );
	if ( background ) { int x,y,w,h; background->GetBounds( x,y,w,h ); background->SetBounds( 0,y,x+w,h ); }
}
void CFullscreenSurvivalModeScoreboard::ShowPanel( bool state )
{
	if ( !IsVisible() && state )
	{
		m_scoreboard = dynamic_cast<CSurvivalModeScoreboard *>( FindChildByName( "SurvivalModeScoreboard" ) );
		if ( m_scoreboard ) m_scoreboard->RefreshRoundInfo();
	}
	BaseClass::ShowPanel( state );
}
CFullscreenVersusModeScoreboard::CFullscreenVersusModeScoreboard( IViewPort *viewport ) :
	BaseClass( viewport, "fullscreen_vs_scoreboard" )
{
	// Native creates this child before reading the layout; the other fullscreen
	// boards deliberately let the build factories create their normal child.
	m_scoreboard = new CVersusModeScoreboard( this, "VersusModeScoreboard" );
}
void CFullscreenVersusModeScoreboard::ApplySchemeSettings( vgui::IScheme *scheme )
{
	KeyValues *layout = new KeyValues( "Resource/UI/FullscreenVersusModeScoreboard.res" );
	KeyValues::AutoDelete deleteLayout( layout );
	const bool loaded = layout->LoadFromFile( filesystem, "Resource/UI/FullscreenVersusModeScoreboard.res", "GAME" );
	if ( loaded )
	{
		KeyValues *child = layout->FindKey( "CVersusModeScoreboard" );
		if ( child ) child->SetName( "VersusModeScoreboard" );
	}
	LoadControlSettings( "Resource/UI/FullscreenVersusModeScoreboard.res", "GAME", loaded ? layout : NULL );
	BaseClass::ApplySchemeSettings( scheme );
}
void CFullscreenVersusModeScoreboard::ShowPanel( bool state )
{
	if ( !IsVisible() && state && m_scoreboard ) m_scoreboard->InvalidateLayout( true, true );
	BaseClass::ShowPanel( state );
}
void CFullscreenVersusModeScoreboard::SetData( KeyValues *data ) { if ( m_scoreboard ) m_scoreboard->SetData( data ); }
IViewPortPanel *CreateTerrorModeScoreboardPanel( IViewPort *viewport, const char *name )
{
	if ( !Q_strcmp( name, "fullscreen_scavenge_scoreboard" ) ) return new CFullscreenScavengeModeScoreboard( viewport );
	if ( !Q_strcmp( name, "fullscreen_survival_scoreboard" ) ) return new CFullscreenSurvivalModeScoreboard( viewport );
	if ( !Q_strcmp( name, "fullscreen_vs_scoreboard" ) ) return new CFullscreenVersusModeScoreboard( viewport );
	return NULL;
}
#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
