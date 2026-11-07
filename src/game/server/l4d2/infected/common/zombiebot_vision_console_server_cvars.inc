// Transplant into src/game/server/l4d2/zombiebot_vision.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )






// Native object 0x0086E648; constructor 0x005A7214.
ConVar nb_vision_notice_hidden_range( "nb_vision_notice_hidden_range", "50", 0x00004004, "" );

#endif
