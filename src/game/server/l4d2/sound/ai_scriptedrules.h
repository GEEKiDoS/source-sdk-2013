//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef AI_SCRIPTEDRULES_H
#define AI_SCRIPTEDRULES_H
#ifdef _WIN32
#pragma once
#endif
#include "AI_Criteria.h"
#include "tier1/utlvector.h"
#include "tier1/utlsymbol.h"
#include "vscript/ivscript.h"
class IResponseSystem;
class CScriptRule;
IResponseSystem *CreateScriptRuleResponseSystem();
void DestroyScriptRuleResponseSystem( IResponseSystem *system );
CUtlSymbol ScriptRuleSymbol( IResponseSystem *system, const char *text );
const char *ScriptRuleSymbolText( IResponseSystem *system, CUtlSymbol symbol );
bool StoreScriptRule( IResponseSystem *system, CScriptRule *rule );
void RemoveScriptRules( IResponseSystem *system );
class CScriptRuleQuery
{
public:
	struct Fact_t { CUtlSymbol key, text; int integer; float number; };
	explicit CScriptRuleQuery( IResponseSystem *system );
	bool ReadTable( HSCRIPT table );
	void ReadCriteria( const AI_CriteriaSet &criteria );
	HSCRIPT CreateTable() const;
	const Fact_t *Find( CUtlSymbol key ) const;
	void AddNumber( const char *key, float value );
	void AddInteger( const char *key, int value );
	void AddString( const char *key, const char *value );
	IResponseSystem *GetSystem() const { return m_System; }
private:
	void Sort();
	static int CompareFacts( const Fact_t *left, const Fact_t *right );
	IResponseSystem *m_System;
	CUtlVector<Fact_t> m_Facts;
};
class CRuleScriptBridge
{
public:
	struct CScriptResponse_t
	{
		CScriptResponse_t( HSCRIPT rule, HSCRIPT responses );
		virtual ~CScriptResponse_t();
		HSCRIPT GetResponses() const { return m_Responses; }
	protected:
		HSCRIPT m_Rule, m_Responses;
	private:
		CScriptResponse_t( const CScriptResponse_t & );
		CScriptResponse_t &operator=( const CScriptResponse_t & );
	};
	CRuleScriptBridge();
	~CRuleScriptBridge();
	bool Binding_AddRuleFromScript( HSCRIPT rule );
	HSCRIPT Binding_FindBestMatch( HSCRIPT query );
	HSCRIPT Binding_FindAllMatches( HSCRIPT query, float leeway );
	void ReleaseScriptReferences();
private:
	IResponseSystem *m_System;
	CRuleScriptBridge( const CRuleScriptBridge & );
	CRuleScriptBridge &operator=( const CRuleScriptBridge & );
};
class CSpeechScriptBridge
{
public:
	struct CSpeechResponse_t : public CRuleScriptBridge::CScriptResponse_t
	{
		CSpeechResponse_t( HSCRIPT rule, HSCRIPT responses );
		virtual ~CSpeechResponse_t();
		bool IsValid() const;
		bool FindResponse( AI_Response &response, const AI_CriteriaSet &criteria, float score ) const;
	private:
		HSCRIPT m_SelectResponse;
	};
	static bool Script_AddDecisionRule( HSCRIPT rule );
	static HSCRIPT Script_FindBestResponse( HSCRIPT entity, HSCRIPT query );
	static bool Script_CommitAIResponse( HSCRIPT entity, HSCRIPT result );
	static HSCRIPT Script_GetExpressers();
};
class CScriptRule
{
public:
	struct Criterion_t
	{
		enum Kind_t { NUMBER, STRING, FUNCTION };
		Criterion_t();
		Criterion_t( const Criterion_t &other );
		~Criterion_t();
		Criterion_t &operator=( const Criterion_t &other );
		Kind_t kind;
		CUtlSymbol key, text;
		float bottom, top, weight;
		bool required;
		HSCRIPT function;
	};
	CScriptRule();
	~CScriptRule();
	void AddRef();
	void Release();
	bool ReadTable( IResponseSystem *system, HSCRIPT table, bool verbose );
	float Score( const CScriptRuleQuery &query, bool *accepted = NULL ) const;
	CSpeechScriptBridge::CSpeechResponse_t *GetResponse() const { return m_Response; }
private:
	bool ReadCriterion( IResponseSystem *system, HSCRIPT table );
	bool Matches( const Criterion_t &criterion, const CScriptRuleQuery &query ) const;
	static int CompareCriteria( const Criterion_t *left, const Criterion_t *right );
	CUtlVector<Criterion_t> m_Criteria;
	CSpeechScriptBridge::CSpeechResponse_t *m_Response;
	int m_References;
};
struct ScriptRuleMatch_t { CScriptRule *rule; float score; };
CScriptRule *FindBestScriptRule( IResponseSystem *system, const CScriptRuleQuery &query, float &score );
void FindAllScriptRules( IResponseSystem *system, const CScriptRuleQuery &query, CUtlVector<ScriptRuleMatch_t> &matches );
bool FindScriptedSpeechResponse( IResponseSystem *legacySystem, const AI_CriteriaSet &criteria, AI_Response &response );
void RegisterSpeechScriptServiceClasses();
void InitSpeechScriptBridge();
void ShutdownSpeechScriptBridge();
// The actual mounted-addon loader is shared with VScript's native addon path.
void VScriptRunScriptAddons( const char *name, HSCRIPT scope, bool warnMissing );
#endif
