// Transplant into src/game/shared/util_shared.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0082EFC0; constructor 0x0058FCC4.
ConVar z_debug_shared_random( "z_debug_shared_random", "0", 0x00002006, "" );

#endif
