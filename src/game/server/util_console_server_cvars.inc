// Transplant into src/game/server/util.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00828678; constructor 0x0058FA04.
ConVar sv_sync_anims_spawn( "sv_sync_anims_spawn", "0", 0x00004004, "" );

#endif
