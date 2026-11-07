// Transplant into src/game/server/l4d2/jockey_actions.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00855798; constructor 0x0059CE19.
ConVar z_jockey_leap_again_timer( "z_jockey_leap_again_timer", "5.0", 0x00004004, "How long after a dismount before the jockey can leap again." );

#endif
