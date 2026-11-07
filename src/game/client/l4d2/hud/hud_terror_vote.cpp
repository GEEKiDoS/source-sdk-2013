//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "hud.h"
#include "hud_macros.h"
#include "hud_vote.h"
#include "iclientmode.h"
#include "cdll_util.h"
#include "igameevents.h"
#include "game/client/iviewport.h"
#include "inputsystem/ButtonCode.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui_controls/AnimationController.h>
#include "tier3/tier3.h"
#include "tier0/memdbgon.h"

// Native client10516310. The existing spawn-menu owner registers the sole
// gameui_ingame_menu_active; its real ClientModeTerrorNormal::Update publishes it.
ConVar cl_vote_active( "cl_vote_active", "0", FCVAR_DEVELOPMENTONLY | FCVAR_CLIENTDLL );
DECLARE_HUDELEMENT( CHudVote );
DECLARE_HUD_MESSAGE( CHudVote, VoteRegistered );
DECLARE_HUD_MESSAGE( CHudVote, CallVoteFailed );
DECLARE_HUD_MESSAGE( CHudVote, VoteStart );
DECLARE_HUD_MESSAGE( CHudVote, VotePass );
DECLARE_HUD_MESSAGE( CHudVote, VoteFail );

namespace
{
	bool VoteHudTimeAfter( float left, float right )
	{
		unsigned int a = FloatBits( left ), b = FloatBits( right );
		if ( ( a & 0x7fffffffu ) > 0x7f800000u || ( b & 0x7fffffffu ) > 0x7f800000u ) return false;
		if ( ( a & 0x7fffffffu ) == 0u ) a = 0u;
		if ( ( b & 0x7fffffffu ) == 0u ) b = 0u;
		a = ( a & 0x80000000u ) ? ~a : a | 0x80000000u;
		b = ( b & 0x80000000u ) ? ~b : b | 0x80000000u;
		return a > b;
	}
	bool PositiveVoteHudTime( float value )
	{
		const unsigned int bits = FloatBits( value );
		return bits > 0u && bits <= 0x7f800000u;
	}
	const wchar_t *VoteLocalizedText( const char *token, const char *parameter, wchar_t *output, int bytes )
	{
		const wchar_t *format = g_pVGuiLocalize->Find( token );
		if ( !parameter[0] ) return format;
		wchar_t unicode[128]; // Native conversion buffer256 BYTES, not256 wchar_t.
		const wchar_t *value;
		if ( parameter[0] == '#' ) value = g_pVGuiLocalize->Find( parameter );
		else
		{
			g_pVGuiLocalize->ConvertANSIToUnicode( parameter, unicode, sizeof( unicode ) );
			value = unicode;
		}
		g_pVGuiLocalize->ConstructString( output, bytes, format, 1, value );
		return output;
	}
}

VoteBarPanel::VoteBarPanel( vgui::Panel *parent, const char *name ) : BaseClass( parent, name )
{
	m_nYesVotes = m_nNoVotes = m_nPotentialVotes = 0;
}
void VoteBarPanel::SetVoteCounts( int yes, int no, int potential )
{
	m_nYesVotes = yes;
	m_nNoVotes = no;
	m_nPotentialVotes = potential;
}
void VoteBarPanel::Paint()
{
	// Native2DACC0: no normalization/clamp; YES takes priority on overlap.
	int x = 0;
	for ( int i = 0; i < m_nPotentialVotes; ++i )
	{
		vgui::surface()->DrawSetColor( 128, 128, 128, 128 );
		vgui::surface()->DrawFilledRect( x, 0, x + m_iBoxSize, m_iBoxSize );
		vgui::surface()->DrawSetColor( 0, 0, 0, 255 );
		vgui::surface()->DrawFilledRect( x + m_iBoxInset, m_iBoxInset, x + m_iBoxSize - m_iBoxInset, m_iBoxSize - m_iBoxInset );
		vgui::surface()->DrawSetColor( 255, 255, 255, 255 );
		if ( i < m_nYesVotes || i >= m_nPotentialVotes - m_nNoVotes )
		{
			vgui::surface()->DrawSetTexture( i < m_nYesVotes ? m_nYesTextureId : m_nNoTextureId );
			const int inset = 2 * m_iBoxInset;
			vgui::surface()->DrawTexturedRect( x + inset, inset, x + m_iBoxSize - inset, m_iBoxSize - inset );
		}
		x += m_iBoxSize + m_iSpacer;
	}
}

CHudVote::CHudVote( const char *name ) : BaseClass( NULL, "CHudVote" ), CHudElement( name )
{
	SetParent( g_pClientMode->GetViewport() );
	SetHiddenBits( 0 );
	m_pVoteActive = new vgui::EditablePanel( this, "VoteActive" );
	m_pVoteFailed = new vgui::EditablePanel( this, "VoteFailed" );
	m_pVotePassed = new vgui::EditablePanel( this, "VotePassed" );
	m_pCallVoteFailed = new vgui::EditablePanel( this, "CallVoteFailed" );
	m_voteBar = new VoteBarPanel( m_pVoteActive, "VoteBar" );
	SetProportional( true );
	RegisterForRenderGroup( "vote" );
	// Native2DC310 leaves round flags/times to Init/LevelInit/message writers.
}
void CHudVote::Init()
{
	ListenForGameEvent( "vote_changed" );
	m_bVotingActive = false;
	cl_vote_active.SetValue( 0 );
	m_flVoteResultCycleTime = -1.0f;
	m_flHideTime = -1.0f;
	HOOK_HUD_MESSAGE( CHudVote, VoteRegistered );
	HOOK_HUD_MESSAGE( CHudVote, CallVoteFailed );
	HOOK_HUD_MESSAGE( CHudVote, VoteStart );
	HOOK_HUD_MESSAGE( CHudVote, VotePass );
	HOOK_HUD_MESSAGE( CHudVote, VoteFail );
}
void CHudVote::LevelInit()
{
	m_bVotingActive = false;
	cl_vote_active.SetValue( 0 );
	m_flVoteResultCycleTime = -1.0f;
	m_flHideTime = -1.0f;
}
bool CHudVote::ShouldDraw()
{
	// Actual2DBF70 is gated by GAMEUI menu state, NOT hidehud or a TF vote ID.
	static ConVarRef menuActive( "gameui_ingame_menu_active" );
	return ( m_bVotingActive || VoteHudTimeAfter( m_flHideTime, gpGlobals->curtime ) ) && !menuActive.GetInt();
}
void CHudVote::ApplySchemeSettings( vgui::IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	LoadControlSettings( "Resource/UI/HUD/VoteHud.res" );
}
void CHudVote::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( name && !Q_stricmp( name, "vote_changed" ) )
		m_voteBar->SetVoteCounts( event->GetInt( "yesVotes" ), event->GetInt( "noVotes" ), event->GetInt( "potentialVotes" ) );
}
void CHudVote::OnThink()
{
	if ( PositiveVoteHudTime( m_flVoteResultCycleTime ) && VoteHudTimeAfter( gpGlobals->curtime, m_flVoteResultCycleTime ) )
	{
		m_flVoteResultCycleTime = -1.0f;
		m_pVoteActive->SetVisible( false );
		m_pVoteFailed->SetVisible( !m_bVotePassed );
		m_pVotePassed->SetVisible( m_bVotePassed );
		cl_vote_active.SetValue( 0 );
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_pVoteActive, "HideVoteBackgrounds" );
	}
	BaseClass::OnThink();
}
int CHudVote::KeyInput( int down, ButtonCode_t key, const char *binding )
{
	if ( !IsVisible() || !down || !m_bVotingActive || m_bPlayerVoted ) return 1;
	// Native120/121 are Xbox BACK/START;332/333 are Steam SELECT/START.
	// Use actual target enum names, not casts of pinned retail numeric codes.
	switch ( key )
	{
	case KEY_XBUTTON_BACK:
	case STEAMCONTROLLER_SELECT:
		engine->ClientCmd( "Vote No" ); return 0;
	case KEY_XBUTTON_START:
	case STEAMCONTROLLER_START:
		engine->ClientCmd( "Vote Yes" ); return 0;
	default:
		return 1;
	}
}
void CHudVote::MsgFunc_VoteRegistered( bf_read &message )
{
	const int yes = message.ReadByte();
	m_bPlayerVoted = true;
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_pVoteActive, yes > 0 ? "PulseYesVote" : "PulseNoVote" );
}
void CHudVote::MsgFunc_CallVoteFailed( bf_read &message )
{
	const int reason = message.ReadByte();
	if ( ShouldDraw() ) return;
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	player->EmitSound( "Vote.Failed" );
	m_pVoteActive->SetVisible( false );
	m_pVoteFailed->SetVisible( false );
	m_pVotePassed->SetVisible( false );
	m_pCallVoteFailed->SetVisible( true );
	m_flHideTime = (float)( (double)gpGlobals->curtime + 4.0 );
	if ( reason == 0 || reason == 2 ) m_pCallVoteFailed->SetControlString( "FailedReason", "#L4d_vote_no_vote_spam" );
	else if ( reason == 1 ) m_pCallVoteFailed->SetControlString( "FailedReason", "#L4d_vote_no_transition_vote" );
	// Native unknown reason retains the previous control text.
}
void CHudVote::MsgFunc_VoteStart( bf_read &message )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player ) return;
	const int team = message.ReadByte();
	if ( team != 255 && team != player->GetTeamNumber() ) return;
	const int initiator = message.ReadByte();
	char issue[256], parameter[256], caller[256];
	issue[0] = parameter[0] = caller[0] = '\0';
	message.ReadString( issue, sizeof( issue ) );
	message.ReadString( parameter, sizeof( parameter ) );
	message.ReadString( caller, sizeof( caller ) );
	g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( m_pVoteActive, "HideVoteBackgrounds" );
	gViewPortInterface->ShowPanel( "scores", false );
	m_bPlayerVoted = false;
	m_bVotingActive = true;
	cl_vote_active.SetValue( 1 );
	m_pVoteActive->SetVisible( true );
	m_pVoteFailed->SetVisible( false );
	m_pVotePassed->SetVisible( false );
	m_pCallVoteFailed->SetVisible( false );
	wchar_t result[512];
	if ( m_pVoteActive ) m_pVoteActive->SetDialogVariable( "voteissue", VoteLocalizedText( issue, parameter, result, sizeof( result ) ) );
	wchar_t instruction[512];
	Q_memset( instruction, 0, sizeof( instruction ) );
	const wchar_t *text = g_pVGuiLocalize->Find( "#L4D_vote_yes_pc_instruction" );
	if ( text )
	{
		UTIL_ReplaceKeyBindings( text, 0, instruction, sizeof( instruction ) );
		if ( m_pVoteActive ) m_pVoteActive->SetControlString( "YesPCLabel", instruction );
	}
	text = g_pVGuiLocalize->Find( "#L4D_vote_no_pc_instruction" );
	if ( text )
	{
		UTIL_ReplaceKeyBindings( text, 0, instruction, sizeof( instruction ) );
		if ( m_pVoteActive ) m_pVoteActive->SetControlString( "NoPCLabel", instruction );
	}
	if ( m_pVoteActive )
	{
		if ( caller[0] )
		{
			wchar_t unicode[128], header[512];
			g_pVGuiLocalize->ConvertANSIToUnicode( caller, unicode, sizeof( unicode ) );
			header[0] = L'\0';
			g_pVGuiLocalize->ConstructString( header, sizeof( header ), g_pVGuiLocalize->Find( "#L4D_vote_caller_header" ), 1, unicode );
			m_pVoteActive->SetControlString( "Header", "" );
			m_pVoteActive->SetControlString( "HeaderCaller", header );
		}
		else
		{
			m_pVoteActive->SetControlString( "Header", g_pVGuiLocalize->Find( "#L4D_vote_header" ) );
			m_pVoteActive->SetControlString( "HeaderCaller", "" );
		}
	}
	// The target has ONE real local player. This is native slot0's actual
	// event path; multi-slot engine/player producers remain an explicit gap.
	IGameEvent *event = gameeventmanager->CreateEvent( "vote_started" );
	if ( event )
	{
		event->SetString( "issue", issue ); event->SetString( "param1", parameter );
		event->SetInt( "team", team ); event->SetInt( "initiator", initiator ); event->SetString( "callerName", caller );
		gameeventmanager->FireEventClientSide( event );
	}
	player->EmitSound( "Vote.Created" );
}
void CHudVote::MsgFunc_VotePass( bf_read &message )
{
	const int team = message.ReadByte();
	char token[256], parameter[256];
	token[0] = parameter[0] = '\0';
	message.ReadString( token, sizeof( token ) );
	message.ReadString( parameter, sizeof( parameter ) );
	wchar_t result[512];
	m_pVotePassed->SetDialogVariable( "passedresult", VoteLocalizedText( token, parameter, result, sizeof( result ) ) );
	m_bVotingActive = false;
	cl_vote_active.SetValue( 0 );
	m_bVotePassed = true;
	m_flVoteResultCycleTime = (float)( (double)gpGlobals->curtime + 2.0 );
	m_flHideTime = (float)( (double)gpGlobals->curtime + 5.0 );
	IGameEvent *event = gameeventmanager->CreateEvent( "vote_passed" );
	if ( event )
	{
		event->SetString( "details", token ); event->SetString( "param1", parameter ); event->SetInt( "team", team );
		gameeventmanager->FireEventClientSide( event );
	}
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player ) player->EmitSound( "Vote.Passed" );
	// Native2DB7F0 does NOT repeat VoteStart's recipient-team filter.
}
void CHudVote::MsgFunc_VoteFail( bf_read &message )
{
	const int team = message.ReadByte();
	m_bVotingActive = false;
	cl_vote_active.SetValue( 0 );
	m_bVotePassed = false;
	m_flVoteResultCycleTime = (float)( (double)gpGlobals->curtime + 2.0 );
	m_flHideTime = (float)( (double)gpGlobals->curtime + 5.0 );
	IGameEvent *event = gameeventmanager->CreateEvent( "vote_failed" );
	if ( event )
	{
		event->SetInt( "team", team );
		gameeventmanager->FireEventClientSide( event );
	}
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player ) player->EmitSound( "Vote.Failed" );
}