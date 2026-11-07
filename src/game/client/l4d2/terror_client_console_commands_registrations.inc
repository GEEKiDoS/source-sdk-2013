// Append after the native callback bodies in the owner TU; CLIENT_DLL && L4D2 only.
// client:console:1551 object 0x007a04c8 callback 0x0019fd10
static ConCommand ShowSteamStatsSessionID( "ShowSteamStatsSessionID", CC_ShowSteamStatsSessionID, "Prints out the client and server session ID's (developer convar must be set to non-zero).", 0xa );

// client:console:1580 object 0x007c493c callback 0x0027b350
static ConCommand cl_crash( "cl_crash", CC_ClientCrash, "Causes a client crash for testing", 0x4008 );

// client:console:1582 object 0x007820e0 callback 0x00176180
static ConCommand cl_destroy_ragdolls( "cl_destroy_ragdolls", CC_DestroyRagdolls, "Destroys all client-side ragdolls", 0x8 );

// client:console:1584 object 0x0075825c callback 0x001256b0
static ConCommand cl_dumpplayer( "cl_dumpplayer", CC_DumpPlayer, "Dumps info about a player", 0x4008 );

// client:console:1605 object 0x00733988 callback 0x0009ade0
static ConCommand cl_ss_origin( "cl_ss_origin", CC_SplitScreenOrigin, "print origin in script format", 0x8 );

// client:console:1606 object 0x00724bb8 callback 0x0004bca0
static ConCommand cl_updatevisibility( "cl_updatevisibility", CC_UpdateVisibility, "Updates visibility bits.", 0x8 );

// client:console:1621 object 0x007a2b74 callback 0x001baf30
static ConCommand getpos_exact( "getpos_exact", CC_GetPosExact, "dump origin and angles to the console", 0x8 );

// client:console:1656 object 0x007bbc3c callback 0x002544b0
static ConCommand r_skyboxfogfactor( "r_skyboxfogfactor", CC_SkyboxFogFactor, "Lerp between 2D skybox rendering and fog color.  0.0 means regular 2D skybox rendering, 1.0 is fully fog color.", 0x8 );

// client:console:1717 object 0x007bbc18 callback 0x00254410
static ConCommand z_wind_dir( "z_wind_dir", CC_WindDirection, "sets wind direction and strength", 0x8 );

static ConCommand querycache_stats( "cl_querycache_stats", CC_QueryCacheStats, "Display status of the query cache (client only)", 0x4008 );
static ConCommand client_phys2_stats( "cl_phys2_stats", CC_ClientPhysics2Stats, "Dumps client physics stats", 0x4008 );
static ConCommand global_phys2_stats( "phys2_stats", CC_Physics2Stats, "Dumps physics stats", 0x4008 );
static ConCommand save_client_phys2( "phys2_save_client", CC_SaveClientPhysics2, "Saves client physics scene", 0x4008 );
static ConCommand dump_split_hacks( "cl_dumpsplithacks", CC_DumpSplitScreenHacks, "Dump split screen workarounds.", 0x8 );
static ConCommand teleport_split_player( "ss_teleport", CC_SplitScreenTeleport, "Teleport other splitscreen player to my location.", 0x400a );
