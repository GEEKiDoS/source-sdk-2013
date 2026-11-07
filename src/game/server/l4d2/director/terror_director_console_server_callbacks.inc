// Transplant into existing src/game/server/l4d2/terror_director.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// S27AD70 ->27AB50. This is the genuine cached UserConsoleCommand owner.
void CDirectorScriptedMode::UserConsoleCommand( CBaseEntity *entity, const ScriptVariant_t &argument )
{
 if ( !m_hUserConsoleCommand || m_hUserConsoleCommand == INVALID_HSCRIPT )
 {
  DevMsg( "No scripted user function to call\n" );
  return;
 }
 ScriptVariant_t result;
 HSCRIPT instance = entity ? entity->GetScriptInstance() : NULL;
 const ScriptStatus_t status = g_pScriptVM->Call( m_hUserConsoleCommand,
  TheDirector->GetScriptScope( 1 ), true, &result, instance, argument );
 if ( status != SCRIPT_DONE ) DevWarning( "ScriptedMode: Failed to successfully call user console func\n" );
 result.Free();
}

// Native callback 0x0026B9F0 (command_function).
static void CC_director_begin_script( const CCommand &args )
{
	if ( args.ArgC() < 2 ) return;
	TheDirector->StopLocalScript();
	TheDirector->RunScript( args[1], 2, false );
}

// Native object 0x0083D8A0; callback 0x0026B9F0; command_function.
static ConCommand director_begin_script_command( "director_begin_script", CC_director_begin_script, "Begin a director script", 0x00004004 );

// Native callback 0x002689B0 (command_function).
static void CC_director_end_script( const CCommand &args )
{
	TheDirector->StopLocalScript();
}

// Native object 0x0083D8C4; callback 0x002689B0; command_function.
static ConCommand director_end_script_command( "director_end_script", CC_director_end_script, "End a director script", 0x00004004 );

// Native callback 0x00287900 (command_function).
static void CC_director_force_panic_event( const CCommand &args )
{
	TheDirector->OnScriptedPanicEvent( NULL, 0 );
}

// Native object 0x0083E4DC; callback 0x00287900; command_function.
static ConCommand director_force_panic_event_command( "director_force_panic_event", CC_director_force_panic_event, "Forces a 'PanicEvent' to occur", 0x00004004 );

// Native callback 0x0026BA30 (command_function).
static void CC_director_force_scripted_panic_event( const CCommand &args )
{
	if ( args.ArgC() < 2 ) return;
	TheDirector->StopLocalScript();
	TheDirector->RunScript( args[1], 2, false );
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( TheDirector->GetSurvivalMode()->OnScriptedPanicEvent( player, 3 ) == 2 )
		TheDirector->GetScenario()->OnScriptedPanicEvent( player, 3 );
}

// Native object 0x0083D90C; callback 0x0026BA30; command_function.
static ConCommand director_force_scripted_panic_event_command( "director_force_scripted_panic_event", CC_director_force_scripted_panic_event, "Force the specified scripted panic event to happen", 0x00004004 );

// Native callback 0x0027AEC0 (command_function).
static void CC_scripted_user_func( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	ScriptVariant_t argument;
	if ( args.ArgC() > 1 ) argument = args[1];
	TheDirector->GetScriptedMode()->UserConsoleCommand( player, argument );
}

// Native object 0x0083DC6C; callback 0x0027AEC0; command_function.
static ConCommand scripted_user_func_command( "scripted_user_func", CC_scripted_user_func, "Call script from this user, with the value", 0x00000004 );

#endif
