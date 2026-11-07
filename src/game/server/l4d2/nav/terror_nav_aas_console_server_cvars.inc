// Transplant into src/game/server/l4d2/terror_nav_aas.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00851560; constructor 0x00599E44.
ConVar z_show_traffic( "z_show_traffic", "0", 0x00004004, "" );

// Native object 0x008515A8; constructor 0x00599E74.
ConVar z_show_flow_delta( "z_show_flow_delta", "0", 0x00004004, "" );

// Native object 0x008515F0; constructor 0x00599EA4.
ConVar z_show_bottlenecks( "z_show_bottlenecks", "0", 0x00004004, "" );

// Native object 0x00851638; constructor 0x00599ED4.
ConVar z_show_damaging( "z_show_damaging", "0", 0x00004004, "" );

// Native object 0x008516C8; constructor 0x00599F34.
ConVar z_show_population_density( "z_show_population_density", "0", 0x00004004, "" );

// Native object 0x00851710; constructor 0x00599F64.
ConVar z_show_potentially_visible( "z_show_potentially_visible", "0", 0x00004004, "" );

// Native object 0x00851758; constructor 0x00599F94.
ConVar z_show_mutually_visible_set( "z_show_mutually_visible_set", "0", 0x00004004, "" );

// Native object 0x008517A0; constructor 0x00599FC4.
ConVar z_show_potentially_visible_to_survivor_team( "z_show_potentially_visible_to_survivor_team", "0", 0x00004004, "" );

// Native object 0x008517E8; constructor 0x00599FF4.
ConVar z_show_completely_visible_to_survivor_team( "z_show_completely_visible_to_survivor_team", "0", 0x00004004, "" );

// Native object 0x00851830; constructor 0x0059A024.
ConVar z_clear_range( "z_clear_range", "750", 0x00004004, "" );

// Native object 0x00851878; constructor 0x0059A054.
ConVar z_clear_min_time( "z_clear_min_time", "0.2", 0x00004004, "" );

// Native object 0x008518C0; constructor 0x0059A084.
ConVar z_clear_min_time_range( "z_clear_min_time_range", "300", 0x00004004, "" );

// Native object 0x00851908; constructor 0x0059A0B4.
ConVar z_clear_max_time( "z_clear_max_time", "5", 0x00004004, "" );

// Native object 0x008532C0; constructor 0x0059B524.
ConVar z_clear_area_range( "z_clear_area_range", "300", 0x00004004, "" );

// Native object 0x0085DCE8; constructor 0x0059EDC4.
ConVar z_show_flow_distance( "z_show_flow_distance", "0", 0x00004004, "" );

#endif
