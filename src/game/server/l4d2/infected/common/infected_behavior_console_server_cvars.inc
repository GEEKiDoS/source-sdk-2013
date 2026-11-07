// Transplant into src/game/server/l4d2/infected_behavior.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008707E0; constructor 0x005A7CF4.
ConVar nb_chase_lead_time( "nb_chase_lead_time", "2.0", 0x00004004, "" );

// Native object 0x00870828; constructor 0x005A7D24.
ConVar z_max_path_length( "z_max_path_length", "5000", 0x00004004, "" );

// Native object 0x008709D8; constructor 0x005A7E49.
ConVar z_pushaway_force( "z_pushaway_force", "100", 0x00004004, "How hard the infected shove non-IT Survivors out of their way to reach the IT victim" );

// Native object 0x00870A20; constructor 0x005A7E71.
ConVar z_debug_cull( "z_debug_cull", "", 0x00004004, "" );

// Native object 0x00870A68; constructor 0x005A7E9E.
ConVar z_debug_stuck( "z_debug_stuck", "", 0x00000006, "" );

// Native object 0x00870E30; constructor 0x005A8104.
ConVar z_alert_range( "z_alert_range", "1000", 0x00004004, "" );

#endif
