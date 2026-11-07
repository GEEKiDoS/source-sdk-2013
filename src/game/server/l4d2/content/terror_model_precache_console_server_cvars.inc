// Transplant into src/game/server/l4d2/terror_model_precache.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x00852D20; constructor 0x0059B151.
ConVar survivor_model_selection( "survivor_model_selection", "-1", 0x00000006, "" );

#endif
