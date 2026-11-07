// Transplant into src/game/server/l4d2/survivor_intention.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008643D0; constructor 0x005A1E19.
ConVar sb_threat_close_range( "sb_threat_close_range", "150", 0x00004004, "Very close range for threats" );

// Native object 0x00864940; constructor 0x005A2224.
ConVar sb_threat_very_close_range( "sb_threat_very_close_range", "150", 0x00004004, "" );

#endif
