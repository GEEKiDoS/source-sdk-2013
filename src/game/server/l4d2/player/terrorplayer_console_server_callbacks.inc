// Transplant into existing src/game/server/l4d2/terrorplayer.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00308350 (command_function).
static void CC_dump_pz_data( const CCommand &args )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ||
			player->GetTeamNumber() != TEAM_INFECTED || player->IsBot() ) continue;
		DevMsg( "%s:\n", player->GetPlayerName() );
		const float average = player->m_deadCount != 0 ? player->m_totalDeadDuration / player->m_deadCount : -1.0f;
		DevMsg( "  Dead wait time = %3.2f / %3.2f / %3.2f \n", player->m_maxDeadDuration, average, player->m_minDeadDuration );
		for ( int zombieClass = 0; zombieClass < 9; ++zombieClass )
			DevMsg( "  %d: %s\n", player->m_classSpawnCount[zombieClass], GetZombieClassName( zombieClass ) );
		DevMsg( "\n" );
	}
}

// Native object 0x00856614; callback 0x00308350; command_function.
static ConCommand dump_pz_data_command( "dump_pz_data", CC_dump_pz_data, "Dump PZ data", 0x00000004 );

#endif
