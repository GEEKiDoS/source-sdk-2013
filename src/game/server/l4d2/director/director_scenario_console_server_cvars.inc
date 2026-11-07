// Transplant into src/game/server/l4d2/director_scenario.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083E6B0; constructor 0x00594B94.
ConVar director_clear_timeout( "director_clear_timeout", "10.0", 0x00004004, "" );

#endif
