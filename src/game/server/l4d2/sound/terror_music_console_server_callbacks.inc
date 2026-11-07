// Transplant into existing src/game/server/l4d2/terror_music.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002AA7C0 (command_function).
static void CC_l4d2_snd_adrenaline( const CCommand &args )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( !player || !player->IsPlayer() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) return;
	if ( args.ArgC() > 1 ) player->GetMusic()->OnAdrenalineUsed();
	else player->GetMusic()->OnAdrenalineExpired();
}

// Native object 0x0084935C; callback 0x002AA7C0; command_function.
static ConCommand l4d2_snd_adrenaline_command( "l4d2_snd_adrenaline", CC_l4d2_snd_adrenaline, "", 0x00004004 );

// Native callback 0x002AA830 (command_function).
static void CC_snd_play_in_out( const CCommand &args )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( !player || !player->IsPlayer() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR ) return;
	player->GetMusic()->GeneralCommand( "playInOut", args[1], args[2], atoi( args[3] ) );
}

// Native object 0x00849380; callback 0x002AA830; command_function.
static ConCommand snd_play_in_out_command( "snd_play_in_out", CC_snd_play_in_out, "Plays one of two sounds from entity depending on players in/out location", 0x00004004 );

#endif
