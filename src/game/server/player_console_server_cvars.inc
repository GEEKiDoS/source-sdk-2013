// Transplant into src/game/server/player.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007D9F48; constructor 0x00583C24.
ConVar steam_controller_haptics( "steam_controller_haptics", "1", 0x00082004, "" );

// Native object 0x007FE9A0; constructor 0x0058B2C4.
ConVar sv_regeneration_wait_time( "sv_regeneration_wait_time", "1.0", 0x00002006, "" );

// Native object 0x007FED98; constructor 0x0058B584.
ConVar vis_force( "vis_force", "0", 0x00004004, "" );

#endif
