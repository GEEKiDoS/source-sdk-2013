// Transplant into src/game/server/l4d2/terror_music.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084A5A8; constructor 0x00597F11.
ConVar sv_music_debug( "sv_music_debug", "0", 0x00000006, "" );

#endif
