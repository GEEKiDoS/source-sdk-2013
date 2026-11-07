//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//
#include "cbase.h"
#include "AI_Criteria.h"
#include "ai_speech.h"
#include <KeyValues.h>
#include "engine/IEngineSound.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
AI_CriteriaSet::AI_CriteriaSet() : m_Lookup( 0, 0, CritEntry_t::LessFunc )
{
#ifdef L4D2
	m_nWorldContextCount = 0;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : src - 
//-----------------------------------------------------------------------------
AI_CriteriaSet::AI_CriteriaSet( const AI_CriteriaSet& src ) : m_Lookup( 0, 0, CritEntry_t::LessFunc )
{
	// Use fast Copy CUtlRBTree CopyFrom. WARNING: It only handles POD.
	m_Lookup.CopyFrom( src.m_Lookup );
#ifdef L4D2
	m_nWorldContextCount = src.m_nWorldContextCount;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
AI_CriteriaSet::~AI_CriteriaSet()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *criteria - 
//			"" - 
//			1.0f - 
//-----------------------------------------------------------------------------
void AI_CriteriaSet::AppendCriteria( const char *criteria, const char *value /*= ""*/, float weight /*= 1.0f*/ )
{
	// Note: value pointer may come from an entry inside m_Lookup!
	// that value string must be copied out before any modification
	// to the m_Lookup struct which could make the pointer invalid
	int idx = FindCriterionIndex( criteria );
	if ( idx == -1 )
	{
		CritEntry_t entry;
		entry.criterianame = criteria;
		MEM_ALLOC_CREDIT();
		entry.SetValue(value);
		entry.weight = weight;
#ifdef L4D2
		if ( criteria[0] == '$' )
			++m_nWorldContextCount;
#endif
		m_Lookup.Insert( entry );
	}
	else
	{
		CritEntry_t *entry = &m_Lookup[ idx ];
		entry->SetValue( value );
		entry->weight = weight;
	}
}


//-----------------------------------------------------------------------------
// Removes criteria in a set
//-----------------------------------------------------------------------------
void AI_CriteriaSet::RemoveCriteria( const char *criteria )
{
	int idx = FindCriterionIndex( criteria );
	if ( idx == -1 )
		return;

#ifdef L4D2
	if ( m_Lookup[idx].criterianame.String()[0] == '$' )
		--m_nWorldContextCount;
#endif
	m_Lookup.RemoveAt( idx );
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : int
//-----------------------------------------------------------------------------
int AI_CriteriaSet::GetCount() const
{
	return m_Lookup.Count();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *name - 
// Output : int
//-----------------------------------------------------------------------------
int AI_CriteriaSet::FindCriterionIndex( const char *name ) const
{
	CritEntry_t search;
	search.criterianame = name;
	int idx = m_Lookup.Find( search );
	if ( idx == m_Lookup.InvalidIndex() )
		return -1;

	return idx;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
// Output : char const
//-----------------------------------------------------------------------------
const char *AI_CriteriaSet::GetName( int index ) const
{
	static char namebuf[ 128 ];
#ifdef L4D2
	if ( index < 0 || index >= m_Lookup.MaxElement() || !m_Lookup.IsValidIndex( index ) )
#else
	if ( index < 0 || index >= (int)m_Lookup.Count() )
#endif
		return "";

	const CritEntry_t *entry = &m_Lookup[ index ];
	Q_strncpy( namebuf, entry->criterianame.String(), sizeof( namebuf ) );
	return namebuf;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
// Output : char const
//-----------------------------------------------------------------------------
const char *AI_CriteriaSet::GetValue( int index ) const
{
#ifdef L4D2
	if ( index < 0 || index >= m_Lookup.MaxElement() || !m_Lookup.IsValidIndex( index ) )
#else
	if ( index < 0 || index >= (int)m_Lookup.Count() )
#endif
		return "";

	const CritEntry_t *entry = &m_Lookup[ index ];
	return entry->value;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : index - 
// Output : float
//-----------------------------------------------------------------------------
float AI_CriteriaSet::GetWeight( int index ) const
{
#ifdef L4D2
	if ( index < 0 || index >= m_Lookup.MaxElement() || !m_Lookup.IsValidIndex( index ) )
#else
	if ( index < 0 || index >= (int)m_Lookup.Count() )
#endif
		return 1.0f;

	const CritEntry_t *entry = &m_Lookup[ index ];
	return entry->weight;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void AI_CriteriaSet::Describe()
{
	for ( short i = m_Lookup.FirstInorder(); i != m_Lookup.InvalidIndex(); i = m_Lookup.NextInorder( i ) )
	{
		CritEntry_t *entry = &m_Lookup[ i ];

		if ( entry->weight != 1.0f )
		{
			DevMsg( "  %20s = '%s' (weight %f)\n", entry->criterianame.String(), entry->value, entry->weight );
		}
		else
		{
			DevMsg( "  %20s = '%s'\n", entry->criterianame.String(), entry->value );
		}
	}
}

BEGIN_SIMPLE_DATADESC( AI_ResponseParams )
	DEFINE_FIELD( flags,	FIELD_SHORT ),
	DEFINE_FIELD( odds,	FIELD_SHORT ),	
	DEFINE_FIELD( soundlevel,	FIELD_CHARACTER ),	
	DEFINE_FIELD( delay,	FIELD_INTEGER ),		// These are compressed down to two float16s, so treat as an INT for saverestore
	DEFINE_FIELD( respeakdelay,	FIELD_INTEGER ),	//  
END_DATADESC()

BEGIN_SIMPLE_DATADESC( AI_Response )
	DEFINE_FIELD( m_Type,	FIELD_CHARACTER ),
	DEFINE_ARRAY( m_szResponseName, FIELD_CHARACTER, AI_Response::MAX_RESPONSE_NAME ),	
	DEFINE_ARRAY( m_szMatchingRule, FIELD_CHARACTER, AI_Response::MAX_RULE_NAME ),	
	// DEFINE_FIELD( m_pCriteria, FIELD_??? ), // Don't need to save this probably
	DEFINE_EMBEDDED( m_Params ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
AI_Response::AI_Response()
{
	m_Type = RESPONSE_NONE;
	m_szResponseName[0] = 0;
	m_szMatchingRule[0] = 0;

	m_pCriteria = NULL;
	m_bApplyContextToWorld = false;
#ifdef L4D2
	m_flMatchScore = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
AI_Response::AI_Response( const AI_Response &from )
{
	m_pCriteria = NULL;
	*this = from;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
AI_Response::~AI_Response()
{
	delete m_pCriteria;
	m_pCriteria = NULL;
}

//-----------------------------------------------------------------------------
AI_Response &AI_Response::operator=( const AI_Response &from )
{
	Assert( (void*)(&m_Type) == (void*)this );

	if (this == &from)
		return *this;

	m_Type = from.m_Type;

	V_strcpy_safe( m_szResponseName, from.m_szResponseName );
	V_strcpy_safe( m_szMatchingRule, from.m_szMatchingRule );

	delete m_pCriteria;
	m_pCriteria = NULL;

	// Copy criteria.
	if (from.m_pCriteria)
		m_pCriteria = new AI_CriteriaSet(*from.m_pCriteria);

	m_Params = from.m_Params;

	m_szContext = from.m_szContext;
	m_bApplyContextToWorld = from.m_bApplyContextToWorld;
#ifdef L4D2
	m_Followup = from.m_Followup;
	m_ScriptData = from.m_ScriptData;
	m_flMatchScore = from.m_flMatchScore;
#endif

	return *this;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *response - 
//			*criteria - 
//-----------------------------------------------------------------------------
void AI_Response::Init( ResponseType_t type, const char *responseName, const AI_CriteriaSet& criteria,
					const AI_ResponseParams& responseparams, const char *ruleName, const char *applyContext,
					bool bApplyContextToWorld )
{
	m_Type = type;

	V_strcpy_safe( m_szResponseName, responseName );
	V_strcpy_safe( m_szMatchingRule, ruleName ? ruleName : "NULL" );

	// Copy underlying criteria
#ifdef L4D2
	delete m_pCriteria;
	m_pCriteria = NULL;
#else
	Assert( !m_pCriteria );
#endif
	m_pCriteria = new AI_CriteriaSet( criteria );

	m_Params = responseparams;
#ifdef L4D2
	// Native0A840 retains the MAIN function and score across Init.
	m_Followup.Clear();
	m_ScriptData.GetFollowup().Clear();
#endif

	m_szContext = applyContext;
	m_bApplyContextToWorld = bApplyContextToWorld;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void AI_Response::Describe()
{
	if ( m_pCriteria )
	{
		DevMsg( "Search criteria:\n" );
		m_pCriteria->Describe();
	}
	if ( m_szMatchingRule[ 0 ] )
		DevMsg( "Matched rule '%s', ", m_szMatchingRule );
	if ( m_szContext.Length() )
		DevMsg( "Contexts to set '%s' on %s, ", m_szContext.Get(), m_bApplyContextToWorld ? "world" : "speaker" );

	DevMsg( "response %s = '%s'\n", DescribeResponse( (ResponseType_t)m_Type ), m_szResponseName );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char * AI_Response::GetNamePtr() const
{
    return m_szResponseName;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char * AI_Response::GetResponsePtr() const
{
    return m_szResponseName;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : type - 
// Output : char const
//-----------------------------------------------------------------------------
const char *AI_Response::DescribeResponse( ResponseType_t type )
{
	if ( (int)type < 0 || (int)type >= NUM_RESPONSES )
	{
		Assert( 0 );
		return "???AI_Response bogus index";
	}

	switch( type )
	{
	case RESPONSE_NONE:     return "RESPONSE_NONE";
	case RESPONSE_SPEAK:    return "RESPONSE_SPEAK";
	case RESPONSE_SENTENCE: return "RESPONSE_SENTENCE";
	case RESPONSE_SCENE:    return "RESPONSE_SCENE";
	case RESPONSE_RESPONSE: return "RESPONSE_RESPONSE";
	case RESPONSE_PRINT:    return "RESPONSE_PRINT";
#ifdef L4D2
	case RESPONSE_SCRIPT:   return "RESPONSE_SCRIPT";
#endif
	}

	Assert( 0 );
	return "RESPONSE_NONE";
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : const AI_CriteriaSet
//-----------------------------------------------------------------------------
const AI_CriteriaSet *AI_Response::GetCriteria()
{
	return m_pCriteria;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void AI_Response::Release()
{
	delete this;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : soundlevel_t
//-----------------------------------------------------------------------------
soundlevel_t AI_Response::GetSoundLevel() const
{
	if ( m_Params.flags & AI_ResponseParams::RG_SOUNDLEVEL )
	{
		return (soundlevel_t)m_Params.soundlevel;
	}

	return SNDLVL_TALKING;
}

float AI_Response::GetRespeakDelay( void ) const
{
	if ( m_Params.flags & AI_ResponseParams::RG_RESPEAKDELAY )
	{
		interval_t temp;
		m_Params.respeakdelay.ToInterval( temp );
		return RandomInterval( temp );
	}

	return 0.0f;
}

float AI_Response::GetWeaponDelay( void ) const
{
	if ( m_Params.flags & AI_ResponseParams::RG_WEAPONDELAY )
	{
		interval_t temp;
		m_Params.weapondelay.ToInterval( temp );
		return RandomInterval( temp );
	}

	return 0.0f;
}

bool AI_Response::GetSpeakOnce( void ) const
{
	if ( m_Params.flags & AI_ResponseParams::RG_SPEAKONCE )
	{
		return true;
	}

	return false;
}

bool AI_Response::ShouldntUseScene( void ) const
{
	return ( m_Params.flags & AI_ResponseParams::RG_DONT_USE_SCENE ) != 0;
}

bool AI_Response::ShouldBreakOnNonIdle( void ) const
{
	return ( m_Params.flags & AI_ResponseParams::RG_STOP_ON_NONIDLE ) != 0;
}

int AI_Response::GetOdds( void ) const
{
	if ( m_Params.flags & AI_ResponseParams::RG_ODDS )
	{
		return m_Params.odds;
	}
	return 100;
}

float AI_Response::GetDelay() const
{
	if ( m_Params.flags & AI_ResponseParams::RG_DELAYAFTERSPEAK )
	{
		interval_t temp;
		m_Params.delay.ToInterval( temp );
		return RandomInterval( temp );
	}
	return 0.0f;
}

float AI_Response::GetPreDelay() const
{
	if ( m_Params.flags & AI_ResponseParams::RG_DELAYBEFORESPEAK )
	{
		interval_t temp;
		m_Params.predelay.ToInterval( temp );
		return RandomInterval( temp );
	}
	return 0.0f;
}

//-----------------------------------------------------------------------------
// Purpose: Sets context string
// Output : void
//-----------------------------------------------------------------------------
void AI_Response::SetContext( const char *context )
{
	m_szContext = context;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *raw - 
//			*key - 
//			keylen - 
//			*value - 
//			valuelen - 
//			*duration -
// Output : static bool
//-----------------------------------------------------------------------------
#ifdef L4D2
const char *SplitContext( const char *raw, char *key, int keylen, char *value, int valuelen, float *duration, const char *fullContext )
#else
const char *SplitContext( const char *raw, char *key, int keylen, char *value, int valuelen, float *duration )
#endif
{
	char *colon1 = Q_strstr( raw, ":" );
	if ( !colon1 )
	{
		DevMsg( "SplitContext:  warning, ignoring context '%s', missing colon separator!\n", raw );
		*key = *value = 0;
		return NULL;
	}

	int len = colon1 - raw;
	Q_strncpy( key, raw, MIN( len + 1, keylen ) );
	key[ MIN( len, keylen - 1 ) ] = 0;

	bool last = false;
	char *end = Q_strstr( colon1 + 1, "," );
	if ( !end )
	{
		int remaining = Q_strlen( colon1 + 1 );
		end = colon1 + 1 + remaining;
		last = true;
	}

	char *colon2 = Q_strstr( colon1 + 1, ":" );
	if ( colon2 && ( colon2 < end ) )
	{
		if ( duration )
			*duration = atof( colon2 + 1 );
#ifdef L4D2
		if ( (unsigned char)( colon2[1] - '0' ) > 9 )
		{
			DevMsg( "SplitContext:  warning, ignoring context '%s', missing comma separator!  Entire context was '%s'.\n", raw, fullContext );
			*key = *value = 0;
			return NULL;
		}
#endif

		len = MIN( colon2 - ( colon1 + 1 ), valuelen - 1 );
		Q_strncpy( value, colon1 + 1, len + 1 );
		value[ len ] = 0;
	}
	else
	{
		if ( duration )
			*duration = 0.0;

		len = MIN( end - ( colon1 + 1 ), valuelen - 1 );
		Q_strncpy( value, colon1 + 1, len + 1 );
		value[ len ] = 0;
	}

	return last ? NULL : end + 1;
}

#ifdef L4D2
int AI_CriteriaSet::FirstCriterion() const
{
	short index = m_Lookup.FirstInorder();
	return index == m_Lookup.InvalidIndex() ? -1 : index;
}
int AI_CriteriaSet::NextCriterion( int previous ) const
{
	short index = m_Lookup.NextInorder( previous );
	return index == m_Lookup.InvalidIndex() ? -1 : index;
}
void AI_CriteriaSet::MergeFrom( const AI_CriteriaSet &other )
{
	if ( this == &other ) return;
	for ( int index = other.FirstCriterion(); index != -1; index = other.NextCriterion( index ) )
	{
		const CritEntry_t &entry = other.m_Lookup[index];
		AppendCriteria( entry.criterianame.String(), entry.value, entry.weight );
	}
}
void AI_CriteriaSet::InterceptWorldSetContexts( AI_CriteriaSet &contexts )
{
	if ( m_nWorldContextCount == 0 ) return;
	AI_CriteriaSet replacement;
	for ( int index = FirstCriterion(); index != -1; index = NextCriterion( index ) )
	{
		const CritEntry_t &entry = m_Lookup[index];
		const char *name = entry.criterianame.String();
		if ( name[0] == '$' )
		{
			char mapped[80] = "world";
			Q_strncpy( mapped + 5, name + 1, 75 );
			replacement.AppendCriteria( mapped, entry.value, entry.weight );
			contexts.AppendCriteria( name + 1, entry.value, entry.weight );
		}
		else replacement.AppendCriteria( name, entry.value, entry.weight );
	}
	m_nWorldContextCount = 0;
	m_Lookup.Swap( replacement.m_Lookup );
}
#endif
