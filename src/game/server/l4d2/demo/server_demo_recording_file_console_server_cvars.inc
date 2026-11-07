// Transplant into src/game/server/l4d2/server_demo_recording_file.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00822708; constructor 0x0058D579.
ConVar sv_demo_entity_record_rate( "sv_demo_entity_record_rate", "30", 0x00004044, "Set the server demo record rate for entities." );

#endif
