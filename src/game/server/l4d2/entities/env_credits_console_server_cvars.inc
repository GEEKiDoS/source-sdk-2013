// Transplant into src/game/server/l4d2/env_credits.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083B148; constructor 0x005925F4.
ConVar z_credits_interval( "z_credits_interval", "0", 0x00004004, "" );

#endif
