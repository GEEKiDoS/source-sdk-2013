// Transplant into src/game/server/l4d2/terror_nav_world.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083B088; constructor 0x00592591.
ConVar z_checkpoint_debug( "z_checkpoint_debug", "0", 0x00000006, "" );

#endif
