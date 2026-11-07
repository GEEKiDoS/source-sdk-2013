// Transplant into src/game/server/l4d2/terrorplayer_death.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00855AF8; constructor 0x0059D069.
ConVar z_exploding_splat( "z_exploding_splat", "50", 0x00004004, "Boomers splat instead of making a thud when taking this much falling damage" );

#endif
