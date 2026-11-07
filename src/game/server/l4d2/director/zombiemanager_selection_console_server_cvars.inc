// Transplant into src/game/server/l4d2/zombiemanager_selection.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00852A50; constructor 0x0059AF74.
ConVar z_debug_fog( "z_debug_fog", "0", 0x00004004, "" );

// Native object 0x00853308; constructor 0x0059B554.
ConVar z_minion_aim_tolerance( "z_minion_aim_tolerance", "0.05", 0x00004004, "" );

// Native object 0x00853350; constructor 0x0059B584.
ConVar z_minion_limit( "z_minion_limit", "3", 0x00004004, "" );

// Native object 0x00853E00; constructor 0x0059BCA4.
ConVar z_debug_spawn_ahead( "z_debug_spawn_ahead", "0", 0x00004004, "" );

// Native object 0x0085DE98; constructor 0x0059EEE4.
ConVar z_mob_sacrifice_timeout( "z_mob_sacrifice_timeout", "5", 0x00004004, "" );

// Native object 0x0085DEE0; constructor 0x0059EF14.
ConVar z_cooldown_spawn_safety_range( "z_cooldown_spawn_safety_range", "1000", 0x00004004, "" );

#endif
