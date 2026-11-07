// Included in vscript_server.cpp instead of its stock ParseTable, L4D2 only.
// S:220D70. The extra active ancestry is the standing cyclic-data adaptation.
static void ParseTable( CBaseEntity *entity, HSCRIPT table, const char *keyOverride = NULL,
	CUtlVector< HSCRIPT > *ancestry = NULL )
{
	CUtlVector< HSCRIPT > rootAncestry;
	if ( !ancestry )
		ancestry = &rootAncestry;
	FOR_EACH_VEC( *ancestry, i )
	{
		bool same;
		if ( !VScriptIsSameObject( ( *ancestry )[i], table, &same ) )
		{
			Warning( "Cannot compare VScript spawn-table ancestry\n" );
			return;
		}
		if ( same )
			return;
	}
	ancestry->AddToTail( table );
	ScriptVariant_t key, value;
	int iterator = 0;
	int count = g_pScriptVM->GetNumTableEntries( table );
	for ( int i = 0; i < count; ++i )
	{
		iterator = g_pScriptVM->GetKeyValue( table, iterator, &key, &value );
		const char *name = keyOverride ? keyOverride : static_cast< const char * >( key );
		switch ( value.GetType() )
		{
		case FIELD_VECTOR:
			entity->KeyValueFromVector( name, value );
			break;
		case FIELD_INTEGER:
			entity->KeyValueFromFloat( name, static_cast< float >( static_cast< int >( value ) ) );
			break;
		case FIELD_FLOAT:
			entity->KeyValueFromFloat( name, value );
			break;
		case FIELD_BOOLEAN:
			entity->KeyValueFromFloat( name, static_cast< bool >( value ) ? 1.0f : 0.0f );
			break;
		case FIELD_CSTRING:
			entity->KeyValueFromString( name, value );
			break;
		case FIELD_HSCRIPT:
			ParseTable( entity, value, key, ancestry );
			break;
		default:
			Warning( "Unsupported KeyValue type for key %s (type %s)\n", static_cast< const char * >( key ), VariantFieldTypeName( value.GetType() ) );
			break;
		}
		g_pScriptVM->ReleaseValue( key );
		g_pScriptVM->ReleaseValue( value );
	}
	ancestry->Remove( ancestry->Count() - 1 );
}
