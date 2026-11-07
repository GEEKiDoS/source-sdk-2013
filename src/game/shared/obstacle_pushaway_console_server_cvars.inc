// Transplant into src/game/shared/obstacle_pushaway.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007FC508; constructor 0x00589F39.
ConVar sv_pushaway_min_force( "sv_pushaway_min_force", "500", 0x00002006, "Minimum amount of force applied to physics objects by players." );

#endif
