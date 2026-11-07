// Transplant into src/game/server/l4d2/terror_gamerules_versus.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084DA88; constructor 0x00599739.
ConVar vs_incap_bonus( "vs_incap_bonus", "10", 0x00006004, "Bonus score for infected, per incap in a chapter" );

// Native object 0x00853ED8; constructor 0x0059BD31.
ConVar vs_score_pp_health( "vs_score_pp_health", "0.5", 0x00000006, "" );

// Native object 0x00853F20; constructor 0x0059BD61.
ConVar vs_score_pp_healthbuffer( "vs_score_pp_healthbuffer", "0.25", 0x00000006, "" );

// Native object 0x00853FB0; constructor 0x0059BDC4.
ConVar vs_survivor_damage_reduction( "vs_survivor_damage_reduction", "0.8", 0x00004004, "" );

#endif
