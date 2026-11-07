//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "l4d2/sound/ai_responsefollowup.h"
#include "l4d2/director/terror_director.h"
#include "functorutils.h"
#include "l4d2/content/terror_scene_file_cache.h"
#include "l4d2/player/terrorplayer.h"
#include "eventqueue.h"
#include "ai_basenpc.h"
#include "tier0/memdbgon.h"

extern ConVar rr_debugresponses;
// S00EE20: this is NOT GetResponseDuration (the complete scene length).
float GetSpeechDuration( AI_Response &response )
{
	if ( response.GetType() == RESPONSE_SCRIPT )
		return 0.0f;
	TerrorSceneCachedData_t data;
	if ( scenefilecache->GetSceneCachedData( response.GetResponsePtr(), &data ) )
		return data.lastSpeakSecs;
	return -1.0f;
}

// S0272C0. Mutates the caller's criteria before the derived/find phase.
// Native's global reserve hint only affects allocation, not this contract.
void CAI_Expresser::PrepareSpeechCriteria( AIConcept_t concept, AI_CriteriaSet &criteria, const char *modifiers )
{
	criteria.AppendCriteria( "concept", concept, 5.0f );
	AppendSpeechContext( criteria, modifiers );
	GatherSpeechCriteria( criteria );
}

// S25170/25B50/25BD0: share the existing native team-count collectors.
void AppendTeamSpeechCounts( AI_CriteriaSet &criteria )
{
	const int alive = CountLivingSurvivors( 0 );
	criteria.AppendCriteria( "NumberOfTeamAlive", CFmtStr( "%d", alive ) );
	criteria.AppendCriteria( "NumberOfTeamIncapacitated", CFmtStr( "%d", CountLivingSurvivors( 2 ) ) );
	class CountTeam
	{
	public:
		CountTeam() : m_count( 0 ) {}
		bool operator()( CBasePlayer *player )
		{
			if ( player->GetTeamNumber() == TEAM_SURVIVOR )
				++m_count;
			return true;
		}
		int m_count;
	} countTeam;
	ForEachPlayer( countTeam );
	criteria.AppendCriteria( "NumberOfTeamDead", CFmtStr( "%d", countTeam.m_count - alive ) );
}

void CAI_Expresser::GatherSpeechCriteria( AI_CriteriaSet &criteria )
{
	class AppendCharacterAlive
	{
	public:
		explicit AppendCharacterAlive( AI_CriteriaSet &criteria ) : m_criteria( criteria ) {}
		bool operator()( CBasePlayer *base )
		{
			CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
			if ( player->GetTeamNumber() == TEAM_SURVIVOR &&
				(unsigned int)player->GetSurvivorCharacter() <= 3 )
			{
				m_criteria.AppendCriteria( CFmtStr( "Is%sAlive", player->GetResponseCharacterName() ),
					player->IsAlive() ? "1" : "0", 1.0f );
			}
			return true;
		}
		AI_CriteriaSet &m_criteria;
	} appendCharacterAlive( criteria );
	ForEachPlayer( appendCharacterAlive );

	AppendTeamSpeechCounts( criteria );
	criteria.AppendCriteria( "gamemode", TheDirector->GetBaseGameMode() );
	GetOuter()->ModifyOrAppendCriteria( criteria );
	if ( !GetOuter()->IsPlayer() )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( 1 );
		if ( player )
			player->ModifyOrAppendPlayerCriteria( criteria );
	}
}

void CAI_Expresser::GatherDerivedSpeechCriteria( AI_CriteriaSet &criteria )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( GetOuter() );
	if ( player )
		player->AppendSubjectDistanceCriteria( criteria );
}

bool CAI_Expresser::FindResponseWithCriteria( AI_Response &response, AIConcept_t concept, AI_CriteriaSet &criteria )
{
	IResponseSystem *system = GetOuter()->GetResponseSystem();
	if ( !system || !GetOuter()->IsAlive() ) return false;
	AI_CriteriaSet worldContexts;
	criteria.InterceptWorldSetContexts( worldContexts );
	bool found = system->FindBestResponse( criteria, response, this );
	if ( rr_debugresponses.GetInt() == 4 &&
		( ( GetOuter()->MyNPCPointer() && ( GetOuter()->m_debugOverlays & OVERLAY_NPC_SELECTED_BIT ) ) || GetOuter()->IsPlayer() ) )
	{
		const char *name = GetOuter()->IsPlayer() ? static_cast< CBasePlayer * >( GetOuter() )->GetPlayerName() : GetOuter()->GetDebugName();
		if ( found ) Warning( "RESPONSERULES: %s spoke '%s'. Found response '%s'.\n", name, static_cast< const char * >( concept ), response.GetResponsePtr() );
		else Warning( "RESPONSERULES: %s spoke '%s'. Found no matching response.\n", name, static_cast< const char * >( concept ) );
	}
	if ( !found ) return false;
	CBaseEntity *world = CBaseEntity::Instance( engine->PEntityOfEntIndex( 0 ) );
	for ( int index = worldContexts.FirstCriterion(); index != -1; index = worldContexts.NextCriterion( index ) )
		world->AddContext( worldContexts.GetName( index ), worldContexts.GetValue( index ), 0.0f );
	// Commit precedes this empty-response rejection. Do not wait for audio or
	// dispatch success, rerun matching, or add an odds gate absent in S27360.
	return response.GetResponsePtr()[0] != '\0';
}

bool CAI_Expresser::SpeakWithCriteria( AIConcept_t concept, AI_CriteriaSet &criteria, char *chosen, size_t size, IRecipientFilter *filter )
{
	// S027650. The provider is a real scenario-exit state, not a constant
	// "speech enabled" default. Its writer must land before registration.
	if ( TheDirector->IsSpeechDisabled() )
		return false;
	GatherDerivedSpeechCriteria( criteria );
	AI_Response response;
	if ( !FindResponseWithCriteria( response, concept, criteria ) )
		return false;
	bool spoke = SpeakDispatchResponse( concept, response, filter, &criteria );
	if ( chosen )
		Q_strncpy( chosen, response.GetResponsePtr(), size );
	return spoke;
}

CAI_ExpresserWithFollowup::CAI_ExpresserWithFollowup( CBaseFlex *outer ) : CAI_Expresser( outer ) {}

bool CAI_ExpresserWithFollowup::SpeakDispatchResponse( AIConcept_t concept, AI_Response &response, IRecipientFilter *filter, const AI_CriteriaSet *queryCriteria )
{
	if ( !CAI_Expresser::SpeakDispatchResponse( concept, response, filter, queryCriteria ) )
		return false;

	const AI_ResponseFollowup &followup = response.GetFollowup();
	const AI_CriteriaSet *criteria = queryCriteria ? queryCriteria : response.GetCriteria();
	if ( followup.HasEntityIO() && criteria )
	{
		CBaseEntity *target = gEntList.FindEntityByName( NULL, followup.fireTarget.String() );
		if ( target )
			g_EventQueue.AddEvent( target, followup.fireInput.String(), followup.fireDelay, GetOuter(), GetOuter() );
	}
	const AI_ResponseScriptFollowup &script = response.GetScriptData().GetFollowup();
	bool hasConcept = followup.HasConcept();
	if ( !hasConcept && !script.function )
		return true;

	// S00F960: a script followup's delay overrides THEN's delay even when
	// THEN wins dispatch and the script callback is never enqueued.
	float delay = script.function ? script.delay : followup.delay;
	if ( delay < 0.0f )
	{
		delay = -delay;
	}
	else if ( response.GetType() == RESPONSE_SCENE || response.GetType() == RESPONSE_SCRIPT )
	{
		// Add even the missing-scene value -1; no clamp or duration fallback.
		delay += GetSpeechDuration( response );
	}
	else if ( hasConcept && response.GetType() == RESPONSE_PRINT )
	{
		// The native PRINT branch uses THEN's own delay, not the overriding
		// script delay used to reach this branch.
		delay = followup.delay;
	}
	else
	{
		Warning( "Response %s has a followup with unsupported response type %d", response.GetResponsePtr(), response.GetType() );
		return false;
	}

	AI_CriteriaSet empty;
	if ( hasConcept )
	{
		ResponseTarget_t target = ResponseQueueManager().ResolveTarget( followup.target.String(), GetOuter(), criteria ? *criteria : empty );
		QueueResponseConcept( followup.concept.String(), followup.contexts.String(), delay, target, GetOuter() );
	}
	else
	{
		QueueSpeechScriptFollowup( script, delay, GetOuter(), criteria ? *criteria : empty );
	}
	return true;
}
