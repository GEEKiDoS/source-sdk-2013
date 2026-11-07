// Transplant into src/game/server/l4d2/terrorplayer_inventory.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008493C8; constructor 0x00597894.
ConVar g_debug_physcannon( "g_debug_physcannon", "0", 0x00006004, "" );

// Native object 0x00849410; constructor 0x005978C4.
ConVar z_carry_max_mass( "z_carry_max_mass", "250", 0x00006004, "" );

#endif
