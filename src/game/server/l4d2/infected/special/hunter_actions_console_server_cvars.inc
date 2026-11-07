// Transplant into src/game/server/l4d2/hunter_actions.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084CE90; constructor 0x00599516.
ConVar z_hunter_lunge_pitch( "z_hunter_lunge_pitch", "25", 0x00000006, "Extra pitch bot hunters will lunge with at their max range (goes to 0 when bots are lunging up close)" );

// Native object 0x00852780; constructor 0x0059AD94.
ConVar z_hunter_lunge_stagger_time( "z_hunter_lunge_stagger_time", "1", 0x00004004, "" );

#endif
