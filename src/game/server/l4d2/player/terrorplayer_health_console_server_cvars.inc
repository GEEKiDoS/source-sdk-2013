// Transplant into src/game/server/l4d2/terrorplayer_health.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00852E40; constructor 0x0059B214.
ConVar survivor_ledge_scales_health( "survivor_ledge_scales_health", "1", 0x00004004, "" );

#endif
