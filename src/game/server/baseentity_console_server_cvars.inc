// Transplant into src/game/server/baseentity.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007D6CE8; constructor 0x00582CD9.
ConVar sv_tracereffects_limit_general( "sv_tracereffects_limit_general", "15", 0x00002006, "Limits the number of visible tracers per frame" );

// Native object 0x007D77F8; constructor 0x00582E76.
ConVar ent_show_contexts( "ent_show_contexts", "0", 0x00000006, "Show entity contexts in ent_text display" );

// Native object 0x007D78D8; constructor 0x00582F61.
ConVar autoaim_viewing_client( "autoaim_viewing_client", "1", 0x00000006, "" );

// Native object 0x007D7920; constructor 0x00582F91.
ConVar debug_overlay_fullposition( "debug_overlay_fullposition", "0", 0x00000006, "" );

// Native object 0x007D7968; constructor 0x00582FC4.
ConVar ent_text_allow_script( "ent_text_allow_script", "0", 0x00004004, "" );

#endif
