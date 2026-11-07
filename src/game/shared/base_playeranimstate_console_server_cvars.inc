// Transplant into src/game/shared/base_playeranimstate.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007D3FC8; constructor 0x00582609.
ConVar sv_showanimstate_activities( "sv_showanimstate_activities", "1", 0x00004006, "Show activities in the (server) animation state display." );

#endif
