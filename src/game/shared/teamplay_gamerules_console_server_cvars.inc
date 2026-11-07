// Native constructor8265D8 is housed in selected teamplay_gamerules.cpp.
// The standalone server does not compile the unrelated SDK roundbased/TF TU.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008265D8; constructor 0x0058EA99.
ConVar mp_stalemate_at_timelimit( "mp_stalemate_at_timelimit", "0", 0x00000106, "Allow the match to end when mp_timelimit hits instead of waiting for the end of the current round." );

#endif
