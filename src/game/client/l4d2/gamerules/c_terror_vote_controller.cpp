//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "c_vote_controller.h"
#include "l4d2/player/c_terrorplayer.h"
#include "igameevents.h"
#include "tier0/memdbgon.h"

IMPLEMENT_CLIENTCLASS_DT( C_VoteController, DT_VoteController, CVoteController )
	RecvPropInt( RECVINFO_NAME( m_iActiveIssueIndex, m_activeIssueIndex ), 0, C_VoteController::RecvProxy_VoteType ),
	RecvPropInt( RECVINFO_NAME( m_iOnlyTeamToVote, m_onlyTeamToVote ) ),
	RecvPropInt( RECVINFO_NAME( m_nVotesYes, m_votesYes ), 0, C_VoteController::RecvProxy_VotesYes ),
	RecvPropInt( RECVINFO_NAME( m_nVotesNo, m_votesNo ), 0, C_VoteController::RecvProxy_VotesNo ),
	RecvPropInt( RECVINFO_NAME( m_nPotentialVotes, m_potentialVotes ) ),
END_RECV_TABLE()

C_VoteController::C_VoteController()
{
	// NativeA6210: exactly five scalar fields and two dirty bytes; no vote ID,
	// option array, yes/no-mode alias or global replacement service.
	ResetData();
	ListenForGameEvent( "vote_cast_yes" );
	ListenForGameEvent( "vote_cast_no" );
}
C_VoteController::~C_VoteController()
{
	// Real CGameEventListener destructor owns removal, as in nativeA62D0.
}
void C_VoteController::ResetData()
{
	m_iActiveIssueIndex = -1;
	m_iOnlyTeamToVote = -1;
	m_nVotesYes = 0;
	m_nVotesNo = 0;
	m_nPotentialVotes = 0;
	m_bVotesDirty = false;
	m_bTypeDirty = false;
}
void C_VoteController::Spawn()
{
	ResetData();
	BaseClass::Spawn();
	SetNextClientThink( gpGlobals->curtime );
}
void C_VoteController::RecvProxy_VoteType( const CRecvProxyData *data, void *structure, void *out )
{
	C_VoteController *self = static_cast< C_VoteController * >( structure );
	if ( self->m_iActiveIssueIndex == data->m_Value.m_Int ) return;
	self->m_iActiveIssueIndex = data->m_Value.m_Int;
	self->m_bTypeDirty = true; // NativeA5F20 does NOT reschedule think here.
}
void C_VoteController::RecvProxy_VotesYes( const CRecvProxyData *data, void *structure, void *out )
{
	C_VoteController *self = static_cast< C_VoteController * >( structure );
	if ( self->m_nVotesYes == data->m_Value.m_Int ) return;
	self->m_nVotesYes = data->m_Value.m_Int;
	self->m_bVotesDirty = true;
	// A5F81 FADD qword0.001 then FSTP float: preserve the independent widening.
	self->SetNextClientThink( (float)( (double)gpGlobals->curtime + 0.001 ) );
}
void C_VoteController::RecvProxy_VotesNo( const CRecvProxyData *data, void *structure, void *out )
{
	C_VoteController *self = static_cast< C_VoteController * >( structure );
	if ( self->m_nVotesNo == data->m_Value.m_Int ) return;
	self->m_nVotesNo = data->m_Value.m_Int;
	self->m_bVotesDirty = true;
	self->SetNextClientThink( (float)( (double)gpGlobals->curtime + 0.001 ) );
}
void C_VoteController::ClientThink()
{
	BaseClass::ClientThink();
	if ( m_bTypeDirty )
	{
		m_bTypeDirty = false;
		m_bVotesDirty = true;
	}
	if ( m_bVotesDirty )
	{
		if ( m_nPotentialVotes > 0 )
		{
			DevMsg( "Vote count: %d Yes (possible %d), %d No\n", m_nVotesYes, m_nPotentialVotes, m_nVotesNo );
			IGameEvent *event = gameeventmanager->CreateEvent( "vote_changed" );
			if ( event )
			{
				event->SetInt( "yesVotes", m_nVotesYes );
				event->SetInt( "noVotes", m_nVotesNo );
				event->SetInt( "potentialVotes", m_nPotentialVotes );
				gameeventmanager->FireEventClientSide( event );
			}
		}
		m_bVotesDirty = false;
	}
	SetNextClientThink( gpGlobals->curtime + 0.5f );
}
void C_VoteController::FireGameEvent( IGameEvent *event )
{
	const char *name = event->GetName();
	if ( !name ) return;
	C_TerrorPlayer *player = ToTerrorPlayer( C_BasePlayer::GetLocalPlayer() );
	if ( !player ) return;
	const int team = event->GetInt( "team" );
	if ( team > 0 && team != player->GetTeamNumber() ) return;
	// NativeA6340 has no TF vote-ID or HUD-visible gate.
	if ( !Q_stricmp( name, "vote_cast_no" ) ) player->EmitSound( "Vote.Cast.No" );
	else if ( !Q_stricmp( name, "vote_cast_yes" ) ) player->EmitSound( "Vote.Cast.Yes" );
}