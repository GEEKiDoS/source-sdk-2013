// Transplant into existing src/game/server/l4d2/terror_nav.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002E0D80 (command_function).
static void CC_sv_crash( const CCommand &args )
{
	// Main decision: deliberate developer fault is observable native behavior.
	*(volatile int *)0 = 0;
}

// Native object 0x0084E240; callback 0x002E0D80; command_function.
static ConCommand sv_crash_command( "sv_crash", CC_sv_crash, "Causes a server crash for testing", 0x00004004 );

#endif
