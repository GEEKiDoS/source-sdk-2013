// Transplant into existing src/game/server/l4d2/terrorplayer_replacement.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00333AD0 (command_function).
static void CC_sb_takecontrol( const CCommand &args )
{
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( !player ) return;
	SurvivorBot *target = NULL;
	if ( args.ArgC() > 1 && *args[1] && Q_stricmp( args[1], "next" ) )
	{
		const int character = GetTerrorSurvivorCharacterForName( args[1] );
		if ( character == SurvivorCharacter_Unknown ) return;
		CTerrorPlayer *first = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) );
		if ( !first || !first->IsBot() ) return;
		target = dynamic_cast< SurvivorBot * >( first );
	}
	else if ( args.ArgC() <= 1 || !*args[1] )
	{
		extern CBaseEntity *GetNextCommandEntity( CBasePlayer *, const char *, CBaseEntity * );
		CBaseEntity *picked = GetNextCommandEntity( player, "", NULL );
		if ( picked && picked->IsPlayer() ) target = dynamic_cast< SurvivorBot * >( picked );
	}
	if ( !target || !target->IsBot() || target->GetTeamNumber() != TEAM_SURVIVOR )
	{
		target = NULL;
		const int current = static_cast< int >( player->GetSurvivorCharacter() );
		int character = ( current + 1 ) % 4;
		while ( character != current && !target )
		{
			if ( character != SurvivorCharacter_Unknown )
			{
				CTerrorPlayer *first = FindSurvivorByCharacter( static_cast< SurvivorCharacterType >( character ) );
				if ( first && first->IsBot() ) target = dynamic_cast< SurvivorBot * >( first );
			}
			character = ( character + 1 ) % 4;
		}
		if ( !target ) return;
	}
	CTerrorPlayer *associated = player->GetSpectatedBot();
	if ( SurvivorBot *old = dynamic_cast< SurvivorBot * >( associated ) ) old->ClearHumanSpectator();
	if ( BossZombiePlayerBot *old = dynamic_cast< BossZombiePlayerBot * >( associated ) ) old->ClearHumanSpectator();
	player->ChangeTeam( TEAM_SPECTATOR );
	target->SetHumanSpectator( player );
	player->SetObserverTarget( target );
	player->TakeOverBot( false );
}

// Native object 0x00855F54; callback 0x00333AD0; command_function.
static ConCommand sb_takecontrol_command( "sb_takecontrol", CC_sb_takecontrol, "Take control of a bot.", 0x00004004 );

#endif
