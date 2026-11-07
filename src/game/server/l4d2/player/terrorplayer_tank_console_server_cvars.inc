// Transplant into src/game/server/l4d2/terrorplayer_tank.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00854C58; constructor 0x0059C699.
ConVar z_tank_grenade_damage( "z_tank_grenade_damage", "750", 0x00004004, "How much damage a Tank takes from a grenade." );

// Native object 0x00855000; constructor 0x0059C901.
ConVar z_tank_footstep_shake_interval( "z_tank_footstep_shake_interval", "0.4", 0x00000006, "" );

#endif
