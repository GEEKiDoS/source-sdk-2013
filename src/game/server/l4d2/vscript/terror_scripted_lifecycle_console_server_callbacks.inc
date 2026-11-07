// Transplant into existing src/game/server/l4d2/terror_scripted_lifecycle.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x0027AAC0 (command_function).
static void CC_scripted_mode_activate( const CCommand &args )
{
	TheDirector->GetScriptedMode()->Activate();
}

// Native object 0x0083DCD8; callback 0x0027AAC0; command_function.
static ConCommand scripted_mode_activate_command( "scripted_mode_activate", CC_scripted_mode_activate, "Activate scripted mode.", 0x00004006 );

// Native callback 0x0027AAD0 (command_function).
static void CC_scripted_mode_shutdown( const CCommand &args )
{
	TheDirector->GetScriptedMode()->Shutdown( 0, "" );
}

// Native object 0x0083DCFC; callback 0x0027AAD0; command_function.
static ConCommand scripted_mode_shutdown_command( "scripted_mode_shutdown", CC_scripted_mode_shutdown, "Shutdown scripted mode.", 0x00004006 );

#endif
