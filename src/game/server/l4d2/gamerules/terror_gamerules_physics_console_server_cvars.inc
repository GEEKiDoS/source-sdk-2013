// Transplant into src/game/server/l4d2/terror_gamerules_physics.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007DE3E0; constructor 0x00585009.
ConVar sv_doors_push_players( "sv_doors_push_players", "0", 0x00004004, "If true, a door will push players out of the way as it opens and closes." );

// Native object 0x0084DE08; constructor 0x00599A34.
ConVar z_tanks_block_molotovs( "z_tanks_block_molotovs", "1", 0x00004004, "" );

#endif
