// Transplant into src/game/shared/gamemovement.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007F7D18; constructor 0x00588449.
ConVar ladder_console_controls( "ladder_console_controls", "0", 0x00002006, "Use absolute (up is always up and down always down) ladder controls." );

// Native object 0x007F7D60; constructor 0x00588479.
ConVar ladder_autocenter( "ladder_autocenter", "1", 0x00002006, "Auto-center players on ladders." );

// Native object 0x007F7E48; constructor 0x005884D4.
ConVar ladder_horizon( "ladder_horizon", "-0.3", 0x00002006, "" );

// Native object 0x007F7E90; constructor 0x00588504.
ConVar sv_player_stuck_tolerance( "sv_player_stuck_tolerance", "10", 0x00004004, "" );

#endif
