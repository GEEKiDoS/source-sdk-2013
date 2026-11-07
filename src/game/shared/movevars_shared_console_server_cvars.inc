// Transplant into src/game/shared/movevars_shared.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007FB7B8; constructor 0x00589659.
ConVar sv_noclipfriction( "sv_noclipfriction", "4", 0x00002106, "Friction during noclip move." );

#endif
