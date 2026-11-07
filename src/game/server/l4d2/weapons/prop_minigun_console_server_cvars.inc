// Transplant into src/game/server/l4d2/prop_minigun.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084DE98; constructor 0x00599AA4.
ConVar z_splat_survivor_pitch_min( "z_splat_survivor_pitch_min", "15", 0x00004004, "" );

// Native object 0x0084DEE0; constructor 0x00599AD4.
ConVar z_splat_survivor_pitch_max( "z_splat_survivor_pitch_max", "25", 0x00004004, "" );

// Native object 0x0084DF28; constructor 0x00599B04.
ConVar z_splat_survivor_yaw_min( "z_splat_survivor_yaw_min", "-10", 0x00004004, "" );

// Native object 0x0084DF70; constructor 0x00599B34.
ConVar z_splat_survivor_yaw_max( "z_splat_survivor_yaw_max", "10", 0x00004004, "" );

// Native object 0x0084DFB8; constructor 0x00599B64.
ConVar z_splat_survivor_shake_amplitude( "z_splat_survivor_shake_amplitude", "50", 0x00004004, "" );

// Native object 0x0084E000; constructor 0x00599B94.
ConVar z_splat_survivor_shake_frequency( "z_splat_survivor_shake_frequency", "150", 0x00004004, "" );

// Native object 0x0084E048; constructor 0x00599BC4.
ConVar z_splat_survivor_shake_duration( "z_splat_survivor_shake_duration", "1", 0x00004004, "" );

// Native object 0x0084E090; constructor 0x00599BF4.
ConVar z_splat_survivor_shake_radius( "z_splat_survivor_shake_radius", "10", 0x00004004, "" );

#endif
