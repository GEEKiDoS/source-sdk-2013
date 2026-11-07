// Transplant into src/game/server/baseanimating.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007D59A0; constructor 0x00582874.
ConVar sv_showonlyhitbox( "sv_showonlyhitbox", "-1", 0x00004004, "" );

#endif
