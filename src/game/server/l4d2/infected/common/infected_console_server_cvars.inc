// Transplant into src/game/server/l4d2/infected.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084C860; constructor 0x005990E4.
ConVar z_avoidteammates( "z_avoidteammates", "1", 0x00006004, "" );

// Native object 0x0085C390; constructor 0x0059E074.
ConVar z_infected_tinting( "z_infected_tinting", "1", 0x00002006, "" );

// Native object 0x0086DEA0; constructor 0x005A6DC4.
ConVar z_infected_move( "z_infected_move", "1", 0x00004004, "" );

// Native object 0x0086E098; constructor 0x005A6F19.
ConVar sv_zombie_touch_trigger_delay( "sv_zombie_touch_trigger_delay", "0.5", 0x00004004, "Time between trigger tests for infected. This is throttled for perf reasons." );

// Native object 0x0086F9D8; constructor 0x005A72D1.
ConVar z_debug_infected_server_anim( "z_debug_infected_server_anim", "0", 0x00000006, "" );

// Native object 0x0086FAB0; constructor 0x005A7364.
ConVar z_debug_neighbors( "z_debug_neighbors", "0", 0x00004004, "" );

// Native object 0x00870228; constructor 0x005A79B9.
ConVar z_experimental_blast_damage_probability( "z_experimental_blast_damage_probability", "100", 0x00004004, "Damage to zombie from explosion will use new cutouts - probability, %" );

#endif
