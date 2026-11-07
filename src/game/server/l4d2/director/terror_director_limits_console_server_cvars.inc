// Transplant into src/game/server/l4d2/terror_director_limits.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083CDF0; constructor 0x00593964.
ConVar z_chance( "z_chance", "60", 0x00004004, "" );

// Native object 0x0083CE38; constructor 0x00593994.
ConVar z_finale_chance( "z_finale_chance", "100", 0x00004004, "" );

#endif
