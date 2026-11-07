// Transplant into src/game/server/l4d2/terrorplayer.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00852C00; constructor 0x0059B099.
ConVar z_respawn_interval( "z_respawn_interval", "10", 0x00004004, "Infected can try a respawn within this many seconds of spawning" );

// Native object 0x00852C48; constructor 0x0059B0C9.
ConVar z_respawn_distance( "z_respawn_distance", "100", 0x00004004, "Infected can try a respawn within this far of their original spawn" );

// Native object 0x008533E0; constructor 0x0059B5E4.
ConVar z_vomit( "z_vomit", "1", 0x00004004, "" );

// Native object 0x00853428; constructor 0x0059B619.
ConVar z_noise_level_footstep( "z_noise_level_footstep", "135", 0x00004004, "How much noise we make with a footstep." );

#endif
