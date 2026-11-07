// Transplant into src/game/server/l4d2/witch.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083D300; constructor 0x00593CC4.
ConVar z_witch_damage( "z_witch_damage", "100", 0x00004004, "" );

// Native object 0x0083D348; constructor 0x00593CF4.
ConVar z_witch_min_threat_time( "z_witch_min_threat_time", "5", 0x00004004, "" );

// Native object 0x0083D390; constructor 0x00593D24.
ConVar z_witch_max_threat_time( "z_witch_max_threat_time", "7", 0x00004004, "" );

// Native object 0x0083D3D8; constructor 0x00593D54.
ConVar z_witch_berserk_range( "z_witch_berserk_range", "200", 0x00004004, "" );

// Native object 0x00871178; constructor 0x005A8224.
ConVar z_witch_discard_range( "z_witch_discard_range", "2000", 0x00004004, "" );

// Native object 0x008713B8; constructor 0x005A83A6.
ConVar z_witch_tongue_range( "z_witch_tongue_range", "100", 0x00000006, "If a tongue passes this close to a witch's head, she will snap it" );

#endif
