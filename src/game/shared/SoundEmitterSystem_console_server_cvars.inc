// Transplant into src/game/shared/SoundEmitterSystem.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00822F28; constructor 0x0058D829.
ConVar cc_showmissing( "cc_showmissing", "0", 0x00002006, "Show missing closecaption entries." );

// Native object 0x00822F70; constructor 0x0058D856.
ConVar cc_norepeat( "cc_norepeat", "5", 0x00000006, "In multiplayer games, don't repeat captions more often than this many seconds." );

#endif
