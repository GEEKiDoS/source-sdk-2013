// Transplant into src/game/server/nav_file.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00874DB8; constructor 0x005AA3C6.
ConVar nav_save_history( "nav_save_history", "10", 0x00000006, "Keep this many backup copies of nav files." );

#endif
