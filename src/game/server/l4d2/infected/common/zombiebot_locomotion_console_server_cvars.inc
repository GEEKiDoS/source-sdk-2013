// Transplant into src/game/server/l4d2/zombiebot_locomotion.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0086E448; constructor 0x005A70C4.
ConVar nb_lean_forward_accel( "nb_lean_forward_accel", "100", 0x00004004, "" );

// Native object 0x0086E520; constructor 0x005A7156.
ConVar nb_avoid( "nb_avoid", "2", 0x00000006, "0=no avoidance, 1=avoid nearest, 2=avoid all" );

// Native object 0x0086E568; constructor 0x005A7186.
ConVar nb_avoid_interval( "nb_avoid_interval", "0.5", 0x00000006, "Interval to update nearest for avoidance" );

// Native object 0x0086E5B0; constructor 0x005A71B4.
ConVar z_debug_climb( "z_debug_climb", "0", 0x00004004, "" );

#endif
