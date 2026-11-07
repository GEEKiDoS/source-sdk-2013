// Transplant into src/game/shared/l4d2/terror_gamemovement.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084C8F0; constructor 0x00599144.
ConVar z_ghost_friction( "z_ghost_friction", "1.5", 0x00002006, "" );

#endif
