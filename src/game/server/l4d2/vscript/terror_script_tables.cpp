//========= Copyright Valve Corporation, All rights reserved. ============//
// Native2790E0/279C90/21C4C0 and276970/275FE0/275D70/223E40/223D30.
#include "cbase.h"
#include "l4d2/director/terror_director.h"
#include "vscript_shared.h"
#include "vscript_container_helpers.h"
#include "tier1/KeyValues.h"
#include "tier1/utlvector.h"
#include "tier0/memdbgon.h"

namespace
{
KeyValues *ScriptTableToSavedKeyValues( const char *name, HSCRIPT table, CUtlVector< HSCRIPT > &ancestors );
// 275D70: key conversion is NOT VM tostring(). Numeric/boolean/character
// keys become strings, empty keys are omitted, and the fixed128 buffer clips.
bool SavedKeyName( const ScriptVariant_t &key, char ( &name )[128] )
{
	switch ( key.GetType() )
	{
	case FIELD_FLOAT: Q_snprintf( name, sizeof( name ), "%f", (float)key ); return true;
	case FIELD_INTEGER: Q_snprintf( name, sizeof( name ), "%d", (int)key ); return true;
	case FIELD_BOOLEAN: Q_snprintf( name, sizeof( name ), "%d", (bool)key ? 1 : 0 ); return true;
	case FIELD_CHARACTER: Q_snprintf( name, sizeof( name ), "%c", (char)key ); return true;
	case FIELD_CSTRING: Q_snprintf( name, sizeof( name ), "%s", (const char *)key ); return true;
	default:
		Warning( "ScriptVmStringFromVariant failed to unpack parameter variant type %d\n", key.GetType() );
		return false;
	}
}
KeyValues *SavedValueFromScript( const ScriptVariant_t &value, CUtlVector< HSCRIPT > &ancestors )
{
	KeyValues *result;
	switch ( value.GetType() )
	{
	case FIELD_FLOAT:
		result = new KeyValues( "" ); result->SetFloat( NULL, (float)value ); return result;
	case FIELD_INTEGER:
		result = new KeyValues( "" ); result->SetInt( NULL, (int)value ); return result;
	case FIELD_BOOLEAN:
		result = new KeyValues( "" ); result->SetInt( NULL, (bool)value ? 1 : 0 ); return result;
	case FIELD_CHARACTER:
		{
			char text[2] = { (char)value, 0 };
			result = new KeyValues( "" ); result->SetString( NULL, text ); return result;
		}
	case FIELD_CSTRING:
		result = new KeyValues( "" ); result->SetString( NULL, (const char *)value ); return result;
	case FIELD_HSCRIPT:
		return ScriptTableToSavedKeyValues( "", (HSCRIPT)value, ancestors );
	default:
		Warning( "ScriptVmKeyValueFromVariant failed to unpack parameter variant type %d\n", value.GetType() );
		return NULL;
	}
}
KeyValues *ScriptTableToSavedKeyValues( const char *name, HSCRIPT table, CUtlVector< HSCRIPT > &ancestors )
{
	KeyValues *result = new KeyValues( name ? name : "" );
	if ( !table || !g_pScriptVM ) return result;
	// Main's 2026-10-23 extension of ledger302: only CURRENT-PATH ancestor
	// revisits terminate recursion. Do not deduplicate shared acyclic tables.
	FOR_EACH_VEC( ancestors, index )
	{
		bool same;
		if ( !VScriptIsSameObject( table, ancestors[index], &same ) || same ) return result;
	}
	ancestors.AddToTail( table ); // Borrowed: caller keeps each value alive through recursion.
	int iterator = 0;
	for ( ;; )
	{
		ScriptVariant_t key, value;
		iterator = g_pScriptVM->GetKeyValue( table, iterator, &key, &value );
		if ( iterator == -1 ) break;
		char text[128] = { 0 };
		SavedKeyName( key, text );
		if ( text[0] )
		{
			KeyValues *child = SavedValueFromScript( value, ancestors );
			// This is the native representation of an unsupported value, NOT
			// a dropped field: restore later produces an empty table here.
			if ( !child ) child = new KeyValues( "" );
			child->SetName( text );
			result->AddSubKey( child );
		}
		g_pScriptVM->ReleaseValue( key );
		g_pScriptVM->ReleaseValue( value );
	}
	ancestors.Remove( ancestors.Count() - 1 );
	return result;
}
void SavedKeyValuesToScriptTable( KeyValues *root, HSCRIPT table );
bool ScriptValueFromSaved( KeyValues *key, ScriptVariant_t &value, char ( &buffer )[128] )
{
	switch ( key->GetDataType() )
	{
	case KeyValues::TYPE_NONE:
		g_pScriptVM->CreateTable( value );
		SavedKeyValuesToScriptTable( key, (HSCRIPT)value );
		return true;
	case KeyValues::TYPE_STRING: value = key->GetString(); return true;
	case KeyValues::TYPE_INT: value = key->GetInt(); return true;
	case KeyValues::TYPE_FLOAT: value = key->GetFloat(); return true;
	case KeyValues::TYPE_UINT64:
		Q_snprintf( buffer, sizeof( buffer ), "%llu", key->GetUint64( NULL, 0 ) );
		value = buffer; return true;
	default:
		Warning( "ScriptVmKeyValueToVariant failed to package parameter %s (type %d)\n", key->GetName(), key->GetDataType() );
		return false;
	}
}
void SavedKeyValuesToScriptTable( KeyValues *root, HSCRIPT table )
{
	if ( !root || !g_pScriptVM ) return;
	for ( KeyValues *key = root->GetFirstSubKey(); key; key = key->GetNextKey() )
	{
		ScriptVariant_t value;
		char buffer[128];
		if ( ScriptValueFromSaved( key, value, buffer ) ) g_pScriptVM->SetValue( table, key->GetName(), value );
		g_pScriptVM->ReleaseValue( value );
	}
}
}

void CDirectorScriptedMode::SaveTable( const char *name, HSCRIPT table )
{
	if ( !VScriptIsTable( table ) ) { Warning( "SaveTable: \"%s\" is not a table!\n", name ); return; }
	if ( !m_pSavedTables ) m_pSavedTables = new KeyValues( "sm_Persist" );
	KeyValues *old = m_pSavedTables->FindKey( name, false );
	if ( old ) { m_pSavedTables->RemoveSubKey( old ); old->deleteThis(); }
	CUtlVector< HSCRIPT > ancestors;
	m_pSavedTables->AddSubKey( ScriptTableToSavedKeyValues( name, table, ancestors ) );
}
void CDirectorScriptedMode::RestoreTable( const char *name, HSCRIPT table )
{
	if ( !m_pSavedTables ) return; // Native does not even type-check without a saved root.
	if ( !VScriptIsTable( table ) ) { Warning( "RestoreTable: \"%s\" is not a table!\n", name ); return; }
	KeyValues *saved = m_pSavedTables->FindKey( name, false );
	if ( saved )
	{
		SavedKeyValuesToScriptTable( saved, table ); // Merge, do not clear unrelated entries.
		m_pSavedTables->RemoveSubKey( saved );
		saved->deleteThis(); // Restore CONSUMES this named snapshot.
	}
}
void CDirectorScriptedMode::ClearSavedTables()
{
	if ( m_pSavedTables ) { m_pSavedTables->deleteThis(); m_pSavedTables = NULL; }
}
