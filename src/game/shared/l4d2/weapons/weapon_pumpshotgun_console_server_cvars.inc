// Transplant into src/game/shared/l4d2/weapon_pumpshotgun.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00865E50; constructor 0x005A2E39.
ConVar shotgun_reload_cancel_delay( "shotgun_reload_cancel_delay", "0.5", 0x00002006, "On reload from empty, time before a fire command is recognized to cancel the reload." );

#endif
