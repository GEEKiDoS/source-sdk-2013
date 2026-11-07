// Transplant into src/game/shared/l4d2/cs_gamerules.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00836C10; constructor 0x005911AD.
ConVar mp_buytime( "mp_buytime", "1.5", 0x00002006, "How many minutes after round start players can buy items for.", true, 0.25f, false, 0.0f );

// Native object 0x00836D30; constructor 0x005912B1.
ConVar mp_startmoney( "mp_startmoney", "800", 0x00002006, "amount of money each player gets when they reset", true, 800.0f, true, 16000.0f );

// Native object 0x00836E08; constructor 0x00591381.
ConVar mp_c4timer( "mp_c4timer", "45", 0x00002106, "how long from when the C4 is armed until it blows", true, 10.0f, true, 90.0f );

// Native object 0x00836E50; constructor 0x005913CD.
ConVar mp_limitteams( "mp_limitteams", "2", 0x00002106, "Max # of players 1 team can have over another (0 disables check)", true, 0.0f, true, 30.0f );

// Native object 0x00836E98; constructor 0x005913F9.
ConVar mp_tkpunish( "mp_tkpunish", "0", 0x00002006, "Will a TK'er be punished in the next round?  {0=no,  1=yes}" );

// Native object 0x00836F28; constructor 0x00591459.
ConVar mp_spawnprotectiontime( "mp_spawnprotectiontime", "5", 0x00002006, "Kick players who team-kill within this many seconds of a round restart." );

// Native object 0x00836F70; constructor 0x00591489.
ConVar mp_humanteam( "mp_humanteam", "any", 0x00002006, "Restricts human players to a single team {any, CT, T}" );

// Native object 0x00836FB8; constructor 0x005914E4.
ConVar radius_damage_show_hits( "radius_damage_show_hits", "0", 0x00006004, "" );

#endif
