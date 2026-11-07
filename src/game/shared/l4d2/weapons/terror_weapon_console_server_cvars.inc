// Transplant into src/game/shared/l4d2/terror_weapon.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008668C0; constructor 0x005A3254.
ConVar prop_use( "prop_use", "0", 0x00002006, "" );

// Native object 0x0086D778; constructor 0x005A68AD.
ConVar survivor_upgrade_reload_duration( "survivor_upgrade_reload_duration", "0.5", 0x00002006, "Upgraded survivors reload in this fraction of the base time.", true, 0.10000000149011612f, true, 1.0f );

#endif
