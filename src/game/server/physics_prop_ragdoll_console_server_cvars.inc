// Transplant into src/game/server/physics_prop_ragdoll.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00820288; constructor 0x0058CC64.
ConVar phys_ragdoll_force_magnitude( "phys_ragdoll_force_magnitude", "0.3", 0x00002006, "" );

// Native object 0x008202D0; constructor 0x0058CCA4.
ConVar ragdoll_self_collision( "ragdoll_self_collision", "1", 0x00002006, "" );

// Native object 0x00820318; constructor 0x0058CCD4.
ConVar ragdoll_collide( "ragdoll_collide", "0", 0x00002006, "" );

// Native object 0x00820360; constructor 0x0058CD09.
ConVar ragdoll_sleepaftertime( "ragdoll_sleepaftertime", "5", 0x00002006, "After this many seconds of being basically stationary, the ragdoll will go to sleep." );

// Native object 0x00820618; constructor 0x0058CEC4.
ConVar ragdoll_slowdown( "ragdoll_slowdown", "1.0", 0x00002006, "" );

// Native object 0x00820660; constructor 0x0058CEF4.
ConVar ragdoll_slowdown_time( "ragdoll_slowdown_time", "5.0", 0x00002006, "" );

// Native object 0x008206A8; constructor 0x0058CF24.
ConVar ragdoll_door_uncollide_time( "ragdoll_door_uncollide_time", "4.0", 0x00002006, "" );

// Native object 0x008206F0; constructor 0x0058CF54.
ConVar ragdoll_force_to_sleep_time( "ragdoll_force_to_sleep_time", "10", 0x00002006, "" );

// Native object 0x00820860; constructor 0x0058D011.
ConVar sv_ragdoll_version( "sv_ragdoll_version", "1", 0x00000006, "" );

#endif
