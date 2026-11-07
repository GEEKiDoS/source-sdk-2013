//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "vote_controller.h"
#include "l4d2/director/terror_director.h"
#include "l4d2/gamerules/terror_gamerules.h"
#include "l4d2/gamerules/terror_game_details.h"
#include "l4d2/gamerules/terror_mission_info.h"
#include "l4d2/gamerules/terror_mode.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/director/director_survival_mode.h"
#include "l4d2/director/director_versus_mode.h"
#include "l4d2/stats/l4d_gamestats.h"
#include "EventLog.h"
#include "team.h"
#include "recipientfilter.h"
#include "inetchannelinfo.h"
#include "tier1/fmtstr.h"
#include "tier0/memdbgon.h"

// Native21A000..21BF42 and353060..353F43. This replaces the TF implementation
// ONLY for L4D2. The canonical vote_controller.h L4D2 branch is in the packet.
ConVar sv_allow_votes( "sv_allow_votes", "1", 0x00000006, "Allow voting? (Requires mod's support first.)" );
ConVar sv_vote_timer_duration( "sv_vote_timer_duration", "15", 0x00000006, "How long to allow voting on an issue" );
ConVar sv_vote_command_delay( "sv_vote_command_delay", "2", 0x00000006, "How long after a vote passes until the action happens", false, 0.0f, true, 4.5f );
ConVar sv_vote_creation_timer( "sv_vote_creation_timer", "180", 0x00006004, "How often someone can individually call a vote." );
ConVar sv_vote_failure_timer( "sv_vote_failure_timer", "0", 0x00000006, "A vote that fails cannot be re-submitted for this long" );
ConVar sv_vote_show_caller( "sv_vote_show_caller", "1", 0x00006004, "Show the name of the person that called the vote? (1: Yes, 0: No)" );
ConVar sv_vote_plr_map_limit( "sv_vote_plr_map_limit", "3", 0x00006004, "Number of failed votes a user can call per map" );
ConVar sv_vote_issue_change_difficulty_allowed( "sv_vote_issue_change_difficulty_allowed", "1", 0x00000006, "Can people hold votes to change the difficulty?" );
ConVar sv_vote_issue_restart_game_allowed( "sv_vote_issue_restart_game_allowed", "1", 0x00000006, "Can people hold votes to restart the game?" );
ConVar sv_vote_issue_kick_allowed( "sv_vote_issue_kick_allowed", "1", 0x00000006, "Can people hold votes to kick players from the server?" );
ConVar sv_vote_issue_change_mission_allowed( "sv_vote_issue_change_mission_allowed", "1", 0x00000006, "Can people hold votes to change missions?" );
ConVar sv_vote_issue_change_map_now_allowed( "sv_vote_issue_change_map_now_allowed", "1", 0x00000006, "Can people hold votes to immediately change the map?" );
ConVar sv_vote_issue_change_map_later_allowed( "sv_vote_issue_change_map_later_allowed", "1", 0x00000006, "Can people hold votes to change the map after this round?" );
ConVar sv_vote_kick_ban_duration( "sv_vote_kick_ban_duration", "5", 0x00000006, "How long should a kick vote ban someone from the server? (in minutes)" );
extern ConVar sv_alltalk;

CVoteController *g_voteControllerGlobal = NULL;
LINK_ENTITY_TO_CLASS( vote_controller, CVoteController );
IMPLEMENT_SERVERCLASS_ST( CVoteController, DT_VoteController )
	SendPropInt( SENDINFO_NAME( m_iActiveIssueIndex, m_activeIssueIndex ), 32 ),
	SendPropInt( SENDINFO_NAME( m_iOnlyTeamToVote, m_onlyTeamToVote ), 32 ),
	SendPropInt( SENDINFO_NAME( m_nVotesYes, m_votesYes ), 32 ),
	SendPropInt( SENDINFO_NAME( m_nVotesNo, m_votesNo ), 32 ),
	SendPropInt( SENDINFO_NAME( m_nPotentialVotes, m_potentialVotes ), 32 ),
END_SEND_TABLE()
BEGIN_DATADESC( CVoteController )
	DEFINE_THINKFUNC( VoteControllerThink ),
END_DATADESC()

namespace
{
	int VoteTeam( const CBaseEntity *entity )
	{
		if ( !entity ) return TEAM_UNASSIGNED;
		const int team = entity->GetTeamNumber();
		return team == TEAM_SPECTATOR ? TEAM_SURVIVOR : team;
	}
	void StartVoteTimer( CountdownTimer &timer, float duration )
	{
		timer.m_timestamp = (float)( (double)gpGlobals->curtime + (double)duration );
		timer.m_duration = duration;
	}
	bool IsPositiveVoteTimestamp( float timestamp )
	{
		const unsigned int bits = FloatBits( timestamp );
		return bits > 0u && bits <= 0x7f800000u;
	}
	bool VoteTimerElapsed( const CountdownTimer &timer )
	{
		// Native positive timestamp admission followed by ordered Now>=timestamp.
		// Integer IEEE ordering keeps those guards intact under the target /fp:fast.
		const unsigned int deadline = FloatBits( timer.m_timestamp );
		const unsigned int now = FloatBits( gpGlobals->curtime );
		return deadline > 0u && deadline <= 0x7f800000u && now <= 0x7f800000u && now >= deadline;
	}
	bool VoteFloatAbove( float left, float right )
	{
		unsigned int a = FloatBits( left ), b = FloatBits( right );
		if ( ( a & 0x7fffffffu ) > 0x7f800000u || ( b & 0x7fffffffu ) > 0x7f800000u ) return false;
		if ( ( a & 0x7fffffffu ) == 0u ) a = 0u;
		if ( ( b & 0x7fffffffu ) == 0u ) b = 0u;
		a = ( a & 0x80000000u ) ? ~a : a | 0x80000000u;
		b = ( b & 0x80000000u ) ? ~b : b | 0x80000000u;
		return a > b;
	}
	bool IsSingleChapterVoteMode()
	{
		// Native2D9C10 reads current AND base metadata, with the base value as
		// the current-mode default. The sole production mode dictionary owns both.
		KeyValues *mode = g_TerrorModes.GetCurrentMode();
		KeyValues *base = g_TerrorModes.FindMode( g_TerrorModes.GetBaseName() );
		return mode && base && mode->GetInt( "singlechapter", base->GetInt( "singlechapter", 0 ) != 0 ) != 0;
	}
}

CVoteController::CVoteController()
{
	// Native21A930 constructs exactly three CountdownTimers and two vectors.
	// ResetData/scalar writers belong Spawn21B980, not the constructor.
}
CVoteController::~CVoteController()
{
	g_voteControllerGlobal = NULL; // Native unconditional clear, before issue destruction.
	FOR_EACH_VEC( m_potentialIssues, i ) delete m_potentialIssues[i];
	m_callerHistory.Purge();
	m_potentialIssues.Purge();
}
void CVoteController::Spawn()
{
	ResetData();
	BaseClass::Spawn();
	SetThink( &CVoteController::VoteControllerThink );
	SetNextThink( gpGlobals->curtime );
	m_nVoteHoldCount = 0;
	g_voteControllerGlobal = this;
}
int CVoteController::UpdateTransmitState()
{
	return SetTransmitState( FL_EDICT_ALWAYS ); // Native21A0C0 -> SetTransmitState(8).
}
void CVoteController::RegisterIssue( CBaseIssue *issue )
{
	m_potentialIssues.AddToTail( issue );
}
void CVoteController::ResetData()
{
	m_iActiveIssueIndex = -1;
	m_nVotesYes = 0;
	m_nVotesNo = 0;
	m_nPotentialVotes = 0;
	m_acceptingVotesTimer.Invalidate();
	m_executeCommandTimer.Invalidate();
	CallerEntityIndex() = -1;
	m_nTeamHoldingVote = -1;
	m_iOnlyTeamToVote = -1;
	m_nCallerAccountID = 0;
	for ( int i = 0; i < 32; ++i ) m_playerBallotsAndCaller[i] = 2;
	// Native21AB90 retains hold count, caller history and reset timer.
}
void CVoteController::HoldVotes()
{
	m_nVoteHoldCount = (int)( (unsigned int)m_nVoteHoldCount + 1u );
}
void CVoteController::ReleaseVotes()
{
	m_nVoteHoldCount = (int)( (unsigned int)m_nVoteHoldCount - 1u );
}
bool CVoteController::CanTeamVote( int team ) const
{
	return m_iOnlyTeamToVote == -1 || team == m_iOnlyTeamToVote;
}
void CVoteController::ListIssues( CBasePlayer *forWhom )
{
	if ( !sv_allow_votes.GetBool() ) return;
	ClientPrint( forWhom, HUD_PRINTCONSOLE, "---Vote commands---\n" );
	FOR_EACH_VEC( m_potentialIssues, i ) m_potentialIssues[i]->ListIssueDetails( forWhom );
	ClientPrint( forWhom, HUD_PRINTCONSOLE, "--- End Vote commands---\n" );
}
void CVoteController::SendCallVoteFailed( CBasePlayer *player, int reason )
{
	if ( !player ) return; // Exact original21A3C0 guard.
	CSingleUserRecipientFilter filter( player );
	filter.MakeReliable();
	UserMessageBegin( filter, "CallVoteFailed" );
		WRITE_BYTE( reason );
	MessageEnd();
}
bool CVoteController::CanCallVote( uint32 accountID ) const
{
	// Native25BED0 is a REQUIRED real MatchSession capability, not sv_lan or
	// an empty snapshot. Its unsupported target acquisition remains an explicit
	// integration blocker; no false/default implementation is supplied here.
	if ( accountID == 0 || TheDirector->IsOfflineSession() ) return true;
	FOR_EACH_VEC( m_callerHistory, i )
	{
		const CallerHistory_t &record = m_callerHistory[i];
		if ( record.accountID != accountID ) continue;
		const float elapsed = gpGlobals->realtime - record.realtime; // Native SUBSS.
		//21A67D COMISS/JA rejects ONLY ordered creation>elapsed; NaN continues.
		if ( VoteFloatAbove( sv_vote_creation_timer.GetFloat(), elapsed ) ) return false;
		if ( record.failedVotes >= sv_vote_plr_map_limit.GetInt() ) return false;
	}
	return true;
}
void CVoteController::RecordVoteResult( bool passed )
{
	if ( m_nCallerAccountID == 0 ) return;
	int index = 0;
	for ( ; index < m_callerHistory.Count(); ++index )
		if ( m_callerHistory[index].accountID == m_nCallerAccountID ) break;
	if ( index == m_callerHistory.Count() )
	{
		index = m_callerHistory.AddToTail();
		m_callerHistory[index].accountID = m_nCallerAccountID;
		m_callerHistory[index].passedVotes = 0;
		m_callerHistory[index].failedVotes = 0;
	}
	CallerHistory_t &record = m_callerHistory[index];
	record.realtime = gpGlobals->realtime;
	if ( passed ) ++record.passedVotes;
	else ++record.failedVotes;
	record.issueIndex = m_iActiveIssueIndex;
	record.lastVotePassed = passed;
}
void CVoteController::SendVoteFailed()
{
	CBaseIssue *issue = m_potentialIssues[m_iActiveIssueIndex];
	UTIL_LogPrintf( "Vote failed \"%s %s\"\n", issue->GetTypeString(), issue->GetDetailsString() );
	CBroadcastRecipientFilter filter;
	filter.MakeReliable();
	UserMessageBegin( filter, "VoteFail" );
		WRITE_BYTE( m_iOnlyTeamToVote );
	MessageEnd();
}
void CVoteController::CancelVote()
{
	if ( m_iActiveIssueIndex == -1 ) return;
	SendVoteFailed();
	m_potentialIssues[m_iActiveIssueIndex]->OnVoteFailed();
	m_acceptingVotesTimer.Invalidate();
	StartVoteTimer( m_resetVoteTimer, 5.0f );
	// Original21A7E0 does NOT RecordVoteResult(false), ResetData, clear the
	// issue, or invalidate a command already scheduled after a passed vote.
}
void CVoteController::CheckForEarlyVoteClose()
{
	if ( m_nVotesYes + m_nVotesNo < m_nPotentialVotes ) return;
	m_acceptingVotesTimer.m_timestamp = gpGlobals->curtime;
	m_acceptingVotesTimer.m_duration = 0.0f;
}
CVoteController::TryCastVoteResult CVoteController::TryCastVote( int entityIndex, const char *choice )
{
	if ( !sv_allow_votes.GetBool() ) return CAST_FAIL_SERVER_DISABLE;
	if ( entityIndex > 32 ) return CAST_FAIL_SYSTEM_ERROR;
	if ( m_iActiveIssueIndex == -1 ) return CAST_FAIL_NO_ACTIVE_ISSUE;
	if ( IsPositiveVoteTimestamp( m_executeCommandTimer.m_timestamp ) ) return CAST_FAIL_VOTE_CLOSED;
	CBaseIssue *issue = m_potentialIssues[m_iActiveIssueIndex];
	if ( issue->IsTeamRestrictedVote() )
	{
		CBaseEntity *entity = UTIL_EntityByIndex( entityIndex );
		if ( !entity || m_nTeamHoldingVote != VoteTeam( entity ) ) return CAST_FAIL_TEAM_RESTRICTED;
	}
	if ( m_playerBallotsAndCaller[entityIndex] != 2 ) return CAST_FAIL_NO_CHANGES;
	const bool yes = !Q_stricmp( choice, "Yes" );
	if ( !yes && Q_stricmp( choice, "No" ) ) return CAST_FAIL_SYSTEM_ERROR;
	IGameEvent *event = gameeventmanager->CreateEvent( yes ? "vote_cast_yes" : "vote_cast_no" );
	if ( event )
	{
		event->SetInt( "team", m_iOnlyTeamToVote );
		event->SetInt( "entityid", entityIndex );
		gameeventmanager->FireEvent( event );
	}
	m_playerBallotsAndCaller[entityIndex] = yes ? 0 : 1;
	if ( yes ) ++m_nVotesYes;
	else ++m_nVotesNo;
	CheckForEarlyVoteClose();
	CSingleUserRecipientFilter filter( UTIL_PlayerByIndex( entityIndex ) );
	filter.MakeReliable();
	UserMessageBegin( filter, "VoteRegistered" );
		WRITE_BYTE( yes ? 1 : 0 );
	MessageEnd();
	return CAST_OK;
}
bool CVoteController::CreateVote( int entityIndex, const char *issueType, const char *details )
{
	if ( !sv_allow_votes.GetBool() || m_iActiveIssueIndex != -1 ) return false;
	CBasePlayer *player = UTIL_PlayerByIndex( entityIndex );
	if ( !player || player->GetTeamNumber() == TEAM_UNASSIGNED || m_nVoteHoldCount != 0 ) return false;
	uint32 accountID = 0;
	const CSteamID *steamID = engine->GetClientSteamID( player->edict() );
	if ( steamID ) accountID = steamID->GetAccountID();
	if ( !CanCallVote( accountID ) )
	{
		SendCallVoteFailed( player, 2 );
		return false;
	}
	m_nCallerAccountID = accountID;
	char callerName[64];
	if ( sv_vote_show_caller.GetBool() ) Q_strncpy( callerName, player->GetPlayerName(), sizeof( callerName ) );
	else callerName[0] = '\0';
	int index = 0;
	for ( ; index < m_potentialIssues.Count(); ++index )
		if ( !Q_stricmp( issueType, m_potentialIssues[index]->GetTypeString() ) ) break;
	if ( index == m_potentialIssues.Count() ) return false;
	CBaseIssue *issue = m_potentialIssues[index];
	char error[MAX_CREATE_ERROR_STRING];
	error[0] = '\0';
	if ( !issue->CanCallVote( entityIndex, details, error ) ) return false;
	// The original discards this error string; do not add TF feedback/cooldowns.
	m_nVotesYes = 0;
	m_nVotesNo = 0;
	m_iActiveIssueIndex = index;
	issue->SetIssueDetails( details );
	CallerEntityIndex() = entityIndex;
	m_nTeamHoldingVote = VoteTeam( player );
	m_iOnlyTeamToVote = issue->IsTeamRestrictedVote() ? VoteTeam( player ) : -1;
	m_nPotentialVotes = issue->CountPotentialVoters();
	StartVoteTimer( m_acceptingVotesTimer, sv_vote_timer_duration.GetFloat() );
	CBroadcastRecipientFilter filter;
	filter.MakeReliable();
	char displayDetails[64];
	Q_strncpy( displayDetails, issue->GetDetailsString(), sizeof( displayDetails ) );
	UserMessageBegin( filter, "VoteStart" );
		WRITE_BYTE( m_iOnlyTeamToVote );
		WRITE_BYTE( CallerEntityIndex() );
		WRITE_STRING( issue->GetDisplayString() );
		WRITE_STRING( displayDetails );
		WRITE_STRING( callerName );
	MessageEnd();
	char identity[256];
	static_cast< CEventLog * >( GameLogSystem() )->GetEntityIdentity( player, identity, sizeof( identity ) );
	if ( details && *details ) UTIL_LogPrintf( "%s called vote \"%s %s\"\n", identity, issueType, details );
	else UTIL_LogPrintf( "%s called vote \"%s\"\n", identity, issueType );
	TryCastVote( entityIndex, "Yes" );
	issue->OnVoteStarted();
	return true;
}
void CVoteController::VoteControllerThink()
{
	if ( m_iActiveIssueIndex < 0 || m_iActiveIssueIndex >= m_potentialIssues.Count() )
	{
		SetNextThink( gpGlobals->curtime + 0.5f );
		return;
	}
	if ( VoteTimerElapsed( m_acceptingVotesTimer ) )
	{
		m_acceptingVotesTimer.Invalidate();
		CBaseIssue *issue = m_potentialIssues[m_iActiveIssueIndex];
		const char *type = issue->GetTypeString();
		const int length = Q_strlen( type );
		const bool allTalk = !Q_strncmp( type, "ChangeAllTalk", length );
		const bool versusRestart = TerrorGameRules()->IsVersusMode() && !Q_strncmp( type, "RestartGame", length );
		bool normalCount = true, teamPass = false;
		if ( allTalk || versusRestart )
		{
			normalCount = false;
			float survivorCount = 0.0f, survivorPossibleYes = 0.0f;
			float infectedCount = 0.0f, infectedPossibleYes = 0.0f;
			// Original scans0..31, NOT1..32, and treats every non-NO ballot
			// (including unvoted2) as a possible yes for this per-team policy.
			for ( int i = 0; i < 32; ++i )
			{
				CBasePlayer *player = UTIL_PlayerByIndex( i );
				if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() || player->IsBot() ) continue;
				if ( player->GetTeamNumber() == TEAM_SURVIVOR )
				{
					survivorCount += 1.0f;
					if ( m_playerBallotsAndCaller[i] != 1 ) survivorPossibleYes += 1.0f;
				}
				else if ( player->GetTeamNumber() == TEAM_INFECTED )
				{
					infectedCount += 1.0f;
					if ( m_playerBallotsAndCaller[i] != 1 ) infectedPossibleYes += 1.0f;
				}
			}
			const bool survivorPass = survivorPossibleYes > survivorCount * 0.5f;
			const bool infectedPass = infectedPossibleYes > infectedCount * 0.5f;
			teamPass = versusRestart || !sv_alltalk.GetBool() ? survivorPass && infectedPass : survivorPass || infectedPass;
		}
		if ( ( m_nVotesYes > m_nVotesNo && normalCount ) || teamPass )
		{
			RecordVoteResult( true );
			UTIL_LogPrintf( "Vote succeeded \"%s %s\"\n", issue->GetTypeString(), issue->GetDetailsString() );
			StartVoteTimer( m_executeCommandTimer, sv_vote_command_delay.GetFloat() );
			issue->OnVotePassed();
			char details[64];
			Q_strncpy( details, issue->GetDetailsString(), sizeof( details ) );
			CBroadcastRecipientFilter filter;
			filter.MakeReliable();
			UserMessageBegin( filter, "VotePass" );
				WRITE_BYTE( m_iOnlyTeamToVote );
				WRITE_STRING( issue->GetVotePassedString() );
				WRITE_STRING( details );
			MessageEnd();
			CBaseEntity *caller = UTIL_EntityByIndex( CallerEntityIndex() );
			if ( caller && caller->IsPlayer() ) static_cast< CBasePlayer * >( caller )->m_lastVoteCallTimer.Invalidate();
			StartVoteTimer( m_resetVoteTimer, 5.0f );
			// Actual2A5940 local receiver. Only unavailable uploader reason3/4
			// setter calls are omitted; native collection/submission remains live.
			L4DGameStats().OnVotePassed( issue->GetVotePassedString() );
		}
		else
		{
			RecordVoteResult( false );
			SendVoteFailed();
			issue->OnVoteFailed();
			StartVoteTimer( m_resetVoteTimer, 5.0f );
		}
	}
	if ( VoteTimerElapsed( m_executeCommandTimer ) )
	{
		m_executeCommandTimer.Invalidate();
		m_potentialIssues[m_iActiveIssueIndex]->ExecuteCommand();
	}
	if ( VoteTimerElapsed( m_resetVoteTimer ) )
	{
		ResetData();
		m_resetVoteTimer.Invalidate();
	}
	SetNextThink( gpGlobals->curtime + 0.5f );
}

CBaseIssue::CBaseIssue( const char *typeString )
{
	Q_strcpy( m_szTypeString, typeString );
	g_voteControllerGlobal->RegisterIssue( this );
}
const char *CBaseIssue::GetDetailsString() { return m_szDetailsString; }
void CBaseIssue::SetIssueDetails( const char *details ) { Q_strcpy( m_szDetailsString, details ); }
void CBaseIssue::ListStandardNoArgCommand( CBasePlayer *forWhom, const char *issueString )
{
	ClientPrint( forWhom, HUD_PRINTCONSOLE, "callvote %s1\n", issueString );
}
bool CBaseIssue::CanCallVote( int entityIndex, const char *details, char *error )
{
	if ( entityIndex == -1 ) return false;
	CBaseEntity *entity = UTIL_EntityByIndex( entityIndex );
	if ( !entity || CanTeamCallVote( VoteTeam( entity ) ) ) return true;
	CTeam *team = entity->GetTeam();
	if ( team ) Q_snprintf( error, MAX_CREATE_ERROR_STRING, "The %s team cannot call this vote.", team->GetName() );
	else Q_strncpy( error, "Your team cannot call this vote.", MAX_CREATE_ERROR_STRING );
	return false;
}
int CBaseIssue::CountPotentialVoters()
{
	int count = 0;
	for ( int i = 1; i <= 32; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( player && player->IsConnected() && !player->IsBot() && g_voteControllerGlobal->CanTeamVote( VoteTeam( player ) ) ) ++count;
	}
	return count;
}

// Actual native RTTI CBaseTerrorIssue. No replacement Director/session host.
class CBaseTerrorIssue : public CBaseIssue
{
public:
	CBaseTerrorIssue( const char *type ) : CBaseIssue( type ) {}
	virtual int CountPotentialVoters() OVERRIDE
	{
		int count = CBaseIssue::CountPotentialVoters();
		int total = 0, restoring = 0;
		if ( g_voteControllerGlobal->CanTeamVote( TEAM_SURVIVOR ) )
		{
			TerrorGameRules()->GetRestoringPlayerCounts( TEAM_SURVIVOR, &total, &restoring );
			count += total - restoring;
		}
		if ( g_voteControllerGlobal->CanTeamVote( TEAM_INFECTED ) )
		{
			total = restoring = 0;
			TerrorGameRules()->GetRestoringPlayerCounts( TEAM_INFECTED, &total, &restoring );
			count += total - restoring;
		}
		return count;
	}
	virtual bool CanCallVote( int entityIndex, const char *details, char *error ) OVERRIDE
	{
		if ( !CBaseIssue::CanCallVote( entityIndex, details, error ) ) return false;
		int survivorTotal = 0, survivorRestoring = 0, infectedTotal = 0, infectedRestoring = 0;
		TerrorGameRules()->GetRestoringPlayerCounts( TEAM_SURVIVOR, &survivorTotal, &survivorRestoring );
		TerrorGameRules()->GetRestoringPlayerCounts( TEAM_INFECTED, &infectedTotal, &infectedRestoring );
		if ( survivorTotal + infectedTotal > 0 )
		{
			CVoteController::SendCallVoteFailed( UTIL_PlayerByIndex( entityIndex ), 1 );
			return false;
		}
		// Native353100's optional start-area branch is unreachable for ALL
		// seven original issue vtables: their virtual56 is2D8CB0 RETfalse.
		// Elide that proven branch rather than publishing a fake policy API.
		return !TheDirector->IsFinaleVehicleLeaving() && !TheDirector->IsFinaleWon();
	}
};
class CChangeDifficultyIssue : public CBaseTerrorIssue
{
public:
	CChangeDifficultyIssue() : CBaseTerrorIssue( "ChangeDifficulty" ) {}
	virtual const char *GetDetailsString() OVERRIDE
	{
		// Native353240 uses CASE-SENSITIVE comparison, although admission is
		// case-insensitive. Lowercase valid input can therefore display empty.
		if ( !Q_strcmp( m_szDetailsString, "Easy" ) ) return "#L4D_DifficultyEasy";
		if ( !Q_strcmp( m_szDetailsString, "Normal" ) ) return "#L4D_DifficultyNormal";
		if ( !Q_strcmp( m_szDetailsString, "Hard" ) ) return "#L4D_DifficultyHard";
		if ( !Q_strcmp( m_szDetailsString, "Impossible" ) ) return "#L4D_DifficultyImpossible";
		return "";
	}
	virtual bool CanCallVote( int entityIndex, const char *details, char *error ) OVERRIDE
	{
		if ( !CBaseTerrorIssue::CanCallVote( entityIndex, details, error ) || !g_TerrorModes.HasDifficulty() ) return false;
		if ( !sv_vote_issue_change_difficulty_allowed.GetBool() )
		{
			Q_strncpy( error, "Server has disabled that Issue.", MAX_CREATE_ERROR_STRING ); return false;
		}
		if ( Q_stricmp( details, "Impossible" ) && Q_stricmp( details, "Normal" ) && Q_stricmp( details, "Hard" ) && Q_stricmp( details, "Easy" ) )
		{
			Q_strncpy( error, "Needs arg of Easy, Normal, Hard, or Impossible.", MAX_CREATE_ERROR_STRING ); return false;
		}
		ConVarRef difficulty( "z_difficulty" );
		if ( !Q_stricmp( details, difficulty.GetString() ) )
		{
			Q_snprintf( error, MAX_CREATE_ERROR_STRING, "Difficulty is already %s.", details ); return false;
		}
		return true;
	}
	virtual bool IsTeamRestrictedVote() const OVERRIDE { return true; }
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_change_difficulty"; }
	virtual void ExecuteCommand() OVERRIDE { ConVarRef difficulty( "z_difficulty" ); difficulty.SetValue( m_szDetailsString ); }
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE
	{
		if ( !sv_vote_issue_change_difficulty_allowed.GetBool() ) return;
		ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s Impossible\n", GetTypeString() ) );
		ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s Expert\n", GetTypeString() ) );
		ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s Hard\n", GetTypeString() ) );
		ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s Normal\n", GetTypeString() ) );
	}
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_change_difficulty"; }
};
class CRestartGameIssue : public CBaseTerrorIssue
{
public:
	CRestartGameIssue() : CBaseTerrorIssue( "RestartGame" ) {}
	virtual bool CanCallVote( int entityIndex, const char *details, char *error ) OVERRIDE
	{
		if ( !CBaseTerrorIssue::CanCallVote( entityIndex, details, error ) ) return false;
		if ( !sv_vote_issue_restart_game_allowed.GetBool() )
		{
			Q_strncpy( error, "Server has disabled that Issue.", MAX_CREATE_ERROR_STRING ); return false;
		}
		return !TerrorGameRules()->IsScavengeMode();
	}
	virtual const char *GetDisplayString() OVERRIDE
	{
		if ( TerrorGameRules()->IsVersusMode() ) return "#L4D_vote_versus_level_restart";
		return TerrorGameRules()->IsSurvivalMode() ? "#L4D_vote_restart_chapter" : "#L4D_vote_restart_game";
	}
	virtual void ExecuteCommand() OVERRIDE
	{
		if ( TerrorGameRules()->IsSurvivalMode() ) TheDirector->GetSurvivalMode()->RestartRoundFromVote();
		else if ( TerrorGameRules()->IsVersusMode() ) TheDirector->GetVersusMode()->RestartScenarioFromVote();
		else
		{
			const char *map = STRING( gpGlobals->mapname );
			if ( !map ) map = "";
			if ( !IsSingleChapterVoteMode() )
			{
				KeyValues *first = TerrorGameRules()->GetFirstChapterInfo();
				if ( first ) map = first->GetString( "map", map );
			}
			TheDirector->RestartScenarioFromVote( map );
		}
	}
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE
	{
		if ( sv_vote_issue_restart_game_allowed.GetBool() ) ListStandardNoArgCommand( forWhom, GetTypeString() );
	}
	virtual const char *GetVotePassedString() OVERRIDE
	{
		if ( TerrorGameRules()->IsVersusMode() ) return "#L4D_vote_passed_versus_level_restart";
		return TerrorGameRules()->IsSurvivalMode() ? "#L4D_vote_passed_restart_chapter" : "#L4D_vote_passed_restart_game";
	}
};
class CReturnToLobbyIssue : public CBaseTerrorIssue
{
public:
	CReturnToLobbyIssue() : CBaseTerrorIssue( "ReturnToLobby" ) {}
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_return_to_lobby"; }
	virtual void ExecuteCommand() OVERRIDE
	{
		if ( TerrorGameRules()->IsCoopMode() ) TheDirector->StartScenarioExit();
		else TheDirector->FinishScenarioExit();
	}
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE { ListStandardNoArgCommand( forWhom, GetTypeString() ); }
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_return_to_lobby"; }
};
class CKickIssue : public CBaseTerrorIssue
{
public:
	CKickIssue() : CBaseTerrorIssue( "Kick" ), m_bDoNotBan( false ) { m_networkID[0] = '\0'; }
	virtual bool CanCallVote( int entityIndex, const char *details, char *error ) OVERRIDE
	{
		// Original353A40 deliberately calls BASE21A1A0, not353100's finale/
		// restoring/start-area policy. Do not route it through the common override.
		if ( !CBaseIssue::CanCallVote( entityIndex, details, error ) ) return false;
		if ( !sv_vote_issue_kick_allowed.GetBool() )
		{
			Q_strncpy( error, "Server has disabled that Issue.", MAX_CREATE_ERROR_STRING ); return false;
		}
		const int userID = atoi( details );
		CBasePlayer *target = userID >= 0 ? UTIL_PlayerByUserId( userID ) : NULL;
		if ( !target || !target->IsPlayer() )
		{
			Q_strncpy( error, "Requires valid player.", MAX_CREATE_ERROR_STRING ); return false;
		}
		CBasePlayer *caller = ToBasePlayer( UTIL_EntityByIndex( entityIndex ) );
		if ( caller )
		{
			const int targetTeam = target->GetTeamNumber();
			const bool sameSide = caller->GetTeamNumber() == TEAM_INFECTED ? targetTeam == TEAM_INFECTED :
				targetTeam == TEAM_SURVIVOR || targetTeam == TEAM_SPECTATOR;
			if ( target->IsSplitScreenPartner( caller ) ) return false;
			if ( !engine->IsDedicatedServer() )
			{
				if ( target->entindex() == 1 ) return false;
				CBasePlayer *owner = target->GetSplitScreenPlayerOwner();
				if ( owner && owner->entindex() == 1 ) return false;
			}
			if ( !sameSide ) return false;
		}
		return true;
	}
	virtual const char *GetDetailsString() OVERRIDE
	{
		CBasePlayer *target = UTIL_PlayerByUserId( atoi( m_szDetailsString ) );
		return target ? target->GetPlayerName() : "unnamed";
	}
	virtual bool IsTeamRestrictedVote() const OVERRIDE { return true; }
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_kick_player"; }
	virtual void OnVoteStarted() OVERRIDE
	{
		CBasePlayer *target = NULL;
		const int userID = atoi( m_szDetailsString );
		if ( userID >= 0 )
		{
			target = UTIL_PlayerByUserId( userID );
			if ( target && !target->IsPlayer() ) target = NULL;
			if ( target && !ToTerrorPlayer( target )->IsIdle() && target->GetTeamNumber() != TEAM_SPECTATOR )
				g_voteControllerGlobal->TryCastVote( target->entindex(), "No" );
		}
		m_bDoNotBan = false;
		m_networkID[0] = '\0';
		if ( !target ) return;
		INetChannelInfo *channel = engine->GetPlayerNetInfo( target->entindex() );
		if ( channel && !channel->IsTimingOut() )
		{
			Q_strncpy( m_networkID, target->GetNetworkIDString(), sizeof( m_networkID ) );
			return;
		}
		const double idleTime = channel ? channel->GetTimeSinceLastReceived() : 0.0;
		DevMsg( "Will not ban kicked player: net channel was idle for %.2f sec.\n", idleTime );
		m_bDoNotBan = true;
	}
	virtual void ExecuteCommand() OVERRIDE
	{
		const int userID = atoi( m_szDetailsString );
		CBasePlayer *target = userID >= 0 ? UTIL_PlayerByUserId( userID ) : NULL;
		if ( target && target->IsPlayer() )
		{
			if ( sv_vote_kick_ban_duration.GetInt() > 0 && !m_bDoNotBan )
				engine->ServerCommand( CFmtStr( "banid %d %d;", sv_vote_kick_ban_duration.GetInt(), target->GetUserID() ) );
			engine->ServerCommand( CFmtStr( "kickid %d You have been voted off;", target->GetUserID() ) );
		}
		else if ( m_networkID[0] && sv_vote_kick_ban_duration.GetInt() > 0 && !m_bDoNotBan )
			engine->ServerCommand( CFmtStr( "banid %d %s;", sv_vote_kick_ban_duration.GetInt(), m_networkID ) );
	}
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE
	{
		ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s <userID>\n", GetTypeString() ) );
	}
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_kick_player"; }
private:
	bool m_bDoNotBan; // Native132; native constructor initializes just this and networkID[0].
	char m_networkID[128]; // Native133; actual OnVoteStarted ownership/copy.
};
class CChangeMissionIssue : public CBaseTerrorIssue
{
public:
	CChangeMissionIssue() : CBaseTerrorIssue( "ChangeMission" ) {}
	virtual const char *GetDetailsString() OVERRIDE
	{
		KeyValues *details = TerrorBuildActiveServerGameProjection();
		if ( !details ) return m_szDetailsString;
		details->SetString( "Game/campaign", m_szDetailsString ); details->SetInt( "Game/chapter", 1 );
		KeyValues *mission = NULL;
		KeyValues *chapter = g_TerrorModes.GetChapterInfo( details, &mission );
		const char *text = chapter ? mission->GetString( "DisplayTitle", "" ) : m_szDetailsString;
		details->deleteThis();
		return text; // Borrowed dictionary string survives owned projection deletion.
	}
	virtual bool CanCallVote( int entityIndex, const char *name, char *error ) OVERRIDE
	{
		if ( !CBaseTerrorIssue::CanCallVote( entityIndex, name, error ) || TerrorGameRules()->IsSurvivalMode() || TerrorGameRules()->IsScavengeMode() ) return false;
		if ( !sv_vote_issue_change_mission_allowed.GetBool() )
		{
			Q_strncpy( error, "Server has disabled that Issue.", MAX_CREATE_ERROR_STRING ); return false;
		}
		if ( !CBaseTerrorIssue::CanCallVote( entityIndex, name, error ) ) return false; // Original second call retained.
		KeyValues *details = TerrorBuildActiveServerGameProjection();
		if ( !details ) return false; // Native353CB0 has this owned-details guard.
		details->SetString( "Game/campaign", name ); details->SetInt( "Game/chapter", 1 );
		const bool valid = g_TerrorModes.GetChapterInfo( details, NULL ) != NULL;
		if ( !valid ) Q_strncpy( error, "Not a valid mission.", MAX_CREATE_ERROR_STRING );
		details->deleteThis();
		return valid;
	}
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_mission_change"; }
	virtual void ExecuteCommand() OVERRIDE { TheDirector->ChangeMissionFromVote( m_szDetailsString ); }
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE
	{
		if ( sv_vote_issue_change_mission_allowed.GetBool() ) ListStandardNoArgCommand( forWhom, GetTypeString() );
	}
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_mission_change"; }
};
class CChangeChapterIssue : public CBaseTerrorIssue
{
public:
	CChangeChapterIssue() : CBaseTerrorIssue( "ChangeChapter" ) {}
	virtual const char *GetDetailsString() OVERRIDE
	{
		KeyValues *details = TerrorBuildActiveServerGameProjection();
		KeyValues *chapter = g_TerrorModes.GetChapterInfoByBspName( details, m_szDetailsString, NULL );
		const char *text = chapter ? chapter->GetString( "DisplayName", "" ) : m_szDetailsString;
		if ( details ) details->deleteThis();
		return text;
	}
	virtual bool CanCallVote( int entityIndex, const char *map, char *error ) OVERRIDE
	{
		if ( !CBaseTerrorIssue::CanCallVote( entityIndex, map, error ) ||
			( !TerrorGameRules()->IsSurvivalMode() && !TerrorGameRules()->IsScavengeMode() ) ||
			!CBaseTerrorIssue::CanCallVote( entityIndex, map, error ) ) return false;
		KeyValues *details = TerrorBuildActiveServerGameProjection();
		const bool valid = g_TerrorModes.GetChapterInfoByBspName( details, map, NULL ) != NULL;
		if ( !valid ) Q_strncpy( error, "Not a valid chapter context.", MAX_CREATE_ERROR_STRING );
		if ( details ) details->deleteThis();
		return valid; // Native3535C0 does not read either change-map-allowed cvar.
	}
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_chapter_change"; }
	virtual void ExecuteCommand() OVERRIDE { TheDirector->ChangeChapterFromVote( m_szDetailsString ); }
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE { ListStandardNoArgCommand( forWhom, GetTypeString() ); }
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_chapter_change"; }
};
class CChangeAllTalkIssue : public CBaseTerrorIssue
{
public:
	CChangeAllTalkIssue() : CBaseTerrorIssue( "ChangeAllTalk" ) {}
	virtual const char *GetDetailsString() OVERRIDE { return sv_alltalk.GetBool() ? "#L4D_vote_alltalk_disable" : "#L4D_vote_alltalk_enable"; }
	virtual bool CanCallVote( int entityIndex, const char *details, char *error ) OVERRIDE
	{
		return CBaseTerrorIssue::CanCallVote( entityIndex, details, error ) && TerrorGameRules()->IsPlayerControlledZombies();
	}
	virtual const char *GetDisplayString() OVERRIDE { return "#L4D_vote_alltalk_change"; }
	virtual void ExecuteCommand() OVERRIDE { sv_alltalk.SetValue( !sv_alltalk.GetBool() ); }
	virtual void ListIssueDetails( CBasePlayer *forWhom ) OVERRIDE { ClientPrint( forWhom, HUD_PRINTCONSOLE, CFmtStr( "callvote %s\n", GetTypeString() ) ); }
	virtual const char *GetVotePassedString() OVERRIDE { return "#L4D_vote_passed_alltalk_change"; }
};
void TerrorCreateVoteIssues()
{
	// Exact2D94D0 registration order. The actual vote_controller must already
	// have been created AND Spawned; no NULL host, alternate registry or fallback.
	new CChangeDifficultyIssue;
	new CRestartGameIssue;
	new CKickIssue;
	new CChangeMissionIssue;
	new CReturnToLobbyIssue;
	new CChangeChapterIssue;
	new CChangeAllTalkIssue;
}
CON_COMMAND_F( listissues, "List all the issues that can be voted on." , 0x00000004)
{
	g_voteControllerGlobal->ListIssues( UTIL_GetCommandClient() );
}
CON_COMMAND_F( callvote, "Start a vote on an issue." , 0x00000004)
{
	Msg( "Potential vote being called\n" );
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( args.ArgC() < 2 ) g_voteControllerGlobal->ListIssues( player );
	else if ( player && g_voteControllerGlobal->CreateVote( player->entindex(), args[1], args.ArgC() >= 3 ? args[2] : "" ) )
		player->m_lastVoteCallTimer.Start();
}

// Native1016AC80 replaces the TF-shaped implementation in player.cpp for L4D2.
bool CBasePlayer::HandleVoteCommands( const CCommand &args )
{
	if ( !g_voteControllerGlobal || Q_stricmp( args[0], "Vote" ) ) return false;
	if ( args.ArgC() >= 2 )
	{
		char text[64];
		const char *choice = args[1];
		switch ( g_voteControllerGlobal->TryCastVote( entindex(), choice ) )
		{
		case CVoteController::CAST_OK: Q_snprintf( text, sizeof( text ), "Voting %s.", choice ); break;
		case CVoteController::CAST_FAIL_SERVER_DISABLE: Q_strncpy( text, "Vote failed: server disabled.", sizeof( text ) ); break;
		case CVoteController::CAST_FAIL_NO_ACTIVE_ISSUE: Q_strncpy( text, "Vote failed: no active issue.", sizeof( text ) ); break;
		case CVoteController::CAST_FAIL_TEAM_RESTRICTED: Q_strncpy( text, "Vote failed: team restricted.", sizeof( text ) ); break;
		case CVoteController::CAST_FAIL_NO_CHANGES: Q_strncpy( text, "Vote failed: no changing vote.", sizeof( text ) ); break;
		case CVoteController::CAST_FAIL_DUPLICATE: Q_snprintf( text, sizeof( text ), "Vote failed: already voting %s.", choice ); break;
		case CVoteController::CAST_FAIL_VOTE_CLOSED: Q_strncpy( text, "Vote failed: voting closed.", sizeof( text ) ); break;
		default: Q_strncpy( text, "Vote failed: system error.", sizeof( text ) ); break;
		}
		DevMsg( "%s", text );
	}
	return true;
}
