// Transplant into src/game/server/l4d2/director_frame.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083B218; constructor 0x005926D9.
ConVar director_test_loop( "director_test_loop", "0", 0x00004004, "Allow a team of nothing but bots, advance through maps and loop back to start" );

// Native object 0x0083B260; constructor 0x00592709.
ConVar director_test_loop_rotate_maps( "director_test_loop_rotate_maps", "1", 0x00004004, "When looping, advance to the next map when reach end of campaign" );

// Native object 0x0083B2A8; constructor 0x00592739.
ConVar director_test_loop_restarts_before_rotate( "director_test_loop_restarts_before_rotate", "4", 0x00004004, "When looping and rotating, how many times to restart before moving to next campaign" );

// Native object 0x0083B2F0; constructor 0x00592769.
ConVar director_test_loop_time( "director_test_loop_time", "3", 0x00004004, "When looping, stay in each map for at most the amount of time, in minutes, specified" );

// Native object 0x0083B380; constructor 0x005927C9.
ConVar z_ghost_checkpoint_spawn_interval( "z_ghost_checkpoint_spawn_interval", "30", 0x00004004, "Interval for spawning special zombies while survivors are in the checkpoint" );

// Native object 0x0083B530; constructor 0x005928E9.
ConVar director_ready_radius( "director_ready_radius", "300", 0x00004004, "The distance survivors can move from the starting weapons during the ready period" );

// Native object 0x0083B5C0; constructor 0x00592949.
ConVar director_no_mobs( "director_no_mobs", "0", 0x00004004, "Disable mob rushes" );

// Native object 0x0083B6E0; constructor 0x00592A09.
ConVar director_no_survivor_bots( "director_no_survivor_bots", "0", 0x00004004, "Disable filling out the survivor team with bots" );

// Native object 0x0083B9F8; constructor 0x00592C14.
ConVar z_round_start_replacement_time( "z_round_start_replacement_time", "30", 0x00004004, "" );

// Native object 0x0083BB18; constructor 0x00592CD9.
ConVar director_tank_force_offer( "director_tank_force_offer", "0", 0x00004004, "Forces the director to offer a tank" );

// Native object 0x0083BD10; constructor 0x00592E29.
ConVar rescue_distance( "rescue_distance", "4500", 0x00004004, "Living survivors must travel this far past a dead survivor's corpse to be able to rescue him" );

// Native object 0x0083BDE8; constructor 0x00592EB9.
ConVar rescue_spawn_flow( "rescue_spawn_flow", "400", 0x00004004, "info_survivor_rescue ents will be considered if they are no more than this far ahead in flow distance from the leading survivor" );

// Native object 0x0083BEC0; constructor 0x00592F44.
ConVar director_ingress_range( "director_ingress_range", "3000", 0x00004004, "" );

// Native object 0x0083BF98; constructor 0x00592FD9.
ConVar director_special_finale_offer_length( "director_special_finale_offer_length", "10", 0x00004004, "How long is given to a player to accept the offer of a special zombie during the finale." );

// Native object 0x0083D6F0; constructor 0x00593F69.
ConVar director_short_finale( "director_short_finale", "0", 0x00004004, "Short finale for testing escape vehicles" );

// Native object 0x0083D930; constructor 0x005941B4.
ConVar director_ai_tanks( "director_ai_tanks", "0", 0x00004004, "" );

// Native object 0x0083D9A8; constructor 0x00594221.
ConVar director_report_screen_delay( "director_report_screen_delay", "10", 0x00000006, "" );

#endif
