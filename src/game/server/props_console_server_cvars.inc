// Transplant into src/game/server/props.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008027E0; constructor 0x0058C011.
ConVar phys_shoot_torque( "phys_shoot_torque", "100000000", 0x00000006, "" );

// Native object 0x008028F0; constructor 0x0058C139.
ConVar sv_prop_door_max_close_attempts( "sv_prop_door_max_close_attempts", "8", 0x00004004, "Number of times blocked doors will try to close before becoming non-solid and forcing a close." );

// Native object 0x00802938; constructor 0x0058C169.
ConVar z_door_retry_interval( "z_door_retry_interval", "1", 0x00004004, "Interval at which blocked doors will try to reopen" );

// Native object 0x008029C8; constructor 0x0058C1C9.
ConVar disable_dynamic_prop_loading( "disable_dynamic_prop_loading", "0", 0x00004004, "If non-zero when a map loads, dynamic props won't be loaded" );

// Native object 0x00823CC8; constructor 0x0058DE14.
ConVar force_disablex360( "force_disablex360", "0", 0x00004004, "" );

#endif
