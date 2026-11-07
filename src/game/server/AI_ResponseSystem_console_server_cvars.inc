// Transplant into src/game/server/AI_ResponseSystem.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x007CF610; constructor 0x00582346.
ConVar rr_debugresponseconcept( "rr_debugresponseconcept", "", 0x00000006, "If set, rr_debugresponses will print only responses testing for the specified concept" );

#endif
