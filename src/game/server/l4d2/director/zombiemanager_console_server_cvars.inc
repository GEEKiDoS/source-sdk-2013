// Transplant into src/game/server/l4d2/zombiemanager.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083B968; constructor 0x00592BB9.
ConVar cleared_wanderer_respawn_chance( "cleared_wanderer_respawn_chance", "0", 0x00004004, "Chance to replenish wanderers in cleared nav areas" );

// Native object 0x0085DC58; constructor 0x0059ED64.
ConVar z_density_region_length( "z_density_region_length", "1500", 0x00004004, "" );

// Native object 0x0085DD30; constructor 0x0059EDF9.
ConVar z_safe_spawn_range( "z_safe_spawn_range", "250", 0x00004004, "Minimum range for spawning special zombies" );

// Native object 0x0085DE50; constructor 0x0059EEB4.
ConVar z_scout_mob_spawn_range( "z_scout_mob_spawn_range", "3000", 0x00004004, "" );

// Native object 0x0085E048; constructor 0x0059F009.
ConVar z_special_spawn_interval( "z_special_spawn_interval", "45", 0x00004004, "Interval for spawning special zombies" );

// Native object 0x0085E0D8; constructor 0x0059F064.
ConVar z_mega_mob_spawn_min_interval( "z_mega_mob_spawn_min_interval", "420", 0x00004004, "" );

// Native object 0x0085E120; constructor 0x0059F094.
ConVar z_mega_mob_spawn_max_interval( "z_mega_mob_spawn_max_interval", "900", 0x00004004, "" );

// Native object 0x0085E168; constructor 0x0059F0C4.
ConVar z_mob_spawn_finale_size( "z_mob_spawn_finale_size", "20", 0x00004004, "" );

// Native object 0x0085E1B0; constructor 0x0059F0F4.
ConVar z_skirmish_spawn_min_interval( "z_skirmish_spawn_min_interval", "45", 0x00004004, "" );

// Native object 0x0085E1F8; constructor 0x0059F124.
ConVar z_skirmish_spawn_max_interval( "z_skirmish_spawn_max_interval", "90", 0x00004004, "" );

// Native object 0x0085E240; constructor 0x0059F154.
ConVar z_skirmish_spawn_min_size( "z_skirmish_spawn_min_size", "1", 0x00004004, "" );

// Native object 0x0085E288; constructor 0x0059F184.
ConVar z_skirmish_spawn_max_size( "z_skirmish_spawn_max_size", "4", 0x00004004, "" );

// Native object 0x0086FBD0; constructor 0x005A7424.
ConVar z_reserved_wanderers( "z_reserved_wanderers", "0", 0x00004004, "" );

#endif
