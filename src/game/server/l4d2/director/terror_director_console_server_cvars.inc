// Transplant into src/game/server/l4d2/terror_director.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083D6A8; constructor 0x00593F39.
ConVar z_close_target_notice_distance( "z_close_target_notice_distance", "60", 0x00004004, "How far an attacking zombie will look for a nearby target on their way to their chosen victim." );

#endif
