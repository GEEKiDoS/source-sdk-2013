// Transplant into src/game/shared/achievementmgr.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007CF288; constructor 0x00581779.
ConVar achievement_disable( "achievement_disable", "0", 0x00006004, "Turn off achievements." );

#endif
