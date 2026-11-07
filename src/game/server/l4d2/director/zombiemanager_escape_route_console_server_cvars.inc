// Transplant into src/game/server/l4d2/zombiemanager_escape_route.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0085E3A8; constructor 0x0059F244.
ConVar director_threat_radius( "director_threat_radius", "1000", 0x00004004, "" );

// Native object 0x0085E480; constructor 0x0059F2D9.
ConVar vs_threat_radius( "vs_threat_radius", "200", 0x00004004, "Distance from the escape route for placing threats" );

#endif
