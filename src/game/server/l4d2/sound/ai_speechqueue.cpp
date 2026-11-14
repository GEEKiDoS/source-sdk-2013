//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "l4d2/sound/flex_expresser.h"
#include "l4d2/player/terrorplayer.h"
#include "l4d2/player/bots/survivor_bot.h"
#include "l4d2/content/terror_model_precache.h"
#include "vscript_shared.h"
#include "tier0/memdbgon.h"

static ConVar rr_followup_maxdist( "rr_followup_maxdist", "1800", 0x00004004,
	"'then ANY' or 'then ALL' response followups will be dispatched only to characters within this distance." );
static ConVar rr_thenany_score_slop( "rr_thenany_score_slop", "0.0", 0x00004084,
	"When computing respondents for a 'THEN ANY' rule, all rule-matching scores within this much of the best score will be considered." );

static CResponseQueueManager g_ResponseQueueManager;
CResponseQueueManager &ResponseQueueManager() { return g_ResponseQueueManager; }

const char *GetSpeechResponseEntityName( CBaseEntity *entity )
{
	// S00F020 -> S00EFA0. This is not the UI survivor display name.
	if ( !entity )
		return "";
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( entity );
	if ( player )
	{
		if ( player->GetTeamNumber() == TEAM_INFECTED )
			return GetZombieClassName( player->GetZombieClass() );
		if ( player->GetTeamNumber() == TEAM_SURVIVOR || player->GetTeamNumber() == TEAM_L4D1_SURVIVOR )
			return player->GetResponseCharacterName();
	}
	return STRING( entity->GetEntityName() );
}

HSCRIPT CResponseQueueManager::GetScriptResponseTargets() const
{
	// S2C2D80 enumerates the registered speech owners, including dead ones.
	ScriptVariant_t table;
	g_pScriptVM->CreateTable( table );
	HSCRIPT result = table;
	FOR_EACH_VEC( m_Expressers, i )
	{
		CBaseEntity *entity = m_Expressers[i].outer.Get();
		if ( entity )
			g_pScriptVM->SetValue( result, GetSpeechResponseEntityName( entity ), ScriptVariant_t( entity->GetScriptInstance() ) );
	}
	return result;
}

CResponseQueueManager::CResponseQueueManager() : CAutoGameSystemPerFrame( "CResponseQueueManager" ), m_Concepts( 64, 64 ), m_Scripts( 64, 64 ) {}

void CResponseQueueManager::LevelShutdownPreEntity() { Clear(); }
void CResponseQueueManager::Shutdown() { Clear(); }
void CResponseQueueManager::Clear()
{
	m_Concepts.RemoveAll();
	m_Scripts.RemoveAll();
}

void CResponseQueueManager::ReleaseScriptReferences()
{
	FOR_EACH_VEC( m_Expressers, i )
		if ( m_Expressers[i].outer.Get() ) m_Expressers[i].expresser->ReleaseScriptReferences();
}

void AppendSpeechContext( AI_CriteriaSet &criteria, const char *context )
{
	if ( !context || !context[0] )
		return;
	const char *fullContext = context;
	char copy[256], key[128] = { 0 }, value[128] = { 0 };
	Q_strncpy( copy, context, 255 );
	context = copy;
	while ( context )
	{
		context = SplitContext( context, key, sizeof( key ), value, sizeof( value ), NULL, fullContext );
		if ( key[0] && value[0] )
			criteria.AppendCriteria( key, value, 5.0f );
	}
}

void CResponseQueueManager::AddExpresser( CAI_Expresser *expresser )
{
	Assert( expresser && expresser->GetOuter() );
	RegisteredExpresser_t entry;
	entry.outer = expresser->GetOuter();
	entry.expresser = expresser;
	FOR_EACH_VEC( m_Expressers, i )
		if ( m_Expressers[i].outer == entry.outer ) return;
	FOR_EACH_VEC( m_Expressers, i )
	{
		if ( !m_Expressers[i].outer.Get() )
		{
			m_Expressers[i] = entry;
			return;
		}
	}
	m_Expressers.AddToTail( entry );
}

void CResponseQueueManager::RemoveExpresser( CAI_Expresser *expresser )
{
	FOR_EACH_VEC( m_Expressers, i )
	{
		if ( m_Expressers[i].expresser == expresser )
		{
			m_Expressers.FastRemove( i );
			return;
		}
	}
}

CAI_Expresser *CResponseQueueManager::GetExpresser( CBaseEntity *entity ) const
{
	if ( !entity )
		return NULL;
	FOR_EACH_VEC( m_Expressers, i )
	{
		if ( m_Expressers[i].outer.Get() == entity )
			return m_Expressers[i].expresser;
	}
	return NULL;
}

CBaseEntity *CResponseQueueManager::FindSpeechTarget( const char *name ) const
{
	// S309090 -> S3054F0: script and display aliases share the native name parser.
	CTerrorPlayer *player = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( GetTerrorSurvivorCharacterForName( name ) ) );
	return player ? player : gEntList.FindEntityByName( NULL, name );
}

ResponseTarget_t CResponseQueueManager::ResolveTarget( const char *name, CBaseEntity *speaker, const AI_CriteriaSet &criteria ) const
{
	if ( !name || !name[0] )
		return ResponseTarget_t();
	if ( !Q_stricmp( name, "self" ) )
		return ResponseTarget_t( ResponseTarget_t::ENTITY, speaker );
	if ( !Q_stricmp( name, "subject" ) || !Q_stricmp( name, "from" ) )
	{
		int index = criteria.FindCriterionIndex( name );
		return ResponseTarget_t( ResponseTarget_t::ENTITY, index < 0 ? NULL : FindSpeechTarget( criteria.GetValue( index ) ) );
	}
	if ( !Q_stricmp( name, "any" ) )
		return ResponseTarget_t( ResponseTarget_t::ANY, speaker );
	if ( !Q_stricmp( name, "all" ) )
		return ResponseTarget_t( ResponseTarget_t::ALL );
	const int character = GetTerrorSurvivorCharacterForName( name );
	if ( character != SurvivorCharacter_Unknown )
		return ResponseTarget_t( ResponseTarget_t::ENTITY, FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) ) );
	CBaseEntity *target = gEntList.FindEntityByName( NULL, name );
	if ( target )
		return ResponseTarget_t( ResponseTarget_t::ENTITY, target );
	Warning( "Couldn't resolve response target %s\n", name );
	return ResponseTarget_t();
}

void CResponseQueueManager::Queue( AIConcept_t concept, const AI_CriteriaSet &criteria, float when, const ResponseTarget_t &target, CBaseEntity *issuer )
{
	intp index = m_Concepts.AddToTail();
	QueuedConcept_t &query = m_Concepts[index];
	query.concept = AllocPooledString( concept );
	query.criteria.MergeFrom( criteria );
	query.when = when;
	query.target = target;
	query.issuer = issuer;
}

void QueueResponseConceptForEntity( AIConcept_t concept, const AI_CriteriaSet &criteria, float when, CBaseEntity *target, CBaseEntity *issuer )
{
	ResponseQueueManager().Queue( concept, criteria, when, ResponseTarget_t( ResponseTarget_t::ENTITY, target ), issuer );
}

void QueueResponseConcept( AIConcept_t concept, const char *context, float delay, const ResponseTarget_t &target, CBaseEntity *issuer )
{
	AI_CriteriaSet criteria;
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( issuer );
	if ( player )
	{
		const char *name = GetSpeechResponseEntityName( player );
		criteria.AppendCriteria( "Subject", name );
		criteria.AppendCriteria( "From", name );
	}
	else if ( issuer )
	{
		criteria.AppendCriteria( "From", STRING( issuer->GetEntityName() ) );
	}
	AppendSpeechContext( criteria, context );
	ResponseQueueManager().Queue( concept, criteria, gpGlobals->curtime + delay, target, issuer );
}

#ifdef _MSC_VER
#pragma float_control( precise, on, push )
#endif

void CResponseQueueManager::BuildCriteria( AI_CriteriaSet &criteria, const QueuedConcept_t &query, CAI_Expresser *expresser ) const
{
	criteria.AppendCriteria( "concept", STRING( query.concept ), 5.0f );
	expresser->GatherSpeechCriteria( criteria );
	criteria.MergeFrom( query.criteria );
}

void CResponseQueueManager::DispatchToAny( const QueuedConcept_t &query, float maxDistanceSqr )
{
	// S028FB0 considers at most eight equal-quality replies, then randomly
	// chooses one. Matching happens before dispatch; this is not first-to-speak.
	AI_Response responses[8];
	CAI_Expresser *candidates[8];
	int count = 0;
	float bestScore = 0.0f;
	const float slop = rr_thenany_score_slop.GetFloat();
	CBaseEntity *issuer = query.issuer.Get();
	const Vector issuerOrigin = issuer ? issuer->GetAbsOrigin() : vec3_origin;
	// S028FB0 keeps the starting count, but reads each live registry entry.
	// The selected expresser itself is retained as a raw pointer across queries.
	const int registeredCount = m_Expressers.Count();
	for ( int i = 0; i < registeredCount; ++i )
	{
		CBaseEntity *target = m_Expressers[i].outer.Get();
		if ( target == issuer || !target->IsAlive() )
			continue;
		float distanceSqr = 0.0f;
		if ( issuer )
		{
			const Vector &origin = target->GetAbsOrigin();
			const float x = origin.x - issuerOrigin.x;
			const float y = origin.y - issuerOrigin.y;
			const float z = origin.z - issuerOrigin.z;
			distanceSqr = ( y * y + x * x ) + z * z;
			if ( distanceSqr > maxDistanceSqr )
				continue;
		}
		CAI_Expresser *expresser = m_Expressers[i].expresser;
		AI_CriteriaSet criteria;
		BuildCriteria( criteria, query, expresser );
		expresser->GatherDerivedSpeechCriteria( criteria );
		if ( issuer )
			criteria.AppendCriteria( "dist_from_issuer", CFmtStr( "%f", sqrt( static_cast< double >( distanceSqr ) ) ) );
		AI_Response response;
		if ( !expresser->FindResponseWithCriteria( response, STRING( query.concept ), criteria ) || !( response.GetMatchScore() > 0.0f ) )
			continue;
		float score = response.GetMatchScore();
		if ( score > bestScore + slop )
		{
			count = 0;
			bestScore = score;
		}
		else if ( !( score >= bestScore - slop ) )
			continue;
		if ( count < ARRAYSIZE( candidates ) )
		{
			candidates[count] = expresser;
			responses[count] = response;
			++count;
			if ( score > bestScore )
				bestScore = score;
		}
	}
	if ( count )
	{
		int chosen = count == 1 ? 0 : RandomInt( 0, count - 1 );
		CAI_Expresser *expresser = candidates[chosen];
		if ( expresser )
			expresser->SpeakDispatchResponse( STRING( query.concept ), responses[chosen], NULL, &query.criteria );
	}
}

void CResponseQueueManager::Dispatch( const QueuedConcept_t &query )
{
	float range = rr_followup_maxdist.GetFloat();
	float rangeSqr = range * range;
	GetFlexExpresserThenAnyRangeSqr( query.issuer.Get(), &rangeSqr );
	if ( query.target.type == ResponseTarget_t::ANY )
	{
		DispatchToAny( query, rangeSqr );
		return;
	}
	if ( query.target.type == ResponseTarget_t::ENTITY )
	{
		CAI_Expresser *expresser = GetExpresser( query.target.entity.Get() );
		if ( expresser )
		{
			AI_CriteriaSet criteria;
			BuildCriteria( criteria, query, expresser );
			expresser->SpeakWithCriteria( STRING( query.concept ), criteria );
		}
		return;
	}
	if ( query.target.type != ResponseTarget_t::ALL )
		return;

	CBaseEntity *issuer = query.issuer.Get();
	const Vector issuerOrigin = issuer ? issuer->GetAbsOrigin() : vec3_origin;
	// S029EC0 does not snapshot the handles: callbacks can change later entries,
	// while entries appended beyond the starting count wait for a later pass.
	const int registeredCount = m_Expressers.Count();
	for ( int i = 0; i < registeredCount; ++i )
	{
		CBaseEntity *target = m_Expressers[i].outer.Get();
		float distanceSqr = 0.0f;
		if ( issuer )
		{
			const Vector &origin = target->GetAbsOrigin();
			const float x = origin.x - issuerOrigin.x;
			const float y = origin.y - issuerOrigin.y;
			const float z = origin.z - issuerOrigin.z;
			distanceSqr = ( x * x + y * y ) + z * z;
			if ( distanceSqr > rangeSqr )
				continue;
		}
		CAI_Expresser *expresser = GetExpresser( target );
		if ( expresser && target != issuer )
		{
			AI_CriteriaSet criteria;
			BuildCriteria( criteria, query, expresser );
			if ( issuer )
				criteria.AppendCriteria( "dist_from_issuer", CFmtStr( "%f", sqrt( static_cast< double >( distanceSqr ) ) ) );
			expresser->GatherDerivedSpeechCriteria( criteria );
			AI_Response response;
			if ( expresser->FindResponseWithCriteria( response, STRING( query.concept ), criteria ) )
				expresser->SpeakDispatchResponse( STRING( query.concept ), response, NULL, &query.criteria );
		}
	}
}

void CResponseQueueManager::QueueScript( const AI_ResponseScriptFollowup &script, const AI_CriteriaSet &criteria, float when, CBaseEntity *speaker )
{
	QueuedScript_t &query = m_Scripts[m_Scripts.AddToTail()];
	query.script = script;
	if ( script.function )
		query.criteria.MergeFrom( criteria );
	query.when = when;
	query.speaker = speaker;
}

void QueueSpeechScriptFollowup( const AI_ResponseScriptFollowup &script, float delay, CBaseEntity *speaker, const AI_CriteriaSet &criteria )
{
	// S00F2F0: Subject/From defaults precede the supplied criteria merge.
	AI_CriteriaSet query;
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( speaker );
	const char *name = GetSpeechResponseEntityName( speaker );
	if ( player )
	{
		query.AppendCriteria( "Subject", name );
	}
	query.AppendCriteria( "From", name );
	query.MergeFrom( criteria );
	ResponseQueueManager().QueueScript( script, query, gpGlobals->curtime + delay, speaker );
}

void CResponseQueueManager::FrameUpdatePostEntityThink()
{
	// S029EC0: world-time >= deadline, in enqueue order; every due query is
	// consumed even if its handle expired or no response matched. No retry.
	for ( intp i = m_Concepts.Head(); i != m_Concepts.InvalidIndex(); )
	{
		intp next = m_Concepts.Next( i );
		if ( gpGlobals->curtime >= m_Concepts[i].when )
		{
			Dispatch( m_Concepts[i] );
			m_Concepts.Remove( i );
		}
		i = next;
	}
	// S02A550 runs script followups AFTER concept followups, in their own
	// native list. An expired speaker is passed as a NULL script instance.
	for ( intp i = m_Scripts.Head(); i != m_Scripts.InvalidIndex(); )
	{
		intp next = m_Scripts.Next( i );
		QueuedScript_t &query = m_Scripts[i];
		if ( gpGlobals->curtime >= query.when )
		{
			// S02A3F0 keeps the converted table alive until AFTER the queued
			// function/scope and criteria have been released by node removal.
			HSCRIPT table = SpeechCriteriaToScriptTable( query.criteria );
			if ( query.script.function )
			{
				CBaseEntity *speaker = query.speaker.Get();
				g_pScriptVM->Call( query.script.function, query.script.scope, true, NULL,
					speaker ? speaker->GetScriptInstance() : static_cast< HSCRIPT >( NULL ), table );
			}
			m_Scripts.Remove( i );
			if ( table )
				g_pScriptVM->ReleaseScript( table );
		}
		i = next;
	}
}

CON_COMMAND_F( rr_forceconcept,
	"fire a response concept directly at a given character.\nUSAGE: rr_forceconcept <target> <concept> \"criteria1:value1,criteria2:value2,...\"\ncriteria values are optional.\ntarget may also be 'any', 'all', etc.", 0x00004004 )
{
	if ( args.ArgC() < 3 )
	{
		Msg( "USAGE: rr_forceconcept <target> <concept> \"criteria1:value1,criteria2:value2,...\"\n" );
		return;
	}
	AI_CriteriaSet criteria;
	AppendSpeechContext( criteria, args.ArgC() > 3 ? args[3] : "" );
	ResponseTarget_t target = ResponseQueueManager().ResolveTarget( args[1], NULL, criteria );
	ResponseQueueManager().Queue( args[2], criteria, 0.0f, target, NULL );
}

#ifdef _MSC_VER
#pragma float_control( pop )
#endif
