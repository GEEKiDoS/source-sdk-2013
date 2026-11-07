// Transplant into src/game/server/l4d2/terrorplayer_movement.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00854868; constructor 0x0059C3C4.
ConVar survivor_max_tug_distance( "survivor_max_tug_distance", "300", 0x00004004, "" );

// Native object 0x008548B0; constructor 0x0059C3F4.
ConVar survivor_max_tug_duration( "survivor_max_tug_duration", "1.5", 0x00004004, "" );

// Native object 0x008548F8; constructor 0x0059C429.
ConVar survivor_drag_type( "survivor_drag_type", "0", 0x00004004, "0: no dragging, 1: short tug, 2: continuous drag" );

// Native object 0x00854CA0; constructor 0x0059C6C9.
ConVar z_tank_grenade_slowdown( "z_tank_grenade_slowdown", "0", 0x00004004, "The speed setting put on a Tank when hit by a grenade." );

// Native object 0x00854D78; constructor 0x0059C759.
ConVar z_tank_max_stagger_fade_duration( "z_tank_max_stagger_fade_duration", "6", 0x00004004, "Fade in time when a Tank is staggered by a grenade." );

// Native object 0x00855FC0; constructor 0x0059D3E4.
ConVar hack_elevator_z_offset( "hack_elevator_z_offset", "10", 0x00004004, "" );

#endif
