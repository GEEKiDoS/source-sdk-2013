// Transplant into src/game/server/l4d2/terror_music_dynamic.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083CB68; constructor 0x005937B9.
ConVar director_music_dynamic_mob_size( "director_music_dynamic_mob_size", "25", 0x00004004, "Spawning a mob this large can play music" );

// Native object 0x0083CBB0; constructor 0x005937E9.
ConVar director_music_dynamic_mobstop_size( "director_music_dynamic_mobstop_size", "8", 0x00004004, "When a mob gets to this size we think about stopping the music" );

// Native object 0x0083CBF8; constructor 0x00593819.
ConVar director_music_dynamic_scanmobstop_size( "director_music_dynamic_scanmobstop_size", "3", 0x00004004, "When see less than this many of a mob, music stops" );

// Native object 0x0085BCA8; constructor 0x0059DCA4.
ConVar z_show_infected( "z_show_infected", "0", 0x00004004, "" );

#endif
