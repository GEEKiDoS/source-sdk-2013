//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "vscript_shared.h"
#include "vscript_container_helpers.h"
#include "tier0/memdbgon.h"

// The engine owns VM010. These closures adapt ONLY missing container API
// shapes, never response content, globals, VM vtables, or script producers.
static HSCRIPT s_ContainerFunctions[6];
static HSCRIPT ContainerFunction( int operation )
{
	static const char *code[] =
	{
		"return function(value) { return typeof value == \"table\"; };",
		"return function(value) { return value.len(); };",
		"return function(value,index) { return value[index]; };",
		"return function() { return []; };",
		"return function(value,entry) { value.append(entry); };",
		"return function(left,right) { return left == right; };"
	};
	if ( !s_ContainerFunctions[operation] )
	{
		HSCRIPT compiled = g_pScriptVM->CompileScript( code[operation], "VM container API adaptation" );
		if ( compiled )
		{
			ScriptVariant_t function;
			g_pScriptVM->ExecuteFunction( compiled, NULL, 0, &function, NULL, true );
			if ( function.GetType() == FIELD_HSCRIPT )
				s_ContainerFunctions[operation] = static_cast< HSCRIPT >( function );
			else
				g_pScriptVM->ReleaseValue( function );
			g_pScriptVM->ReleaseScript( compiled );
		}
	}
	return s_ContainerFunctions[operation];
}
bool VScriptIsTable( HSCRIPT object )
{
	HSCRIPT function = ContainerFunction( 0 );
	if ( !function ) return false;
	ScriptVariant_t value;
	g_pScriptVM->Call( function, NULL, true, &value, object );
	bool result = value.GetType() == FIELD_BOOLEAN && static_cast< bool >( value );
	g_pScriptVM->ReleaseValue( value );
	return result;
}
int VScriptGetArrayCount( HSCRIPT array )
{
	HSCRIPT function = ContainerFunction( 1 );
	if ( !function ) return 0;
	ScriptVariant_t value;
	g_pScriptVM->Call( function, NULL, true, &value, array );
	int count = value.GetType() == FIELD_INTEGER ? static_cast< int >( value ) : 0;
	g_pScriptVM->ReleaseValue( value );
	return count;
}
bool VScriptGetScalarValue( HSCRIPT array, int index, ScriptVariant_t *value )
{
	HSCRIPT function = ContainerFunction( 2 );
	return function && g_pScriptVM->Call( function, NULL, true, value, array, index ) != SCRIPT_ERROR;
}
bool VScriptCreateArray( ScriptVariant_t &array )
{
	HSCRIPT function = ContainerFunction( 3 );
	return function && g_pScriptVM->Call( function, NULL, true, &array ) != SCRIPT_ERROR;
}
bool VScriptArrayAddToTail( HSCRIPT array, const ScriptVariant_t &value )
{
	HSCRIPT function = ContainerFunction( 4 );
	ScriptVariant_t arguments[] = { ScriptVariant_t( array ), value };
	return function && g_pScriptVM->ExecuteFunction( function, arguments, ARRAYSIZE( arguments ), NULL, NULL, true ) != SCRIPT_ERROR;
}
// Wrapper handles can differ while referring to the same table. Ask the VM.
bool VScriptIsSameObject( HSCRIPT left, HSCRIPT right, bool *same )
{
	HSCRIPT function = ContainerFunction( 5 );
	if ( !function ) return false;
	ScriptVariant_t value;
	ScriptStatus_t status = g_pScriptVM->Call( function, NULL, true, &value, left, right );
	bool valid = status != SCRIPT_ERROR && value.GetType() == FIELD_BOOLEAN;
	if ( valid ) *same = static_cast< bool >( value );
	g_pScriptVM->ReleaseValue( value );
	return valid;
}
void VScriptReleaseContainerHelpers()
{
	for ( int i = 0; i < ARRAYSIZE( s_ContainerFunctions ); ++i )
	{
		// ExecuteFunction returned a closure reference, not a CreateScope owner.
		if ( s_ContainerFunctions[i] ) g_pScriptVM->ReleaseFunction( s_ContainerFunctions[i] );
		s_ContainerFunctions[i] = NULL;
	}
}
