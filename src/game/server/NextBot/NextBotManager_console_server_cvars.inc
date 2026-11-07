// Transplant into src/game/server/NextBot/NextBotManager.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00876B88; constructor 0x005ABCD9.
ConVar nb_stuck_dump_filter_entindex( "nb_stuck_dump_filter_entindex", "0", 0x00004004, "If nonzero, only display stuck data for entities with the given entindex" );

// Native object 0x00876BD0; constructor 0x005ABD09.
ConVar nb_stuck_dump_filter_time_start( "nb_stuck_dump_filter_time_start", "0", 0x00004004, "Do not display stuck data with a timestamp less than this value" );

// Native object 0x00876C18; constructor 0x005ABD39.
ConVar nb_stuck_dump_filter_time_end( "nb_stuck_dump_filter_time_end", "99999", 0x00004004, "Do not display stuck data with a timestamp greater than this value" );

// Native object 0x00876C60; constructor 0x005ABD69.
ConVar nb_stuck_dump_filter_identifier( "nb_stuck_dump_filter_identifier", "", 0x00004004, "Only display stuck data for entities whose identifier matches this substring" );

// Native object 0x00876CA8; constructor 0x005ABD99.
ConVar nb_stuck_dump_filter_min_duration( "nb_stuck_dump_filter_min_duration", "0", 0x00004004, "Only display stuck data for entities who were stuck longer than this duration" );

// Native object 0x00876CF0; constructor 0x005ABDC9.
ConVar nb_stuck_dump_show_details( "nb_stuck_dump_show_details", "0", 0x00004004, "If nonzero, include more data in stuck display" );

#endif
