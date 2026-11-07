// Transplant into existing src/game/server/vscript_server.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

static unsigned int s_ResurrectedUnreachablesCount = 0; // native834D80 BSS0

// Native callback 0x0022B820 (command_function).
static void CC_script_reload_enity_code( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	const char *target = args.ArgC() > 1 && *args[1] ? args[1] : "";
	if ( !g_pScriptVM ) { Msg( "Scripting disabled or no server running\n" ); return; }
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	extern CBaseEntity *GetNextCommandEntity( CBasePlayer *, const char *, CBaseEntity * );
	CBaseEntity *entity = NULL;
	while ( ( entity = GetNextCommandEntity( player, target, entity ) ) != NULL )
	{
		if ( entity->m_ScriptScope.IsInitialized() && entity->m_iszVScripts != NULL_STRING )
		{
			char text[256];
			const char *list = STRING( entity->m_iszVScripts );
			if ( Q_strlen( list ) >= sizeof( text ) ) continue; // Ledger306: skip only overflowing local-copy/parser dependent work.
			Q_strcpy( text, list );
			CUtlStringList scripts;
			V_SplitString( text, " ", scripts );
			FOR_EACH_VEC( scripts, i ) VScriptServerReplaceClosures( scripts[i], entity->m_ScriptScope, true );
		}
	}
}

// Native object 0x00834AA0; callback 0x0022B820; command_function.
static ConCommand script_reload_enity_code_command( "script_reload_enity_code", CC_script_reload_enity_code, "Execute all of this entity's VScripts, replacing existing functions with the functions in the run scripts", 0x00004004 );

// Native callback 0x00220570 (command_function).
static void CC_script_resurrect_unreachable( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	if ( !g_pScriptVM ) { Msg( "Scripting disabled or no server running\n" ); return; }
	if ( args.ArgC() > 1 && *args[1] )
	{
		// S2205C7 sign-extends the first byte into an invalid pointer. The
		// dependent VM use is minimally skipped under ledger306, not repaired.
		return;
	}
	char name[256];
	++s_ResurrectedUnreachablesCount;
	Q_snprintf( name, sizeof( name ), "ResurrectedUnreachables%d", static_cast< int >( s_ResurrectedUnreachablesCount ) );
	// Main-approved capability: target IScriptVM has no original +188 method.
	// Only that call is omitted; count/name generation above is retained.
}

// Native object 0x00834A58; callback 0x00220570; command_function.
static ConCommand script_resurrect_unreachable_command( "script_resurrect_unreachable", CC_script_resurrect_unreachable, "Use the garbage collector to track down reference cycles", 0x00004004 );

#endif
