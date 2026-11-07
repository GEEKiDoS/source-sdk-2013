//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/stats/l4d_awards.h"
#include "tier0/memdbgon.h"

static ConVar sb_min_attention_notice_time( "sb_min_attention_notice_time", "0.5", 0x00004004,
	"If someone looks at me longer than this, I'll notice" );

// S38F760: base award/fault dispatch happens before the bot's delayed reaction.
bool SurvivorBot::OnAwardEarned( int award, CBaseEntity *subject )
{
	const bool result = BaseClass::OnAwardEarned( award, subject );
	if ( GetAwardResponseType( award ) != 2 )
	{
		switch ( award )
		{
		case 14: case 15: case 16: case 17: case 18:
		case 19: case 20: case 21: case 26: case 81:
			SpeakConcept( "SurvivorBotAwardHurrah", 0, RandomFloat( 0.5f, 1.0f ), NULL );
			break;
		}
	}
	return result;
}

void SurvivorBot::SpeakConcept( const char *concept, int priority, float delay, CBaseEntity *target )
{
	// S38F600: recent history suppresses a new request, even if the pending
	// request was overwritten or never actually dispatched. No global queue.
	for ( int i = 0; i < m_speechHistoryCount; ++i )
	{
		if ( ( concept == m_speechHistory[i].concept || !Q_stricmp( concept, m_speechHistory[i].concept ) ) &&
			gpGlobals->curtime - m_speechHistory[i].time < 5.0f )
			return;
	}
	if ( m_pendingSpeechConcept && m_pendingSpeechPriority > priority )
		return;
	m_pendingSpeechConcept = concept;
	m_pendingSpeechPriority = priority;
	m_pendingSpeechTimer.Start( delay );
	m_pendingSpeechTarget = target;
	m_speechHistory[m_speechHistoryIndex].concept = concept;
	m_speechHistory[m_speechHistoryIndex].time = gpGlobals->curtime;
	if ( ++m_speechHistoryIndex >= 8 )
		m_speechHistoryIndex = 0;
	if ( m_speechHistoryCount < 8 )
		++m_speechHistoryCount;
}

void SurvivorBot::UpdatePendingSpeech()
{
	// Inline S39D7A0 tail (INextBot+92800 == player+109248), NOT S38F760:
	// that other function is the independently owned award callback.
	if ( !m_pendingSpeechConcept || m_pendingSpeechTimer.GetRemainingTime() > 0.0f )
		return;
	CBaseEntity *target = m_pendingSpeechTarget.Get();
	char modifiers[256];
	const char *context = NULL;
	if ( target )
	{
		GetBodyInterface()->AimHeadTowards( target, IBody::INTERESTING, 1.0f, NULL,
			"Looking at who I'm talking to", true, 0.25f );
		if ( target->IsPlayer() && ( target->GetTeamNumber() == TEAM_SURVIVOR || target->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
		{
			Q_snprintf( modifiers, sizeof( modifiers ), "subject:%s", static_cast< CTerrorPlayer * >( target )->GetResponseCharacterName() );
			context = modifiers;
		}
	}
	// Stored priority controls replacement only. The native dispatch is 1,
	// and the concept is consumed even if the real response query fails.
	SpeakIfAllowed( m_pendingSpeechConcept, 1, context );
	m_pendingSpeechConcept = NULL;
}

void SurvivorBot::UpdateHumanAttention()
{
	// S38E4D0 calls the real team-situation getter before its timer gates.
	SurvivorTeamSituation &team = GetTeamSituation();
	if ( m_attentionTimer.GetRemainingTime() > 0.0f || IsInCombat( false ) ||
		team.HasRecentlyFiredFriend() || IsProgressBarActive() )
		return;
	bool needHelp = IsIncapacitated() || m_isHangingFromTongue || IsHangingFromLedge();
	CTerrorPlayer *subject = NULL;
	float bestRange = FLT_MAX;
	for ( int i = 0; i < team.GetTeammateCount(); ++i )
	{
		CTerrorPlayer *player = team.GetTeammate( i );
		if ( player->IsFakeClient() || team.GetTeammateRange( i ) > 500.0f ||
			player->IsIncapacitated() || player->IsHangingFromLedge() || player->IsIT() )
			continue;
		Vector forward;
		player->EyeVectors( &forward );
		Vector toMe = EyePosition() - player->EyePosition();
		float range = VectorNormalize( toMe );
		if ( range < bestRange && DotProduct( forward, toMe ) > 0.98000002f &&
			GetVisionInterface()->IsAbleToSee( player, IVision::DISREGARD_FOV, NULL ) )
		{
			subject = player;
			bestRange = range;
		}
	}
	if ( !subject )
	{
		m_attentionSubject = NULL;
		m_attentionSubjectTimer.Invalidate();
		m_attentionTimer.Start( 0.25f );
		return;
	}
	m_attentionTimer.Start( 0.1f );
	if ( m_attentionSubject.Get() == subject )
	{
		if ( m_attentionSubjectTimer.GetElapsedTime() <= sb_min_attention_notice_time.GetFloat() )
			return;
		char modifiers[256];
		Q_snprintf( modifiers, sizeof( modifiers ), "Subject:%s", subject->GetResponseCharacterName() );
		SpeakIfAllowed( needHelp ? "SurvivorBotNoteHumanAttentionNeedHelp" : "SurvivorBotNoteHumanAttention", 1, modifiers );
		GetBodyInterface()->AimHeadTowards( subject, IBody::IMPORTANT, 1.0f, NULL,
			"Looking at friend who is looking at me", true, 0.25f );
		m_attentionTimer.Start( 1.0f );
		return;
	}
	m_attentionSubject = subject;
	m_attentionSubjectTimer.Start();
}
