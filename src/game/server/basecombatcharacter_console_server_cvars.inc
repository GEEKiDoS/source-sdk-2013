// Transplant into src/game/server/basecombatcharacter.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007D60D8; constructor 0x005829D9.
ConVar z_last_area_update_tolerance( "z_last_area_update_tolerance", "4.0", 0x00004004, "Distance a character needs to travel in order to invalidate cached area" );

#endif
