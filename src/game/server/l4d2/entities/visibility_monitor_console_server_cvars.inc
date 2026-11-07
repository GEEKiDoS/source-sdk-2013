// Transplant into src/game/server/l4d2/visibility_monitor.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007DE0D8; constructor 0x00584EE4.
ConVar debug_visibility_monitor( "debug_visibility_monitor", "0", 0x00004004, "" );

#endif
