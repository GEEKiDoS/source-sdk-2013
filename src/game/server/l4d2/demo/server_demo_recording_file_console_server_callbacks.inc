// Transplant into existing src/game/server/l4d2/server_demo_recording_file.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x001CF5D0 (command_function).
static void CC_dump_server_demo( const CCommand &args )
{
	if ( !g_bAllowServerDemoDump ) { DevMsg( "Server demo not allowed now.\n" ); return; }
	if ( args.ArgC() != 2 ) { DevMsg( "Please specify an output filename.\n" ); return; }
	if ( !g_pServerDemoSystem ) { DevMsg( "Server demo system not initialized!\n" ); return; }
	g_bAllowServerDemoDump = false;
	g_pServerDemoSystem->WriteServerDemoFile( -1, args[1] );
	g_bAllowServerDemoDump = true;
}

// Native object 0x008226E4; callback 0x001CF5D0; command_function.
static ConCommand dump_server_demo_command( "dump_server_demo", CC_dump_server_demo, "dump_sever_demo <filename>", 0x00000004 );

#endif
