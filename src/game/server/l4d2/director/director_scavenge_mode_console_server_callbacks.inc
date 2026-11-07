// Transplant into existing src/game/server/l4d2/director_scavenge_mode.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002857B0 (command_function).
static void CC_scenario_end( const CCommand &args )
{
	TheDirector->EndScenario();
}

// Native object 0x0083E458; callback 0x002857B0; command_function.
static ConCommand scenario_end_command( "scenario_end", CC_scenario_end, "Force the current scenario to end", 0x00004006 );

// Native callback 0x002857C0 (command_function).
static void CC_scavenge_increment_score( const CCommand &args )
{
	TheDirector->IncrementTeamScore( TEAM_SURVIVOR, NULL );
}

// Native object 0x0083E47C; callback 0x002857C0; command_function.
static ConCommand scavenge_increment_score_command( "scavenge_increment_score", CC_scavenge_increment_score, "Increment the scavenge team score", 0x00004006 );

// Native callback 0x00287220 (command_function).
static void CC_next_scavenge_cluster( const CCommand &args )
{
	TheDirector->GetScavengeMode()->SelectNextCluster();
}

// Native object 0x0083E4A0; callback 0x00287220; command_function.
static ConCommand next_scavenge_cluster_command( "next_scavenge_cluster", CC_next_scavenge_cluster, "spawn the next scavenge cluster", 0x00004006 );

// Native callback 0x00286500 (command_function).
static void CC_scavenge_start( const CCommand &args )
{
	TerrorShowViewPortPanel( -1, "ready_countdown", false, NULL );
	TheDirector->GetScavengeMode()->StartRound();
}

// Native object 0x0083E1AC; callback 0x00286500; command_function.
static ConCommand scavenge_start_command( "scavenge_start", CC_scavenge_start, "Force the scavenge round to start", 0x00004006 );

#endif
