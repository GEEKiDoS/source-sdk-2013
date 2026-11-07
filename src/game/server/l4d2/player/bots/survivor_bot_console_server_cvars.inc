// Transplant into src/game/server/l4d2/survivor_bot.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00862FE8; constructor 0x005A1674.
ConVar sb_follow_stress_factor( "sb_follow_stress_factor", "0.5", 0x00004004, "" );

// Native object 0x008630C0; constructor 0x005A1701.
ConVar sb_skill( "sb_skill", "survivor", 0x00000006, "" );

// Native object 0x00863810; constructor 0x005A1C14.
ConVar sb_threat_exposure_stop( "sb_threat_exposure_stop", "200000", 0x00004004, "" );

// Native object 0x00863858; constructor 0x005A1C44.
ConVar sb_threat_exposure_walk( "sb_threat_exposure_walk", "50000", 0x00004004, "" );

#endif
