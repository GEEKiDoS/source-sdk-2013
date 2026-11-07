// Transplant into src/game/server/l4d2/l4d_awards.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0083AEF8; constructor 0x00592489.
ConVar z_award_debug( "z_award_debug", "0", 0x00006004, "Get debugging info on all award calcs to the console. (0|1|2|3 for none|yes-no thoughts|every thought|every spammy calc)" );

#endif
