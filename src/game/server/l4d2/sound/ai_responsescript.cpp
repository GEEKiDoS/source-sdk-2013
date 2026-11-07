//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/ai_responsescript.h"
#include "AI_Criteria.h"
#include "vscript_shared.h"
#include <emmintrin.h>
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

AI_ResponseScriptFollowup::AI_ResponseScriptFollowup() : function( NULL ), scope( NULL ), delay( 0.0f ) {}
AI_ResponseScriptFollowup::AI_ResponseScriptFollowup( const AI_ResponseScriptFollowup &other ) : function( NULL ), scope( NULL ), delay( 0.0f )
{
	Set( other.function, other.scope, other.delay );
}
AI_ResponseScriptFollowup::~AI_ResponseScriptFollowup() { Clear(); }
AI_ResponseScriptFollowup &AI_ResponseScriptFollowup::operator=( const AI_ResponseScriptFollowup &other )
{
	if ( this != &other )
		Set( other.function, other.scope, other.delay );
	return *this;
}
void AI_ResponseScriptFollowup::Set( HSCRIPT newFunction, HSCRIPT newScope, float newDelay )
{
	HSCRIPT functionRef = newFunction ? g_pScriptVM->ReferenceScope( newFunction ) : NULL;
	HSCRIPT scopeRef = newScope ? g_pScriptVM->ReferenceScope( newScope ) : NULL;
	HSCRIPT oldFunction = function, oldScope = scope;
	function = functionRef; scope = scopeRef; delay = newDelay;
	// ReferenceScope retained generic objects; it did not create named scopes.
	if ( oldFunction ) g_pScriptVM->ReleaseScript( oldFunction );
	if ( oldScope ) g_pScriptVM->ReleaseScript( oldScope );
}
void AI_ResponseScriptFollowup::Clear()
{
	HSCRIPT oldFunction = function, oldScope = scope;
	function = scope = NULL;
	delay = 0.0f;
	if ( oldFunction ) g_pScriptVM->ReleaseScript( oldFunction );
	if ( oldScope ) g_pScriptVM->ReleaseScript( oldScope );
}

AI_ResponseScriptData::AI_ResponseScriptData() : m_Function( NULL ) {}
AI_ResponseScriptData::AI_ResponseScriptData( const AI_ResponseScriptData &other ) :
	m_Function( other.m_Function ? g_pScriptVM->ReferenceScope( other.m_Function ) : NULL ), m_Followup( other.m_Followup ) {}
AI_ResponseScriptData::~AI_ResponseScriptData()
{
	if ( m_Function )
		g_pScriptVM->ReleaseScript( m_Function );
}
AI_ResponseScriptData &AI_ResponseScriptData::operator=( const AI_ResponseScriptData &other )
{
	if ( this != &other )
	{
		SetFunction( other.m_Function );
		m_Followup = other.m_Followup;
	}
	return *this;
}
void AI_ResponseScriptData::SetFunction( HSCRIPT function )
{
	HSCRIPT reference = function ? g_pScriptVM->ReferenceScope( function ) : NULL;
	HSCRIPT oldFunction = m_Function;
	m_Function = reference;
	if ( oldFunction ) g_pScriptVM->ReleaseScript( oldFunction );
}
void AI_ResponseScriptData::Clear()
{
	HSCRIPT oldFunction = m_Function;
	HSCRIPT oldFollowupFunction = m_Followup.function, oldFollowupScope = m_Followup.scope;
	m_Function = NULL;
	m_Followup.function = m_Followup.scope = NULL;
	m_Followup.delay = 0.0f;
	if ( oldFunction ) g_pScriptVM->ReleaseScript( oldFunction );
	if ( oldFollowupFunction ) g_pScriptVM->ReleaseScript( oldFollowupFunction );
	if ( oldFollowupScope ) g_pScriptVM->ReleaseScript( oldFollowupScope );
}

// S010980 is deliberately not a conventional numeric validator. Empty,
// '.', '00..' and any string with nonzero atof are numeric; '-0' is not.
static bool IsSpeechScriptNumber( const char *text )
{
	if ( atof( text ) != 0.0 )
		return true;
	for ( ; *text; ++text )
		if ( *text != '0' && *text != '.' )
			return false;
	return true;
}

HSCRIPT SpeechCriteriaToScriptTable( const AI_CriteriaSet &criteria )
{
	// S2C2AD0: script sees values, not response-rule weights. Numeric values
	// become FLOAT, including integer-looking text, exactly as the original.
	ScriptVariant_t table;
	g_pScriptVM->CreateTable( table );
	HSCRIPT result = table;
	for ( int i = criteria.FirstCriterion(); i != -1; i = criteria.NextCriterion( i ) )
	{
		const char *value = criteria.GetValue( i );
		if ( IsSpeechScriptNumber( value ) )
			g_pScriptVM->SetValue( result, criteria.GetName( i ), ScriptVariant_t( static_cast< float >( atof( value ) ) ) );
		else
			g_pScriptVM->SetValue( result, criteria.GetName( i ), value );
	}
	// CreateTable returns the reference; the caller releases it with the VM.
	return result;
}

bool CallSpeechScript( HSCRIPT function, HSCRIPT scope, CBaseEntity *speaker, const AI_CriteriaSet &criteria )
{
	// S0275F0 / S02A3F0: (speaker script INSTANCE, criteria table), NOT the
	// entity's scope as `this`, an AddCriteria callback, or script text.
	HSCRIPT table = SpeechCriteriaToScriptTable( criteria );
	if ( function )
		g_pScriptVM->Call( function, scope, true, NULL, speaker ? speaker->GetScriptInstance() : static_cast< HSCRIPT >( NULL ), table );
	if ( table )
		g_pScriptVM->ReleaseScript( table );
	// The original marks an invoked function successful even on SCRIPT_ERROR.
	return function != NULL;
}

static bool GetScriptObject( HSCRIPT table, const char *key, CScriptAutoRef &object )
{
	ScriptVariant_t value;
	if ( g_pScriptVM->GetValue( table, key, &value ) && value.GetType() == FIELD_HSCRIPT )
	{
		// GetValue already references the object (S2C2430). Transfer that one
		// reference; ScriptVariant's destructor itself does not release HSCRIPT.
		object.SetNoAddref( static_cast< HSCRIPT >( value ) );
		return true;
	}
	g_pScriptVM->ReleaseValue( value );
	return false;
}

static bool GetScriptInteger( HSCRIPT table, const char *key, int &number )
{
	ScriptVariant_t value;
	bool found = g_pScriptVM->GetValue( table, key, &value ) && value.GetType() != FIELD_VOID;
	if ( found )
	{
		uint32 bits = value.GetRawPayload32();
		float raw; memcpy( &raw, &bits, sizeof( raw ) );
		number = value.GetType() == FIELD_INTEGER ? static_cast< int32 >( bits ) : _mm_cvtt_ss2si( _mm_set_ss( raw ) );
	}
	g_pScriptVM->ReleaseValue( value );
	return found;
}
static bool GetScriptFloat( HSCRIPT table, const char *key, float &number )
{
	ScriptVariant_t value;
	bool found = g_pScriptVM->GetValue( table, key, &value ) && value.GetType() != FIELD_VOID;
	if ( found )
	{
		uint32 bits = value.GetRawPayload32();
		if ( value.GetType() == FIELD_FLOAT ) memcpy( &number, &bits, sizeof( number ) );
		else number = static_cast< float >( static_cast< int32 >( bits ) );
	}
	g_pScriptVM->ReleaseValue( value );
	return found;
}
static bool GetScriptBoolean( HSCRIPT table, const char *key, bool &boolean )
{
	ScriptVariant_t value;
	bool found = g_pScriptVM->GetValue( table, key, &value ) && value.GetType() != FIELD_VOID;
	if ( found ) boolean = ( value.GetRawPayload32() & 255 ) != 0;
	g_pScriptVM->ReleaseValue( value );
	return found;
}
static bool GetScriptString( HSCRIPT table, const char *key, CUtlString &text )
{
	ScriptVariant_t value;
	bool found = g_pScriptVM->GetValue( table, key, &value ) && value.GetType() != FIELD_VOID;
	if ( found )
		text = static_cast< const char * >( value );
	g_pScriptVM->ReleaseValue( value );
	return found;
}

bool ReadSpeechScriptResponse( AI_Response &response, HSCRIPT table, const AI_CriteriaSet &criteria, float score )
{
	response.SetMatchScore( score );
	int kind;
	if ( !GetScriptInteger( table, "kind", kind ) ) return false;
	CScriptAutoRef function;
	if ( GetScriptObject( table, "func", function ) )
		response.GetScriptData().SetFunction( function );
	// No func field does NOT clear an old function. Unsupported/failed table
	// conversion may leave a function for the subsequent native legacy fallback.
	if ( kind != RESPONSE_SCENE && kind != RESPONSE_SCRIPT )
	{
		Warning( "Unsupported response type %d", kind );
		return false;
	}
	CUtlString target;
	if ( kind == RESPONSE_SCRIPT ) target = "(script)";
	else if ( !GetScriptString( table, "target", target ) ) return false;
	CScriptAutoRef rule;
	if ( !GetScriptObject( table, "rule", rule ) ) return false;
	CUtlString ruleName;
	if ( !GetScriptString( rule, "rulename", ruleName ) ) ruleName = "(broken)";
	CScriptAutoRef scriptParams;
	if ( !GetScriptObject( table, "params", scriptParams ) ) return false;
	AI_ResponseParams params;
	float number;
	bool boolean;
	if ( GetScriptFloat( scriptParams, "predelay", number ) )
	{
		params.flags |= AI_ResponseParams::RG_DELAYBEFORESPEAK;
		params.predelay.range = number;
	}
	if ( GetScriptFloat( scriptParams, "delay", number ) )
	{
		params.flags |= AI_ResponseParams::RG_DELAYAFTERSPEAK;
		params.delay.range = number;
	}
	if ( GetScriptBoolean( scriptParams, "speakonce", boolean ) && boolean ) params.flags |= AI_ResponseParams::RG_SPEAKONCE;
	if ( GetScriptBoolean( scriptParams, "noscene", boolean ) && boolean ) params.flags |= AI_ResponseParams::RG_DONT_USE_SCENE;
	if ( GetScriptBoolean( scriptParams, "stop_on_nonidle", boolean ) ) Warning( "stop_on_nonidle is not supported in squirrel rules!\n" );
	int integer;
	params.odds = GetScriptInteger( scriptParams, "odds", integer ) ? integer : 1;
	if ( GetScriptFloat( scriptParams, "respeakdelay", number ) ) params.respeakdelay.range = number;
	if ( GetScriptInteger( scriptParams, "soundlevel", integer ) ) params.soundlevel = integer;
	response.Init( static_cast< ResponseType_t >( kind ), target.String(), criteria, params, ruleName.String(), NULL, false );
	CScriptAutoRef followup;
	if ( GetScriptObject( scriptParams, "followup", followup ) )
	{
		CScriptAutoRef followupFunction;
		if ( GetScriptObject( followup, "func", followupFunction ) && GetScriptFloat( followup, "delay", number ) )
			response.GetScriptData().GetFollowup().Set( followupFunction, followup, number );
		else Warning( "Script response in rule %s had a 'followup' parameter, but it didn't have 'func' and 'delay' keys", ruleName.String() );
	}
	return true;
}

bool FindSpeechScriptResponse( AI_Response &response, HSCRIPT function, HSCRIPT scope, const AI_CriteriaSet &criteria, float score )
{
	ScriptVariant_t result;
	g_pScriptVM->ExecuteFunction( function, NULL, 0, &result, scope, true );
	if ( result.GetType() == FIELD_VOID )
	{
		Warning( "Script failed to return response after matching rule.\n" );
		g_pScriptVM->ReleaseValue( result );
		return false;
	}
	if ( result.GetType() != FIELD_HSCRIPT )
	{
		Warning( "Script returned a response of inappropriate type %d\n", result.GetType() );
		g_pScriptVM->ReleaseValue( result );
		return false;
	}
	bool found = ReadSpeechScriptResponse( response, result, criteria, score );
	g_pScriptVM->ReleaseValue( result );
	return found;
}

#pragma float_control( pop )
