// Transplant into src/game/server/gameinterface.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native object 0x007F7800; constructor 0x00588019.
ConVar sv_no_navmesh( "sv_no_navmesh", "0", 0x00004006, "Block loading of the navmesh. Unplayable, only used for memory sampling." );

// Native object 0x007F79F8; constructor 0x00588166.
ConVar debug_l4dverbose( "debug_l4dverbose", "1", 0x00000006, "Add more l4d specific data to the bug report." );

// Native object 0x007F7B00; constructor 0x00588221.
ConVar sv_threaded_init( "sv_threaded_init", "0", 0x00000006, "" );



#endif
