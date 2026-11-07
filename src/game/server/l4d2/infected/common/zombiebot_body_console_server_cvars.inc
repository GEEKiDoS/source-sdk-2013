// Transplant into src/game/server/l4d2/zombiebot_body.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0086DEE8; constructor 0x005A6DF9.
ConVar z_falling_land_hard_speed( "z_falling_land_hard_speed", "200", 0x00004004, "Minimum falling speed to cause a 'hard' landing animation to occur" );

// Native object 0x0086DF30; constructor 0x005A6E29.
ConVar z_falling_land_speed( "z_falling_land_speed", "200", 0x00004004, "Minimum falling speed to cause a landing animation to occur" );

// Native object 0x0086DF78; constructor 0x005A6E59.
ConVar z_falling_land_forward_speed( "z_falling_land_forward_speed", "200", 0x00004004, "Minimum falling speed to cause a stumbling forward landing animation to occur" );

// Native object 0x0086E008; constructor 0x005A6EB9.
ConVar z_vocalize_burn_min_interval( "z_vocalize_burn_min_interval", "2", 0x00004004, "The minimum time between vocalizing being burned" );

// Native object 0x0086E050; constructor 0x005A6EE9.
ConVar z_vocalize_burn_max_interval( "z_vocalize_burn_max_interval", "3", 0x00004004, "The maximum time between vocalizing being burned" );

#endif
