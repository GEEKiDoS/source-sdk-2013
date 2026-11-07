// Transplant into src/game/server/l4d2/cs_player.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008377F0; constructor 0x00591629.
ConVar sv_cycle_latch_timer( "sv_cycle_latch_timer", "0.2", 0x00004004, "How often to transmit our cycle to the client for corrections." );

// Native object 0x00837880; constructor 0x00591684.
ConVar bot_freeze( "bot_freeze", "0", 0x00004004, "" );

// Native object 0x00837910; constructor 0x005916E4.
ConVar bot_mimic_yaw_offset( "bot_mimic_yaw_offset", "180", 0x00004004, "" );







// Native object 0x00839A98; constructor 0x00591AF9.
ConVar z_tracer_spacing( "z_tracer_spacing", "1", 0x00006004, "One in how many bullets is a tracer in automatic weapons.", true, 1.0f, false, 0.0f );

// Native object 0x00839AE0; constructor 0x00591B29.
ConVar z_do_tracers( "z_do_tracers", "1", 0x00006004, "Do we want tracers at all?" );

// Native object 0x00839B28; constructor 0x00591B54.
ConVar survivor_ff_avoidance( "survivor_ff_avoidance", "0", 0x00004004, "" );

// Native object 0x00839B70; constructor 0x00591B84.
ConVar survivor_ff_avoidance_yaw( "survivor_ff_avoidance_yaw", "10", 0x00004004, "" );

// Native object 0x00839BB8; constructor 0x00591BB4.
ConVar survivor_ff_avoidance_pitch( "survivor_ff_avoidance_pitch", "20", 0x00004004, "" );

#endif
