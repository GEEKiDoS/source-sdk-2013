//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef AI_RESPONSESCRIPT_H
#define AI_RESPONSESCRIPT_H
#ifdef _WIN32
#pragma once
#endif

#include "vscript/ivscript.h"

class AI_Response;
class AI_CriteriaSet;
class CBaseEntity;

// Native AI_Response+196: function, its followup table/scope, and delay.
// These are VM references, never raw pointers into a transient return table.
struct AI_ResponseScriptFollowup
{
	AI_ResponseScriptFollowup();
	AI_ResponseScriptFollowup( const AI_ResponseScriptFollowup &other );
	~AI_ResponseScriptFollowup();
	AI_ResponseScriptFollowup &operator=( const AI_ResponseScriptFollowup &other );
	void Set( HSCRIPT function, HSCRIPT scope, float delay );
	void Clear();
	HSCRIPT function;
	HSCRIPT scope;
	float delay;
};

// Native AI_Response+132 and +196. A default response can exist before the
// VM: unlike CScriptAutoRef, destruction asserts no VM for no empty handles.
class AI_ResponseScriptData
{
public:
	AI_ResponseScriptData();
	AI_ResponseScriptData( const AI_ResponseScriptData &other );
	~AI_ResponseScriptData();
	AI_ResponseScriptData &operator=( const AI_ResponseScriptData &other );
	void SetFunction( HSCRIPT function );
	HSCRIPT GetFunction() const { return m_Function; }
	AI_ResponseScriptFollowup &GetFollowup() { return m_Followup; }
	const AI_ResponseScriptFollowup &GetFollowup() const { return m_Followup; }
	void Clear();
private:
	HSCRIPT m_Function;
	AI_ResponseScriptFollowup m_Followup;
};

HSCRIPT SpeechCriteriaToScriptTable( const AI_CriteriaSet &criteria );
bool CallSpeechScript( HSCRIPT function, HSCRIPT scope, CBaseEntity *speaker, const AI_CriteriaSet &criteria );
bool ReadSpeechScriptResponse( AI_Response &response, HSCRIPT table, const AI_CriteriaSet &criteria, float score );
bool FindSpeechScriptResponse( AI_Response &response, HSCRIPT function, HSCRIPT scope, const AI_CriteriaSet &criteria, float score );

#endif // AI_RESPONSESCRIPT_H
