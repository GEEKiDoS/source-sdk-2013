//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "l4d2/sound/ai_scriptedrules.h"
#include "game.h"
#include "l4d2/sound/ai_responsescript.h"
#include "l4d2/sound/ai_speechqueue.h"
#include "vscript_shared.h"
#include "vscript_container_helpers.h"
#include "filesystem.h"
#include "tier1/utlbuffer.h"
#include "tier1/utlstring.h"
#include <float.h>
#include <emmintrin.h>
#include "tier0/memdbgon.h"

#pragma float_control( precise, on, push )

// S4F6060 leaves this sentinel in the result when the numerical return is zero.
static const float SCRIPT_NO_MATCH = -2147483648.0f;
static IResponseSystem *s_SpeechRules;
static HSCRIPT s_SpeechScope;
static bool s_Initialized;
static CUtlVector<CRuleScriptBridge *> s_Deciders;

static bool ScriptNumberText( const char *text )
{
	if ( atof( text ) != 0.0 ) return true;
	for ( ; *text; ++text ) if ( *text != '0' && *text != '.' ) return false;
	return true;
}
static float RawFloat( const ScriptVariant_t &value )
{
	uint32 bits = value.GetRawPayload32();
	float result;
	memcpy( &result, &bits, sizeof( result ) );
	return result;
}
// S2C20E0, not CVariant::AssignTo (notably SHORT is cast from raw FLOAT).
static double ScriptNumber( const ScriptVariant_t &value )
{
	switch ( value.GetType() )
	{
	case FIELD_FLOAT: return RawFloat( value );
	case FIELD_INTEGER: return static_cast< int32 >( value.GetRawPayload32() );
	case FIELD_SHORT: return _mm_cvtt_ss2si( _mm_set_ss( RawFloat( value ) ) );
	case FIELD_UINT64: return static_cast< double >( static_cast< uint64 >( value ) );
	case FIELD_FLOAT64: return static_cast< float64 >( value );
	case FIELD_UINT: return value.GetRawPayload32();
	default: return 0.0;
	}
}
static int ScriptInteger( const ScriptVariant_t &value )
{
	switch ( value.GetType() )
	{
	case FIELD_INTEGER: case FIELD_UINT64: case FIELD_UINT: return static_cast< int32 >( value.GetRawPayload32() );
	case FIELD_FLOAT: case FIELD_SHORT: return _mm_cvtt_ss2si( _mm_set_ss( RawFloat( value ) ) );
	case FIELD_FLOAT64: return _mm_cvttsd_si32( _mm_set_sd( static_cast< float64 >( value ) ) );
	default: return 0;
	}
}
static bool GetObject( HSCRIPT table, const char *key, CScriptAutoRef &object )
{
	ScriptVariant_t value;
	if ( g_pScriptVM->GetValue( table, key, &value ) && value.GetType() == FIELD_HSCRIPT )
	{
		object.SetNoAddref( static_cast< HSCRIPT >( value ) );
		return true;
	}
	g_pScriptVM->ReleaseValue( value );
	return false;
}
static bool ScriptOptional( HSCRIPT table )
{
	ScriptVariant_t value;
	g_pScriptVM->GetValue( table, "optional", &value );
	bool result = false;
	if ( value.GetType() == FIELD_BOOLEAN ) result = ( value.GetRawPayload32() & 255 ) != 0;
	else if ( value.GetType() == FIELD_INTEGER ) result = value.GetRawPayload32() != 0;
	else if ( value.GetType() == FIELD_FLOAT ) result = RawFloat( value ) != 0.0f;
	g_pScriptVM->ReleaseValue( value );
	return result;
}
CScriptRuleQuery::CScriptRuleQuery( IResponseSystem *system ) : m_System( system ) {}
int CScriptRuleQuery::CompareFacts( const Fact_t *a, const Fact_t *b ) { return static_cast< int >( a->key ) - static_cast< int >( b->key ); }
void CScriptRuleQuery::Sort() { m_Facts.Sort( CompareFacts ); }
void CScriptRuleQuery::AddNumber( const char *key, float value )
{
	Fact_t fact;
	fact.key = ScriptRuleSymbol( m_System, key ); fact.text = UTL_INVAL_SYMBOL;
	fact.number = value; fact.integer = _mm_cvtt_ss2si( _mm_set_ss( value ) );
	m_Facts.AddToTail( fact );
}
void CScriptRuleQuery::AddInteger( const char *key, int value )
{
	Fact_t fact;
	fact.key = ScriptRuleSymbol( m_System, key ); fact.text = UTL_INVAL_SYMBOL;
	fact.integer = value; fact.number = static_cast< float >( value );
	m_Facts.AddToTail( fact );
}
void CScriptRuleQuery::AddString( const char *key, const char *value )
{
	Fact_t fact;
	fact.key = ScriptRuleSymbol( m_System, key ); fact.text = ScriptRuleSymbol( m_System, value );
	fact.integer = static_cast< int >( fact.text );
	uint32 bits = 0x7FBFFFFF; // S4F5580's actual string sentinel, NOT ordinary zero.
	memcpy( &fact.number, &bits, sizeof( fact.number ) );
	m_Facts.AddToTail( fact );
}
bool CScriptRuleQuery::ReadTable( HSCRIPT table )
{
	if ( !VScriptIsTable( table ) )
	{
		Warning( "Can't parse something that isn't a table into a Decider query.\n" );
		return false;
	}
	m_Facts.RemoveAll();
	int count = g_pScriptVM->GetNumTableEntries( table ), iterator = 0;
	for ( int i = 0; i < count; ++i )
	{
		ScriptVariant_t key, value;
		iterator = g_pScriptVM->GetKeyValue( table, iterator, &key, &value );
		// S2C2930 treats non-string payloads as pointers. For those undefined
		// reads, the standing minimal-skip rule takes the existing invalid-key path.
		const char *name = key.GetType() == FIELD_CSTRING ? static_cast< const char * >( key ) : NULL;
		if ( name )
		{
			switch ( value.GetType() )
			{
			case FIELD_FLOAT: case FIELD_FLOAT64: AddNumber( name, static_cast< float >( ScriptNumber( value ) ) ); break;
			case FIELD_INTEGER: case FIELD_SHORT: case FIELD_VECTOR2D: case FIELD_UINT64: case FIELD_UINT:
				AddInteger( name, ScriptInteger( value ) ); break;
			case FIELD_CSTRING:
				{
					const char *text = static_cast< const char * >( value );
					if ( g_pDeveloper->GetInt() > 0 && ScriptNumberText( text ) )
						Warning( "Query key %s has value %s, which appears to be a number -- you should pass this as a numeric type instead.", name, text );
					AddString( name, text );
				}
				break;
			default: Warning( "Table entry '%s' has inappropriate type %d, can't make a Decider query out of that.\n", name, value.GetType() ); break;
			}
		}
		else Warning( "query has invalid key of type %d\n", key.GetType() );
		g_pScriptVM->ReleaseValue( key ); g_pScriptVM->ReleaseValue( value );
	}
	Sort();
	return count > 0; // Raw table count, even if all values were skipped.
}
void CScriptRuleQuery::ReadCriteria( const AI_CriteriaSet &criteria )
{
	m_Facts.RemoveAll();
	for ( int i = criteria.FirstCriterion(); i != -1; i = criteria.NextCriterion( i ) )
	{
		const char *text = criteria.GetValue( i );
		if ( ScriptNumberText( text ) ) AddNumber( criteria.GetName( i ), static_cast< float >( atof( text ) ) );
		else AddString( criteria.GetName( i ), text );
	}
	Sort(); // Native S2C5220 deliberately gives EVERY fact weight 1, not criteria weights.
}
const CScriptRuleQuery::Fact_t *CScriptRuleQuery::Find( CUtlSymbol key ) const
{
	int lo = 0, hi = m_Facts.Count();
	while ( lo < hi ) { int mid = ( lo + hi ) / 2; if ( m_Facts[mid].key < key ) lo = mid + 1; else hi = mid; }
	return lo < m_Facts.Count() && m_Facts[lo].key == key ? &m_Facts[lo] : NULL;
}
HSCRIPT CScriptRuleQuery::CreateTable() const
{
	ScriptVariant_t table; g_pScriptVM->CreateTable( table );
	HSCRIPT result = table;
	FOR_EACH_VEC( m_Facts, i )
	{
		const Fact_t &fact = m_Facts[i]; uint32 bits;
		memcpy( &bits, &fact.number, sizeof( bits ) );
		if ( ( bits & 0x7F800000 ) == 0x7F800000 )
			g_pScriptVM->SetValue( result, ScriptRuleSymbolText( m_System, fact.key ), ScriptRuleSymbolText( m_System, CUtlSymbol( fact.integer ) ) );
		else g_pScriptVM->SetValue( result, ScriptRuleSymbolText( m_System, fact.key ), ScriptVariant_t( fact.number ) );
	}
	return result;
}
CRuleScriptBridge::CScriptResponse_t::CScriptResponse_t( HSCRIPT rule, HSCRIPT responses ) :
	m_Rule( rule ? g_pScriptVM->ReferenceScope( rule ) : NULL ),
	m_Responses( responses ? g_pScriptVM->ReferenceScope( responses ) : NULL ) {}
// Native2C2370/2C22B0 retain generic objects, not named CreateScope scopes.
// SDK ReleaseScope does not release array or function handles.
CRuleScriptBridge::CScriptResponse_t::~CScriptResponse_t()
{
	if ( m_Responses ) g_pScriptVM->ReleaseScript( m_Responses );
	if ( m_Rule ) g_pScriptVM->ReleaseScript( m_Rule );
}
CSpeechScriptBridge::CSpeechResponse_t::CSpeechResponse_t( HSCRIPT rule, HSCRIPT responses ) :
	CScriptResponse_t( rule, responses ), m_SelectResponse( NULL )
{
	CScriptAutoRef function;
	if ( GetObject( m_Rule, "SelectResponse", function ) ) m_SelectResponse = g_pScriptVM->ReferenceScope( function );
}
CSpeechScriptBridge::CSpeechResponse_t::~CSpeechResponse_t() { if ( m_SelectResponse ) g_pScriptVM->ReleaseScript( m_SelectResponse ); }
bool CSpeechScriptBridge::CSpeechResponse_t::IsValid() const { return m_Rule && m_Responses && m_SelectResponse; }
bool CSpeechScriptBridge::CSpeechResponse_t::FindResponse( AI_Response &response, const AI_CriteriaSet &criteria, float score ) const
{
	return FindSpeechScriptResponse( response, m_SelectResponse, m_Rule, criteria, score );
}
CScriptRule::Criterion_t::Criterion_t() : kind( NUMBER ), key( UTL_INVAL_SYMBOL ), text( UTL_INVAL_SYMBOL ), bottom( -FLT_MAX ), top( FLT_MAX ), weight( 1.0f ), required( true ), function( NULL ) {}
CScriptRule::Criterion_t::Criterion_t( const Criterion_t &other ) : function( NULL ) { *this = other; }
CScriptRule::Criterion_t::~Criterion_t() { if ( function ) g_pScriptVM->ReleaseScript( function ); }
CScriptRule::Criterion_t &CScriptRule::Criterion_t::operator=( const Criterion_t &other )
{
	if ( this == &other ) return *this;
	HSCRIPT reference = other.function ? g_pScriptVM->ReferenceScope( other.function ) : NULL;
	if ( function ) g_pScriptVM->ReleaseScript( function );
	kind = other.kind; key = other.key; text = other.text; bottom = other.bottom; top = other.top;
	weight = other.weight; required = other.required; function = reference;
	return *this;
}
CScriptRule::CScriptRule() : m_Response( NULL ), m_References( 1 ) {}
CScriptRule::~CScriptRule() { delete m_Response; }
void CScriptRule::AddRef() { ++m_References; }
void CScriptRule::Release() { if ( --m_References == 0 ) delete this; }
int CScriptRule::CompareCriteria( const Criterion_t *a, const Criterion_t *b )
{
	int ag = ( a->required ? 0 : 3 ) + a->kind, bg = ( b->required ? 0 : 3 ) + b->kind;
	return ag != bg ? ag - bg : static_cast< int >( a->key ) - static_cast< int >( b->key );
}
bool CScriptRule::ReadCriterion( IResponseSystem *system, HSCRIPT table )
{
	Criterion_t criterion; criterion.required = !ScriptOptional( table );
	ScriptVariant_t weight;
	if ( g_pScriptVM->GetValue( table, "weight", &weight ) )
	{
		double number = ScriptNumber( weight );
		criterion.weight = static_cast< float >( number );
		if ( number <= 0.0 ) { Warning( "Criterion specified invalid weight %f, overriding to 1\n", number ); criterion.weight = 1.0f; }
	}
	g_pScriptVM->ReleaseValue( weight );
	ScriptVariant_t function;
	if ( g_pScriptVM->GetValue( table, "func", &function ) )
	{
		if ( function.GetType() != FIELD_HSCRIPT )
		{
			Warning( "criterion has a 'func' field with inappropriate type %s, rejecting.\n", VariantFieldTypeName( function.GetType() ) );
			g_pScriptVM->ReleaseValue( function ); return false;
		}
		criterion.kind = Criterion_t::FUNCTION; criterion.required = true; criterion.weight = 1.0f;
		criterion.key = ScriptRuleSymbol( system, "DUMMY_FACT_NOT_USED" );
		HSCRIPT handle = static_cast< HSCRIPT >( function );
		criterion.function = handle ? g_pScriptVM->ReferenceScope( handle ) : NULL;
		g_pScriptVM->ReleaseValue( function ); m_Criteria.AddToTail( criterion ); return true;
	}
	g_pScriptVM->ReleaseValue( function );
	ScriptVariant_t key, bottom, top;
	bool ok = g_pScriptVM->GetValue( table, "key", &key );
	if ( !ok ) Warning( "criterion doesn't have a 'key' field, rejecting\n" );
	else if ( !( ok = g_pScriptVM->GetValue( table, "bottom", &bottom ) ) ) Warning( "criterion doesn't have a 'bottom' field, rejecting\n" );
	else if ( !( ok = g_pScriptVM->GetValue( table, "top", &top ) ) ) Warning( "criterion doesn't have a 'top' field, rejecting\n" );
	else if ( !( ok = key.GetType() == FIELD_CSTRING ) ) Warning( "criterion specified invalid key of type %s\n", VariantFieldTypeName( key.GetType() ) );
	if ( ok )
	{
		criterion.key = ScriptRuleSymbol( system, static_cast< const char * >( key ) );
		if ( bottom.GetType() == FIELD_CSTRING )
		{
			if ( top.GetType() != FIELD_CSTRING )
			{
				Warning( "criterion has a 'bottom' field that's a string and a 'top' field that's a %s, this is meaningless\n", VariantFieldTypeName( top.GetType() ) );
				ok = false;
			}
			else if ( Q_strcmp( static_cast< const char * >( bottom ), static_cast< const char * >( top ) ) )
			{
				Warning( "criterion specifies a 'bottom' of %s and a 'top' of %s; string range queries are not supported\n", static_cast< const char * >( bottom ), static_cast< const char * >( top ) );
				ok = false;
			}
			else
			{
				criterion.kind = Criterion_t::STRING;
				criterion.text = ScriptRuleSymbol( system, static_cast< const char * >( bottom ) );
			}
		}
		else
		{
			criterion.bottom = bottom.GetType() == FIELD_VOID ? -FLT_MAX : static_cast< float >( ScriptNumber( bottom ) );
			criterion.top = top.GetType() == FIELD_VOID ? FLT_MAX : static_cast< float >( ScriptNumber( top ) );
		}
		if ( ok ) m_Criteria.AddToTail( criterion );
	}
	g_pScriptVM->ReleaseValue( key ); g_pScriptVM->ReleaseValue( bottom ); g_pScriptVM->ReleaseValue( top );
	return ok;
}
bool CScriptRule::ReadTable( IResponseSystem *system, HSCRIPT table, bool verbose )
{
	ScriptVariant_t name; CScriptAutoRef criteria, responses;
	if ( !g_pScriptVM->GetValue( table, "rulename", &name ) ) { Warning( "Bad rule passed to Script_AddDecisionRule: no rulename\n" ); return false; }
	// Native producer supplies a string here; do not coerce to a manufactured name.
	const char *ruleName = static_cast< const char * >( name );
	if ( !GetObject( table, "criteria", criteria ) )
	{
		Warning( "Bad rule passed to Script_AddDecisionRule: no criteria\n" ); g_pScriptVM->ReleaseValue( name ); return false;
	}
	if ( !GetObject( table, "responses", responses ) )
	{
		Warning( "Bad rule passed to Script_AddDecisionRule: no responses\n" ); g_pScriptVM->ReleaseValue( name ); return false;
	}
	int count = VScriptGetArrayCount( criteria );
	if ( verbose ) Msg( "\tPROCESSING RESPONSE RULE FROM .NUT :\nrulename is %s\ncriteria is an array of %d\n", ruleName, count );
	if ( count < 1 ) { Warning( "Rule %s had no criteria. Rejected!\n", ruleName ); g_pScriptVM->ReleaseValue( name ); return false; }
	for ( int i = 0; i < count; ++i )
	{
		ScriptVariant_t entry; VScriptGetScalarValue( criteria, i, &entry );
		if ( entry.GetType() != FIELD_HSCRIPT )
		{
			Warning( "An entry in rule %s's \"criterion\" array wasn't a script table. Instead it was a %s.\n\tRejecting rule.", ruleName, VariantFieldTypeName( entry.GetType() ) );
			g_pScriptVM->ReleaseValue( entry ); g_pScriptVM->ReleaseValue( name ); return false;
		}
		ReadCriterion( system, static_cast< HSCRIPT >( entry ) ); // S2C4260 IGNORES conversion failure here.
		g_pScriptVM->ReleaseValue( entry );
	}
	m_Response = new CSpeechScriptBridge::CSpeechResponse_t( table, responses );
	bool valid = m_Response->IsValid();
	if ( !valid ) Warning( "Couldn't make response array for rule %s . Rejecting.\n", ruleName );
	g_pScriptVM->ReleaseValue( name ); m_Criteria.Sort( CompareCriteria );
	return valid;
}
bool CScriptRule::Matches( const Criterion_t &criterion, const CScriptRuleQuery &query ) const
{
	if ( criterion.kind == Criterion_t::FUNCTION )
	{
		HSCRIPT table = query.CreateTable(); ScriptVariant_t result;
		g_pScriptVM->Call( criterion.function, NULL, true, &result, table );
		bool match = result.GetType() != FIELD_VOID && ( result.GetRawPayload32() & 255 ) != 0;
		g_pScriptVM->ReleaseValue( result ); g_pScriptVM->ReleaseScript( table ); return match;
	}
	const CScriptRuleQuery::Fact_t *fact = query.Find( criterion.key );
	if ( !fact ) return false;
	if ( criterion.kind == Criterion_t::STRING ) return fact->integer == static_cast< int >( criterion.text );
	// S4F58B2/4F5B91 COMISS/JB rejects unordered values, including string sentinels.
	// There is no separate type gate; keep the original ordered comparisons.
	return fact->number >= criterion.bottom && criterion.top >= fact->number;
}
float CScriptRule::Score( const CScriptRuleQuery &query, bool *accepted ) const
{
	if ( accepted ) *accepted = false;
	float total = 0.0f; int next = 0;
	for ( int group = 0; group != 6; ++group )
	{
		float subtotal = 0.0f; int count = 0;
		while ( next < m_Criteria.Count() )
		{
			const Criterion_t &criterion = m_Criteria[next];
			if ( ( criterion.required ? 0 : 3 ) + criterion.kind != group ) break;
			++next; ++count;
			if ( Matches( criterion, query ) ) subtotal = criterion.weight + subtotal;
			else if ( criterion.required ) return SCRIPT_NO_MATCH;
		}
		// S4F60A1/60D0/6104 FCOMIP/JBE rejects a NaN required subtotal too.
		if ( group < 3 && count && !( subtotal > 0.0f ) ) return SCRIPT_NO_MATCH;
		if ( group == 5 )
		{
			// S4F6146 keeps this final x87 sum in-register through both tests.
			// Earlier group totals spill to float; only this last sum is widened.
			double result = static_cast< double >( subtotal ) + total;
			if ( accepted ) *accepted = result != 0.0;
			return result > 0.0 ? static_cast< float >( result ) : SCRIPT_NO_MATCH;
		}
		total = subtotal + total;
	}
	return SCRIPT_NO_MATCH;
}

CRuleScriptBridge::CRuleScriptBridge() : m_System( NULL )
{
	Msg( "CRuleScriptBridge initializing...\n" );
	m_System = CreateScriptRuleResponseSystem();
	s_Deciders.AddToTail( this );
}
CRuleScriptBridge::~CRuleScriptBridge() { s_Deciders.FindAndRemove( this ); DestroyScriptRuleResponseSystem( m_System ); }
void CRuleScriptBridge::ReleaseScriptReferences() { RemoveScriptRules( m_System ); }
bool CRuleScriptBridge::Binding_AddRuleFromScript( HSCRIPT table )
{
	CScriptRule *rule = new CScriptRule;
	bool result = rule->ReadTable( m_System, table, g_pDeveloper->GetInt() > 0 ) && StoreScriptRule( m_System, rule );
	rule->Release(); return result;
}
HSCRIPT CRuleScriptBridge::Binding_FindBestMatch( HSCRIPT table )
{
	CScriptRuleQuery query( m_System ); if ( !query.ReadTable( table ) ) { Warning( "Unable to parse script table to Decider query.\n" ); return NULL; }
	float score; CScriptRule *rule = FindBestScriptRule( m_System, query, score );
	return rule ? g_pScriptVM->ReferenceScope( rule->GetResponse()->GetResponses() ) : NULL;
}
HSCRIPT CRuleScriptBridge::Binding_FindAllMatches( HSCRIPT table, float leeway )
{
	CScriptRuleQuery query( m_System ); if ( !query.ReadTable( table ) ) { Warning( "Unable to parse script table to Decider query.\n" ); return NULL; }
	CUtlVector<ScriptRuleMatch_t> matches; FindAllScriptRules( m_System, query, matches );
	if ( !matches.Count() ) return NULL;
	ScriptVariant_t array; VScriptCreateArray( array ); HSCRIPT result = array;
	const float cutoff = matches[0].score - leeway;
	FOR_EACH_VEC( matches, i )
	{
		// S2C3B9A COMISS/JB stops for an unordered cutoff as well as a lower score.
		if ( !( matches[i].score >= cutoff ) ) break;
		if ( matches[i].rule ) VScriptArrayAddToTail( result, ScriptVariant_t( matches[i].rule->GetResponse()->GetResponses() ) );
	}
	return result;
}
BEGIN_SCRIPTDESC_ROOT_NAMED( CRuleScriptBridge, "Decider", "An isolated instance of a rulescript database." )
	DEFINE_SCRIPT_CONSTRUCTOR()
	DEFINE_SCRIPTFUNC_NAMED( Binding_AddRuleFromScript, "AddRule", "Add a CRule object." )
	DEFINE_SCRIPTFUNC_NAMED( Binding_FindBestMatch, "FindBestMatch", "Return an arbitrary best-scoring match." )
	DEFINE_SCRIPTFUNC_NAMED( Binding_FindAllMatches, "FindAllMatches", "Return responses within leeway of the best score." )
END_SCRIPTDESC()

// VM-owned opaque snapshots. Their C++ instances are reclaimed by the actual VM
// descriptor destructor; clear retained response functions BEFORE DestroyVM.
class AI_ResponseScriptWrapper;
static CUtlVector<AI_ResponseScriptWrapper *> s_Snapshots;
class AI_ResponseScriptWrapper
{
public:
	explicit AI_ResponseScriptWrapper( const AI_Response &response ) : m_Response( response ) { s_Snapshots.AddToTail( this ); }
	~AI_ResponseScriptWrapper() { s_Snapshots.FindAndRemove( this ); }
	float GetMatchScore() { return m_Response.GetMatchScore(); }
	AI_Response &GetResponse() { return m_Response; }
private:
	AI_Response m_Response;
};
static void DestroyResponseWrapper( void *instance ) { delete static_cast< AI_ResponseScriptWrapper * >( instance ); }
BEGIN_SCRIPTDESC_ROOT_NAMED( AI_ResponseScriptWrapper, "AI_Response", "Opaque type representing a return value from the old response system." )
	pDesc->m_pfnDestruct = DestroyResponseWrapper;
	DEFINE_SCRIPTFUNC( GetMatchScore, "Relative score of this match's quality (based on number of criteria matched)" )
END_SCRIPTDESC()
static bool QueryToCriteria( HSCRIPT table, AI_CriteriaSet &criteria )
{
	int count = g_pScriptVM->GetNumTableEntries( table ), iterator = 0;
	for ( int i = 0; i < count; ++i )
	{
		ScriptVariant_t key, value; iterator = g_pScriptVM->GetKeyValue( table, iterator, &key, &value );
		// S2C2C61 passes the raw key payload as a string. Skip only the
		// name-dependent step for a non-string/NULL key, retaining release/count.
		const char *name = key.GetType() == FIELD_CSTRING ? static_cast< const char * >( key ) : NULL;
		if ( name )
		{
			switch ( value.GetType() )
			{
			case FIELD_FLOAT: case FIELD_INTEGER: case FIELD_SHORT: case FIELD_VECTOR2D: case FIELD_UINT64: case FIELD_FLOAT64: case FIELD_UINT:
				{ char text[64]; Q_snprintf( text, 63, "%f", ScriptNumber( value ) ); criteria.AppendCriteria( name, text, 1.0f ); } break;
			case FIELD_CSTRING: criteria.AppendCriteria( name, static_cast< const char * >( value ), 1.0f ); break;
			default: Warning( "Unable to coerce table entry '%s' to AI_Criteria from type %d, they're incompatible\n", name, value.GetType() ); break;
			}
		}
		g_pScriptVM->ReleaseValue( key ); g_pScriptVM->ReleaseValue( value );
	}
	return count > 0;
}
bool CSpeechScriptBridge::Script_AddDecisionRule( HSCRIPT table )
{
	CScriptRule *rule = new CScriptRule;
	bool result = rule->ReadTable( s_SpeechRules, table, false ) && StoreScriptRule( s_SpeechRules, rule );
	rule->Release(); return result;
}
HSCRIPT CSpeechScriptBridge::Script_FindBestResponse( HSCRIPT entity, HSCRIPT table )
{
	CBaseEntity *speaker = ToEnt( entity );
	if ( !speaker ) { Warning( "Couldn't resolve entity in Script_FindBestResponse\n" ); return NULL; }
	CAI_Expresser *expresser = ResponseQueueManager().GetExpresser( speaker );
	if ( !expresser ) { Warning( "Couldn't resolve entity %s to its AI expresser; you can't send it a speech query.\n", speaker->GetDebugName() ); return NULL; }
	AI_CriteriaSet criteria;
	if ( !QueryToCriteria( table, criteria ) ) { Warning( "Script_FindBestResponse couldn't convert second parameter to a criterion set.\n " ); return NULL; }
	CUtlString concept( criteria.GetValue( criteria.FindCriterionIndex( "concept" ) ) );
	expresser->GatherSpeechCriteria( criteria );
	AI_Response response;
	if ( !expresser->FindResponseWithCriteria( response, concept.String(), criteria ) ) return NULL;
	ScriptVariant_t result; g_pScriptVM->CreateTable( result ); HSCRIPT handle = result;
	AI_ResponseScriptWrapper *snapshot = new AI_ResponseScriptWrapper( response );
	HSCRIPT instance = g_pScriptVM->RegisterInstance( snapshot );
	if ( instance )
	{
		g_pScriptVM->SetValue( handle, "response", ScriptVariant_t( instance ) );
		g_pScriptVM->ReleaseScript( instance );
	}
	else
	{
		delete snapshot;
	}
	HSCRIPT originalQuery = SpeechCriteriaToScriptTable( criteria );
	g_pScriptVM->SetValue( handle, "orig_query", ScriptVariant_t( originalQuery ) );
	g_pScriptVM->ReleaseScript( originalQuery );
	g_pScriptVM->SetValue( handle, "score", ScriptVariant_t( response.GetMatchScore() ) );
	return handle;
}
bool CSpeechScriptBridge::Script_CommitAIResponse( HSCRIPT entity, HSCRIPT table )
{
	CScriptAutoRef responseObject, originalQuery;
	if ( !GetObject( table, "response", responseObject ) ) { Warning( "Didn't find a 'response' field in parameter to UnpackScriptTableToAIResponse\n" ); return false; }
	if ( !GetObject( table, "orig_query", originalQuery ) ) { Warning( "Didn't find a 'orig_query' field in parameter to UnpackScriptTableToAIResponse\n" ); return false; }
	AI_ResponseScriptWrapper *snapshot = static_cast< AI_ResponseScriptWrapper * >( g_pScriptVM->GetInstanceValue( responseObject, GetScriptDescForClass( AI_ResponseScriptWrapper ) ) );
	AI_CriteriaSet criteria; QueryToCriteria( originalQuery, criteria );
	CUtlString concept( criteria.GetValue( criteria.FindCriterionIndex( "concept" ) ) );
	CBaseEntity *speaker = ToEnt( entity ); CAI_Expresser *expresser = speaker ? ResponseQueueManager().GetExpresser( speaker ) : NULL;
	if ( !expresser ) { Warning( "Couldn't convert %s to an entity type that has an expresser\n", speaker ? speaker->GetDebugName() : "<<null>>" ); return false; }
	AI_Response response( snapshot->GetResponse() );
	return expresser->SpeakDispatchResponse( concept.String(), response, NULL, &criteria );
}
HSCRIPT CSpeechScriptBridge::Script_GetExpressers() { return ResponseQueueManager().GetScriptResponseTargets(); }
bool FindScriptedSpeechResponse( IResponseSystem *, const AI_CriteriaSet &criteria, AI_Response &response )
{
	if ( !s_SpeechRules ) return false;
	CScriptRuleQuery query( s_SpeechRules ); query.ReadCriteria( criteria );
	float score; CScriptRule *rule = FindBestScriptRule( s_SpeechRules, query, score );
	return rule && score > 0.0f && rule->GetResponse()->FindResponse( response, criteria, score );
}
void RegisterSpeechScriptServiceClasses()
{
	g_pScriptVM->RegisterClass( GetScriptDescForClass( CRuleScriptBridge ) );
}
void VScriptRunScriptAddons( const char *name, HSCRIPT scope, bool warnMissing )
{
	// Main-approved API-shape adaptation of native filesystem virtual+132.
	// Enumerate ALL GAME directories in their actual order, not FileExists on
	// the shared GAME ID (which would repeatedly select only the first copy).
	int length = g_pFullFileSystem->GetSearchPath( "GAME", false, NULL, 0 );
	CUtlVector<char> paths; paths.SetCount( length + 1 );
	paths[length] = 0;
	g_pFullFileSystem->GetSearchPath( "GAME", false, paths.Base(), paths.Count() );
	for ( char *path = paths.Base(); *path; )
	{
		char *end = Q_strstr( path, ";" ); if ( end ) *end = 0;
		CUtlString filename; filename.Format( "%s/scripts/vscripts/%s_addon.nut", path, name );
		V_FixSlashes( filename.GetForModify() );
		if ( g_pFullFileSystem->FileExists( filename.String(), NULL ) )
		{
			Msg( ">>> Loading addon script %s\n", filename.String() );
			CUtlBuffer source( 0, 0, CUtlBuffer::TEXT_BUFFER );
			if ( g_pFullFileSystem->ReadFile( filename.String(), NULL, source ) )
			{
				source.PutChar( 0 );
				HSCRIPT script = g_pScriptVM->CompileScript( static_cast< const char * >( source.Base() ), filename.String() );
				if ( script ) { g_pScriptVM->Run( script, scope, true ); g_pScriptVM->ReleaseScript( script ); }
				else if ( warnMissing ) Warning( "Failed to compile addon script %s\n", filename.String() );
			}
		}
		if ( !end ) break; path = end + 1;
	}
}
void InitSpeechScriptBridge()
{
	if ( s_Initialized )
	{
		Msg( "CSpeechScriptBridge was double-initialized: tearing down and rebuilding\n" );
		s_Initialized = false;
		if ( s_SpeechScope && s_SpeechScope != INVALID_HSCRIPT ) g_pScriptVM->ReleaseScope( s_SpeechScope );
		s_SpeechScope = NULL;
		DestroyScriptRuleResponseSystem( s_SpeechRules ); s_SpeechRules = NULL;
	}
	else Msg( "CSpeechScriptBridge initializing...\n" );
	s_SpeechScope = g_pScriptVM->CreateScope( "g_rr", NULL );
	s_SpeechRules = CreateScriptRuleResponseSystem();
	bool loaded = VScriptRunScript( "response_testbed", s_SpeechScope == INVALID_HSCRIPT ? NULL : s_SpeechScope, true );
	VScriptRunScriptAddons( "response_testbed", s_SpeechScope == INVALID_HSCRIPT ? NULL : s_SpeechScope, true );
	Msg( "\tHSCRIPT %s\n", loaded ? "loaded successfully" : "failed to load" );
	s_Initialized = true;
}
void ShutdownSpeechScriptBridge()
{
	ResponseQueueManager().Clear();
	// Pop weak native registry entries BEFORE releasing VM references: GC may
	// destroy the instance or another registered instance during that release.
	while ( s_Deciders.Count() )
	{
		CRuleScriptBridge *bridge = s_Deciders.Tail();
		s_Deciders.Remove( s_Deciders.Count() - 1 );
		bridge->ReleaseScriptReferences();
	}
	while ( s_Snapshots.Count() )
	{
		AI_ResponseScriptWrapper *snapshot = s_Snapshots.Tail();
		s_Snapshots.Remove( s_Snapshots.Count() - 1 );
		snapshot->GetResponse().GetScriptData().Clear();
	}
	ResponseQueueManager().ReleaseScriptReferences();
	if ( s_Initialized )
	{
		s_Initialized = false;
		if ( s_SpeechScope && s_SpeechScope != INVALID_HSCRIPT ) g_pScriptVM->ReleaseScope( s_SpeechScope );
		s_SpeechScope = NULL;
		DestroyScriptRuleResponseSystem( s_SpeechRules ); s_SpeechRules = NULL;
	}
}

#pragma float_control( pop )
