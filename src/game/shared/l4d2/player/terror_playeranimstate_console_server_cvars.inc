// Transplant into src/game/shared/l4d2/terror_playeranimstate.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )



// Native object 0x0085AEA8; constructor 0x0059D9C4.
ConVar survivor_lean( "survivor_lean", "0", 0x00002006, "" );

// Native object 0x0085AF80; constructor 0x0059DA59.
ConVar z_anim_idle_speed_min( "z_anim_idle_speed_min", "20", 0x00002006, "Players moving slower than this use an ACT_IDLE variant" );

// Native object 0x0085AFC8; constructor 0x0059DA89.
ConVar z_anim_idle_speed_max( "z_anim_idle_speed_max", "85", 0x00002006, "Players moving slower than this and not pressing any movement buttons use an ACT_IDLE variant" );

#endif
