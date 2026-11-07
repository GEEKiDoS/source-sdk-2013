//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef AI_SPEECHQUEUE_H
#define AI_SPEECHQUEUE_H
#ifdef _WIN32
#pragma once
#endif

#include "ai_speech.h"
#include "l4d2/sound/ai_responsescript.h"
#include "igamesystem.h"
#include "tier1/utllinkedlist.h"

// Native recipient tags from S00F430. DEFAULT is deliberately not SELF.
struct ResponseTarget_t
{
	enum Type { ANY = 0, ALL, ENTITY, DEFAULT };
	ResponseTarget_t( Type value = DEFAULT, CBaseEntity *entity = NULL ) : type( value ), entity( entity ) {}
	Type type;
	EHANDLE entity;
};

class CResponseQueueManager : public CAutoGameSystemPerFrame
{
public:
	CResponseQueueManager();
	virtual void LevelShutdownPreEntity() OVERRIDE;
	virtual void Shutdown() OVERRIDE;
	virtual void FrameUpdatePostEntityThink() OVERRIDE;

	void AddExpresser( CAI_Expresser *expresser );
	void RemoveExpresser( CAI_Expresser *expresser );
	CAI_Expresser *GetExpresser( CBaseEntity *entity ) const;
	CBaseEntity *FindSpeechTarget( const char *name ) const;
	HSCRIPT GetScriptResponseTargets() const;
	ResponseTarget_t ResolveTarget( const char *name, CBaseEntity *speaker, const AI_CriteriaSet &criteria ) const;
	void Queue( AIConcept_t concept, const AI_CriteriaSet &criteria, float when, const ResponseTarget_t &target, CBaseEntity *issuer );
	void QueueScript( const AI_ResponseScriptFollowup &script, const AI_CriteriaSet &criteria, float when, CBaseEntity *speaker );
	void Clear();
	void ReleaseScriptReferences();

private:
	struct QueuedConcept_t
	{
		QueuedConcept_t() : concept( NULL_STRING ), when( 0.0f ) {}
		string_t concept;
		AI_CriteriaSet criteria;
		float when;
		ResponseTarget_t target;
		EHANDLE issuer;
	};
	struct QueuedScript_t
	{
		QueuedScript_t() : when( 0.0f ) {}
		AI_ResponseScriptFollowup script;
		AI_CriteriaSet criteria;
		float when;
		EHANDLE speaker;
	};
	void Dispatch( const QueuedConcept_t &query );
	void DispatchToAny( const QueuedConcept_t &query, float maxDistanceSqr );
	void BuildCriteria( AI_CriteriaSet &criteria, const QueuedConcept_t &query, CAI_Expresser *expresser ) const;
	struct RegisteredExpresser_t
	{
		EHANDLE outer;
		CAI_Expresser *expresser;
	};

	// CUtlFixedMemory keeps the current node alive if a response enqueues its
	// own followup during dispatch. No raw entity pointer crosses the deadline.
	CUtlFixedLinkedList< QueuedConcept_t > m_Concepts;
	CUtlFixedLinkedList< QueuedScript_t > m_Scripts;
	CUtlVector< RegisteredExpresser_t > m_Expressers;
};

CResponseQueueManager &ResponseQueueManager();
const char *GetSpeechResponseEntityName( CBaseEntity *entity );
void AppendSpeechContext( AI_CriteriaSet &criteria, const char *context );
void AppendTeamSpeechCounts( AI_CriteriaSet &criteria );
void QueueResponseConcept( AIConcept_t concept, const char *context, float delay, const ResponseTarget_t &target, CBaseEntity *issuer );
void QueueResponseConceptForEntity( AIConcept_t concept, const AI_CriteriaSet &criteria, float absoluteWhen, CBaseEntity *target, CBaseEntity *issuer );
void QueueSpeechScriptFollowup( const AI_ResponseScriptFollowup &script, float delay, CBaseEntity *speaker, const AI_CriteriaSet &criteria );

#endif // AI_SPEECHQUEUE_H
