// Transplant into src/game/server/l4d2/vomitjar_projectile.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00861790; constructor 0x005A0AC9.
ConVar z_vomit_fade_duration( "z_vomit_fade_duration", "5", 0x00006004, "How long the fade takes" );

#endif
