//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "tier0/memdbgon.h"

#if defined( _MSC_VER )
#pragma float_control( precise, on, push )
#endif

// S617E0: COMISS(0,duration)/JA: only negatives use expiration zero.
// Zero duration is curtime, not a request for an immortal context.
void CBaseEntity::AddContextForScript( const char *name, const char *value, float duration )
{
	// Ledger306: native context mutation dereferences a NULL name.
	if ( !name )
		return;
	const float expiration = duration < 0.0f ? 0.0f : gpGlobals->curtime + duration;
	AddContext( name, value, expiration );
}

// S61820: six fractional digits, including numeric zero's native formatting.
void CBaseEntity::AddContextForScriptNumeric( const char *name, float value, float duration )
{
	char buffer[64];
	Q_snprintf( buffer, 63, "%f", value );
	if ( !name )
		return; // Ledger306, after the native formatting step.
	const float expiration = duration < 0.0f ? 0.0f : gpGlobals->curtime + duration;
	AddContext( name, buffer, expiration );
}

// S10980 is NOT a strict number parser. atof("1junk") is nonzero and numeric;
// empty, ".", and strings containing only '0' / '.' are numeric zero; "-0"
// and "0e0" stay strings. Preserve those defined classification quirks.
static bool IsNativeScriptContextNumeric( const char *text )
{
	if ( atof( text ) != 0.0 )
		return true;
	for ( const char *cursor = text; *cursor; ++cursor )
	{
		if ( *cursor != '0' && *cursor != '.' )
			return false;
	}
	return true;
}

// S5E7D0. Does NOT remove expired contexts before looking up a name.
// Numeric return is FIELD_FLOAT64, although the help text calls it "float".
ScriptVariant_t CBaseEntity::GetContextForScript( const char *name )
{
	if ( !name )
		return SCRIPT_VARIANT_NULL;
	const int index = FindContextByName( name );
	if ( index < 0 )
		return SCRIPT_VARIANT_NULL;
	const char *value = GetContextValue( index );
	if ( !value )
		value = "";
	if ( IsNativeScriptContextNumeric( value ) )
		return ScriptVariant_t( atof( value ) );
	return ScriptVariant_t( value );
}

// S5F060. A live override does not replace the original restoration value.
// This row was counted as owned by the census because its SDK registration
// was under an undefined ENABLE_FRICTION_OVERRIDE; the real method was absent.
void CBaseEntity::OverrideFriction( float duration, float value )
{
	if ( GetIndexForThinkContext( "FrictionRevertThink" ) == -1 ||
		GetNextThinkTick( "FrictionRevertThink" ) == -1 )
		m_flFrictionDefault = GetFriction();
	SetFriction( value );
	SetContextThink( &CBaseEntity::FrictionRevertThink, gpGlobals->curtime + duration, "FrictionRevertThink" );
}

void CBaseEntity::FrictionRevertThink() // S5EB50
{
	SetFriction( m_flFrictionDefault );
	if ( GetIndexForThinkContext( "FrictionRevertThink" ) != -1 )
		SetContextThink( NULL, -1.0f, "FrictionRevertThink" );
}

#if defined( _MSC_VER )
#pragma float_control( pop )
#endif
