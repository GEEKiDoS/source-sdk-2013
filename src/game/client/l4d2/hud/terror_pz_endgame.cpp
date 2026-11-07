//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/hud/terror_pz_endgame.h"
#include "hud_vote.h"
#include "hud_macros.h"
#include "iclientmode.h"
#include "cdll_util.h"
#include "l4d2/stats/c_l4d_gamestats.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/AnimationController.h"
#include "tier0/memdbgon.h"

// C5157F0 passes NULL; the native empty default is not numeric zero.
ConVar cl_rematch_vote_in_progress( "cl_rematch_vote_in_progress", (const char *)NULL, FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL );
static CPZEndGamePanel *s_pPZEndGame;
DECLARE_HUDELEMENT( CPZEndGamePanel );
DECLARE_HUD_MESSAGE( CPZEndGamePanel, PZEndGamePanelMsg );
DECLARE_HUD_MESSAGE( CPZEndGamePanel, PZEndGamePanelVoteRegisteredMsg );
DECLARE_HUD_MESSAGE( CPZEndGamePanel, PZEndGameVoteStatsMsg );
bool IsPZEndGamePanelVisible() { return s_pPZEndGame && s_pPZEndGame->vgui::EditablePanel::IsVisible(); }

CPZEndGamePanel::CPZEndGamePanel( const char *name ) : CHudElement( name ), BaseClass( NULL, "PZEndGamePanel" ), m_result( 0 ), m_registered( 0 )
{
	SetParent( g_pClientMode->GetViewport() );
	s_pPZEndGame = this;
	Q_memset( m_potential, 0, sizeof( m_potential ) ); Q_memset( m_yes, 0, sizeof( m_yes ) ); Q_memset( m_no, 0, sizeof( m_no ) ); Q_memset( m_complete, 0, sizeof( m_complete ) );
	m_active = new vgui::EditablePanel( this, "VoteActive" );
	m_wait = new vgui::EditablePanel( this, "VoteWait" );
	m_failed = new vgui::EditablePanel( this, "VoteFailed" );
	m_passed = new vgui::EditablePanel( this, "VotePassed" );
	m_bar = new VoteBarPanel( m_active, "VoteBar" );
	m_bar2 = new VoteBarPanel( m_active, "VoteBar2" );
}
CPZEndGamePanel::~CPZEndGamePanel() { s_pPZEndGame = NULL; }
void CPZEndGamePanel::Init()
{
	HOOK_HUD_MESSAGE( CPZEndGamePanel, PZEndGamePanelMsg );
	HOOK_HUD_MESSAGE( CPZEndGamePanel, PZEndGamePanelVoteRegisteredMsg );
	HOOK_HUD_MESSAGE( CPZEndGamePanel, PZEndGameVoteStatsMsg );
}
void CPZEndGamePanel::Reset() { cl_rematch_vote_in_progress.SetValue( 0 ); SetActive( false ); SetVisible( false ); }
void CPZEndGamePanel::VidInit() { SetActive( false ); SetVisible( false ); }
bool CPZEndGamePanel::ShouldDraw() { return IsActive(); }
void CPZEndGamePanel::ApplySchemeSettings( vgui::IScheme *scheme )
{
	LoadControlSettings( "resource/UI/PZEndGamePanel.res" );
	BaseClass::ApplySchemeSettings( scheme );
	SetBgColor( Color( 0, 0, 0, 0 ) );
}
void CPZEndGamePanel::Sound( const char *name )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer(); if ( player ) player->EmitSound( name );
}
int CPZEndGamePanel::KeyInput( int down, ButtonCode_t code, const char *binding )
{
	if ( binding && !Q_strcmp( binding, "vote yes" ) ) { TerrorRecordRematchVoteChoice( true ); engine->ClientCmd( "PZEndGameYes" ); return 0; }
	if ( binding && !Q_strcmp( binding, "vote no" ) ) { TerrorRecordRematchVoteChoice( false ); engine->ClientCmd( "PZEndGameNo" ); return 0; }
	// Native120/121 are Xbox BACK/START;332/333 are Steam SELECT/START.
	if ( code == KEY_XBUTTON_BACK || code == STEAMCONTROLLER_SELECT ) { engine->ClientCmd( "PZEndGameNo" ); return 0; }
	if ( code == KEY_XBUTTON_START || code == STEAMCONTROLLER_START ) { engine->ClientCmd( "PZEndGameYes" ); return 0; }
	return 1;
}
void CPZEndGamePanel::MsgFunc_PZEndGamePanelVoteRegisteredMsg( bf_read &message )
{
	int yes = message.ReadByte(); m_registered = (int)( (unsigned int)m_registered + 1u );
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_active, yes > 0 ? "PulseYesVote" : "PulseNoVote" );
	Sound( yes > 0 ? "Vote.Cast.Yes" : "Vote.Cast.No" );
}
void CPZEndGamePanel::MsgFunc_PZEndGamePanelMsg( bf_read &message )
{
	int show = message.ReadByte();
	static ConVarRef voteActive( "cl_vote_active" );
	cl_rematch_vote_in_progress.SetValue( show ? 1 : 0 ); voteActive.SetValue( show ? 1 : 0 );
	if ( !show ) { SetActive( false ); SetVisible( false ); return; }
	m_complete[0] = m_complete[1] = m_result = m_registered = 0;
	m_bar->SetVisible( true ); m_bar->SetVoteCounts( 0, 0, 0 ); m_resultTimer.Invalidate();
	SetControlVisible( "VoteTimer", false ); SetActive( true ); SetVisible( true ); m_active->SetVisible( true );
	m_active->SetControlString( "Issue", "#L4D_vote_endgame_text" );
	const char *tokens[] = { "#L4D_vote_yes_pc_instruction", "#L4D_vote_no_pc_instruction" };
	const char *controls[] = { "YesPCLabel", "NoPCLabel" };
	wchar_t text[512] = { 0 };
	for ( int i = 0; i < 2; ++i )
	{
		const wchar_t *format = g_pVGuiLocalize->Find( tokens[i] );
		if ( format ) { UTIL_ReplaceKeyBindings( format, 0, text, sizeof( text ) ); m_active->SetControlString( controls[i], text ); }
	}
	Sound( "Vote.Created" );
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_active, "HideVoteBackgrounds" );
	SetControlVisible( "TeamVsImage", TerrorGameRules() && TerrorGameRules()->IsVersusMode() );
	SetControlVisible( "TeamScavengeImage", TerrorGameRules() && TerrorGameRules()->IsScavengeMode() );
}
void CPZEndGamePanel::MsgFunc_PZEndGameVoteStatsMsg( bf_read &message )
{
	for ( int i = 0; i < 2; ++i ) { m_potential[i] = message.ReadByte(); m_yes[i] = message.ReadByte(); m_no[i] = message.ReadByte(); m_complete[i] = message.ReadByte(); }
	UpdateVote();
}
void CPZEndGamePanel::OnThink() { if ( ShouldDraw() ) UpdateVote(); }
void CPZEndGamePanel::UpdateVote()
{
	static ConVarRef voteActive( "cl_vote_active" );
	if ( m_complete[0] && m_complete[1] && m_resultTimer.HasStarted() && m_resultTimer.IsElapsed() )
	{
		voteActive.SetValue( 0 ); m_active->SetVisible( false ); m_wait->SetVisible( false );
		bool failed = ( m_potential[0] && m_yes[0] <= m_no[0] ) || ( m_potential[1] && m_yes[1] <= m_no[1] );
		int result = failed ? 2 : 1;
		if ( m_result != result ) { m_result = result; Sound( failed ? "Vote.Failed" : "Vote.Passed" ); TerrorRecordRematchOutcome( !failed ); }
		m_failed->SetVisible( failed ); m_passed->SetVisible( !failed ); return;
	}
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player || ( player->GetTeamNumber() != TEAM_SURVIVOR && player->GetTeamNumber() != TEAM_INFECTED ) ) return;
	int team = player->GetTeamNumber() == TEAM_SURVIVOR ? 0 : 1;
	// Native second-local-player presentation is omitted, not fabricated.
	if ( m_complete[team] && m_resultTimer.m_timestamp <= 0.0f ) m_resultTimer.Start( 2.0f );
	if ( m_complete[team] && m_resultTimer.IsElapsed() )
	{
		voteActive.SetValue( 0 );
		m_active->SetVisible( false ); m_failed->SetVisible( false ); m_passed->SetVisible( false );
		bool passed = m_yes[team] > m_no[team];
		m_wait->SetControlVisible( "PassedIcon", passed ); m_wait->SetControlVisible( "PassedTitle", passed );
		m_wait->SetControlVisible( "FailedIcon", !passed ); m_wait->SetControlVisible( "FailedTitle", !passed ); m_wait->SetVisible( true );
	}
	else
	{
		voteActive.SetValue( m_registered < 1 ? 1 : 0 );
		m_bar->SetVoteCounts( m_yes[team], m_no[team], m_potential[team] );
		m_wait->SetVisible( false ); m_failed->SetVisible( false ); m_passed->SetVisible( false ); m_active->SetVisible( true );
	}
}
