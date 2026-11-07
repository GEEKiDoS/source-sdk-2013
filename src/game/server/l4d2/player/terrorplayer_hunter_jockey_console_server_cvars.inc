// Transplant into src/game/server/l4d2/terrorplayer_hunter_jockey.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008538F0; constructor 0x0059B944.
ConVar z_player_zombie_land_delay( "z_player_zombie_land_delay", "0.5", 0x00004004, "" );

#endif
