// Transplant into src/game/shared/l4d2/terrorplayer_shared.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083CC88; constructor 0x00593874.
ConVar survivor_speed( "survivor_speed", "210", 0x00004004, "" );

// Native object 0x0084AAF8; constructor 0x00598244.
ConVar survivor_unstoppable_speed( "survivor_unstoppable_speed", "150", 0x00006004, "" );

// Native object 0x0084AB40; constructor 0x00598279.
ConVar survivor_speed_boost_factor( "survivor_speed_boost_factor", "1.2", 0x00006004, "Speed boost from upgrade" );

// Native object 0x0084AF78; constructor 0x00598564.
ConVar z_hunter_speed( "z_hunter_speed", "300", 0x00006004, "" );

// Native object 0x0084B098; constructor 0x00598624.
ConVar z_jockey_blend_rate( "z_jockey_blend_rate", "1.0", 0x00004004, "" );

// Native object 0x0084C8A8; constructor 0x00599114.
ConVar z_avoidforce( "z_avoidforce", "2500", 0x00006004, "" );

// Native object 0x0084CAA0; constructor 0x00599264.
ConVar z_spawn_speed( "z_spawn_speed", "450", 0x00006004, "" );

// Native object 0x0084CC98; constructor 0x005993B4.
ConVar z_puking_eye_height( "z_puking_eye_height", "44", 0x00006004, "" );

// Native object 0x0084CE00; constructor 0x005994A4.
ConVar survivor_sprint_multiplier( "survivor_sprint_multiplier", "1.5", 0x00006004, "" );

#endif
