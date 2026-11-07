// Transplant into existing src/game/server/l4d2/director_versus_mode.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002917A0 (command_function).
static void CC_director_force_versus_start( const CCommand &args )
{
	TheDirector->GetVersusMode()->ForceSurvivorsOutOfCheckpoint();
}

// Native object 0x0083EF6C; callback 0x002917A0; command_function.
static ConCommand director_force_versus_start_command( "director_force_versus_start", CC_director_force_versus_start, "Force start the versus game, so PZs can spawn even if survivors are still in the safe room", 0x00004004 );

// Native callback 0x00291140 (command_function).
static void CC_versus_boss_flow_test( const CCommand &args )
{
	versus_boss_debug.SetValue( 1 );
	TheDirector->GetVersusMode()->ConfigureBossSpawns();
}

// Native object 0x0083F728; callback 0x00291140; command_function.
static ConCommand versus_boss_flow_test_command( "versus_boss_flow_test", CC_versus_boss_flow_test, "Force start the versus game, so PZs can spawn even if survivors are still in the safe room", 0x00004004 );

#endif
