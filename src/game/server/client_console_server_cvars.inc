// Transplant into src/game/server/client.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007DC370; constructor 0x005847F4.
ConVar noclip_fixup( "noclip_fixup", "1", 0x00004004, "" );

#endif
