// Same four native Steamworks registration objects, housed in selected
// shared/steamworks_gamestats_sdk.cpp. The standalone server does not compile
// the unrelated generic server/gamestats.cpp translation unit.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00823F28; constructor 0x0058E009.
ConVar steamworks_server_sessionid( "steamworks_server_sessionid", "0", 0x00002016, "The server session ID for the new steam works gamestats." );

// Native object 0x00823F70; constructor 0x0058E039.
ConVar steamworks_show_uploads( "steamworks_show_uploads", "0", 0x00002006, "If set to non-zero, will show when submitting tables." );

// Native object 0x00823FB8; constructor 0x0058E069.
ConVar steamworks_stats_disable( "steamworks_stats_disable", "0", 0x00002016, "If set to non-zero then no steam works game stats will be used." );

// Native object 0x00824000; constructor 0x0058E099.
ConVar steamworks_immediate_upload( "steamworks_immediate_upload", "1", 0x00002016, "If set to zero the stats will only be uploaded at the end of the session. If set to non-zero, stats will immediately be uploaded." );

#endif
