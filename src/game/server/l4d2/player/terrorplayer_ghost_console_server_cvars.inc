// Transplant into src/game/server/l4d2/terrorplayer_ghost.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084B248; constructor 0x00598749.
ConVar z_ghost_ahead_flow( "z_ghost_ahead_flow", "500", 0x00002006, "Ghosts must be this far ahead of the survivors to materialize" );

// Native object 0x00853B30; constructor 0x0059BAC6.
ConVar z_ghost_offer_spawn_safety_time( "z_ghost_offer_spawn_safety_time", "20", 0x00000006, "Ghost offers don't go to ghosts who spawn sooner than this" );

// Native object 0x00853B78; constructor 0x0059BAF6.
ConVar z_ghost_offer_acceptance_time( "z_ghost_offer_acceptance_time", "0", 0x00000006, "Ghosts who accept offers extend their timers to this much" );

#endif
