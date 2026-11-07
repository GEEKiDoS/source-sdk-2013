// Transplant into src/game/server/l4d2/terror_gamerules_damage.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084E0D8; constructor 0x00599C59.
ConVar ammo_turret_survivor_damage( "ammo_turret_survivor_damage", "1", 0x00002006, "Turret damage per shot against survivors (turret fires at 10Hz)" );

// Native object 0x0084E120; constructor 0x00599C89.
ConVar ammo_turret_pz_damage( "ammo_turret_pz_damage", "8", 0x00002006, "Turret damage per shot against player zombies (turret fires at 10Hz)" );

// Native object 0x0084E168; constructor 0x00599CB9.
ConVar ammo_turret_tank_damage( "ammo_turret_tank_damage", "40", 0x00002006, "Turret damage per shot against tanks (turret fires at 10Hz)" );

// Native object 0x0084E1B0; constructor 0x00599CE9.
ConVar ammo_turret_witch_damage( "ammo_turret_witch_damage", "16", 0x00002006, "Turret damage per shot against witches (turret fires at 10Hz)" );

// Native object 0x0084E1F8; constructor 0x00599D19.
ConVar ammo_turret_infected_damage( "ammo_turret_infected_damage", "15", 0x00002006, "Turret damage per shot against the horde (turret fires at 10Hz)" );

#endif
