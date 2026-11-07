// Transplant into src/game/server/l4d2/terrorplayer_damage.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007F7490; constructor 0x00587E44.
ConVar tauntfrequency( "tauntfrequency", "1", 0x00000106, "" );

// Native object 0x00852C90; constructor 0x0059B0F1.
ConVar z_damage_screen_fade_alpha( "z_damage_screen_fade_alpha", "30", 0x00000006, "" );

// Native object 0x00852CD8; constructor 0x0059B121.
ConVar z_damage_screen_fade_duration( "z_damage_screen_fade_duration", "0.5", 0x00000006, "" );

// Native object 0x008541A8; constructor 0x0059BF11.
ConVar z_max_survivor_damage( "z_max_survivor_damage", "300", 0x00000006, "" );

// Native object 0x00854238; constructor 0x0059BF74.
ConVar z_special_burn_dmg_scale( "z_special_burn_dmg_scale", "3", 0x00004004, "" );

// Native object 0x008542C8; constructor 0x0059BFE9.
ConVar incendiary_ammo_burn_time( "incendiary_ammo_burn_time", "4.0", 0x00004004, "Time to burn when hit by incendiary ammo" );

// Native object 0x00854988; constructor 0x0059C489.
ConVar z_burning_lifetime( "z_burning_lifetime", "30", 0x00004004, "Number of seconds a burning zombie takes to crisp" );

// Native object 0x00855090; constructor 0x0059C964.
ConVar z_deafen_radius_one( "z_deafen_radius_one", "100", 0x00004004, "" );

// Native object 0x008550D8; constructor 0x0059C994.
ConVar z_deafen_radius_two( "z_deafen_radius_two", "150", 0x00004004, "" );

// Native object 0x00855120; constructor 0x0059C9C4.
ConVar z_deafen_radius_three( "z_deafen_radius_three", "200", 0x00004004, "" );

// Native object 0x00856098; constructor 0x0059D48D.
ConVar spit_scaling_min_scale_00856098( "spit_scaling_min_scale", "0.5", 0x00080004, "minimum percent of maximum common dps required to start scaling down spit damage", true, 0.10000000149011612f, true, 1.0f );

// Native object 0x008560E0; constructor 0x0059D4CD.
ConVar spit_scaling_min_scale_008560E0( "spit_scaling_min_scale", "0.35", 0x00080004, "minimum scale to apply to spit damage when common dps is near max", true, 0.10000000149011612f, true, 1.0f );

#endif
