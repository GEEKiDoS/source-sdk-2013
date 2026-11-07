// Transplant into src/game/server/l4d2/terrorplayer_speech.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0085BB80; constructor 0x0059DBF6.
ConVar rr_debugplayer( "rr_debugplayer", "-1", 0x00000006, "Player entindex for which to print speech attempts (0 for all)" );

#endif
