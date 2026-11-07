// Transplant into existing src/game/server/l4d2/director_frame.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

extern ConVar director_no_bosses;
extern ConVar z_common_limit;
extern ConVar z_mega_mob_size;
extern ConVar director_ready_duration;

// Native callback 0x0025BBC0 (command_function).
static void CC_director_stop( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	director_no_bosses.SetValue( 1 );
	director_no_specials.SetValue( 1 );
	director_no_mobs.SetValue( 1 );
	z_common_limit.SetValue( 0 );
	z_mega_mob_size.SetValue( 1 );
	director_ready_duration.SetValue( 0 );
}

// Native object 0x0083D810; callback 0x0025BBC0; command_function.
static ConCommand director_stop_command( "director_stop", CC_director_stop, "Shuts off all wanderers, mobs, specials, and bosses", 0x00004004 );

// Native callback 0x0025BC20 (command_function).
static void CC_director_start( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	director_no_bosses.Revert();
	director_no_specials.Revert();
	director_no_mobs.Revert();
	z_common_limit.Revert();
	z_mega_mob_size.Revert();
	director_no_death_check.Revert();
}

// Native object 0x0083D834; callback 0x0025BC20; command_function.
static ConCommand director_start_command( "director_start", CC_director_start, "Restores spawning of all wanderers, mobs, specials, and bosses", 0x00004004 );

// Native callback 0x00260360 (command_function).
static void CC_director_print_player_counts( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	for ( int team = TEAM_SURVIVOR; team <= TEAM_INFECTED; ++team )
	{
		int saved = 0, restored = 0;
		g_SavedTerrorPlayerStates.GetRestoringPlayerCounts( team, &saved, &restored );
		const char *label = team == TEAM_SURVIVOR ? "survivors" : "infected";
		if ( player ) ClientPrint( player, HUD_PRINTTALK, UTIL_VarArgs( "%d transitioning %s\n", saved - restored, label ) );
		else Msg( "%d transitioning %s\n", saved - restored, label );
		const int connected = TheDirector->CountPlayersOnTeam( team, false );
		if ( player ) ClientPrint( player, HUD_PRINTTALK, UTIL_VarArgs( "%d connected %s\n", connected, label ) );
		else Msg( "%d connected %s\n", connected, label );
	}
}

// Native object 0x0083D858; callback 0x00260360; command_function.
static ConCommand director_print_player_counts_command( "director_print_player_counts", CC_director_print_player_counts, "Prints out counts of connected players and transitioning players (for debugging)", 0x00000004 );

#endif
