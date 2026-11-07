// Transplant into src/game/shared/physics_main_shared.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007FE218; constructor 0x0058AE21.
ConVar sv_debug_entity( "sv_debug_entity", "", 0x00000006, "" );

#endif
