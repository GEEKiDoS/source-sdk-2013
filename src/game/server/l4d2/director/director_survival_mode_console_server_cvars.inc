// Transplant into src/game/server/l4d2/director_survival_mode.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084DC58; constructor 0x005998F4.
ConVar survival_generate_fake_times( "survival_generate_fake_times", "1", 0x00006006, "" );

#endif
