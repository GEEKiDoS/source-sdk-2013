// Transplant into src/game/shared/l4d2/cs_player_shared.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00837B28; constructor 0x005919C4.
ConVar vis_debug( "vis_debug", "0", 0x00004004, "" );

// Native object 0x0083ADF8; constructor 0x00592394.
ConVar z_added_light_min( "z_added_light_min", "0.025", 0x00002006, "" );

// Native object 0x0083AE40; constructor 0x005923C4.
ConVar z_added_light_max( "z_added_light_max", "0.25", 0x00002006, "" );

#endif
