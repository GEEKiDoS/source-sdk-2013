// Transplant after the three admitted M6 callback bodies in existing director_item_manager.cpp.
// Replace each old CON_COMMAND_F head with static void <name>(const CCommand &args); retain its full native body.
// No duplicate callback/helper or macro-generated registration survives the clean cutover.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native object 0x0083E0EC; callback 0x002838D0; command_function.
static ConCommand director_log_scavenge_items_command( "director_log_scavenge_items", director_log_scavenge_items, "Writes out a log of the scavenge items on this map (for debugging)", 0x00000006 );

// Native object 0x0083D8E8; callback 0x0025BCC0; command_function.
static ConCommand director_report_scavenge_items_command( "director_report_scavenge_items", director_report_scavenge_items, "Dump to the log all the scavengable items", 0x00004004 );

// Native object 0x0083E0C8; callback 0x0027CB60; command_function.
static ConCommand director_solve_item_density_command( "director_solve_item_density", director_solve_item_density, "Pass the number of items you'd want in this map and this spits out the map density value", 0x00004004 );

#endif
