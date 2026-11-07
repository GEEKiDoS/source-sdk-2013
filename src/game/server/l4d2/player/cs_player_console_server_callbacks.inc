// Transplant into existing src/game/server/l4d2/cs_player.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00240F80 (command_function).
static void CC_CreatePredictionError( const CCommand &args )
{
	const float distance = args.ArgC() >= 2 ? static_cast< float >( atof( args[1] ) ) : 32.0f;
	edict_t *edict = INDEXENT( 1 );
	if ( !edict || edict->IsFree() ) edict = INDEXENT( 0 );
	CBaseEntity *entity = edict ? CBaseEntity::Instance( edict ) : NULL;
	if ( entity ) entity->SetAbsOrigin( entity->GetAbsOrigin() + Vector( distance, 0.0f, 0.0f ) );
}

// Native object 0x00837ABC; callback 0x00240F80; command_function.
static ConCommand CreatePredictionError_command( "CreatePredictionError", CC_CreatePredictionError, "Create a prediction error", 0x00004004 );

// Native callback 0x002431E0 (command_function).
static void CC_cs_make_vip( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() || args.ArgC() != 2 ) return;
	CCSPlayer *player = ToCSPlayer( UTIL_PlayerByIndex( atoi( args[1] ) ) );
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *base = UTIL_PlayerByIndex( i );
		if ( base && base->edict() && base->entindex() && base->IsPlayer() && base->IsConnected() )
			static_cast< CCSPlayer * >( base )->m_isVIP = false;
	}
	if ( player ) player->m_isVIP = true;
}

// Native object 0x00837B70; callback 0x002431E0; command_function.
static ConCommand cs_make_vip_command( "cs_make_vip", CC_cs_make_vip, "Marks a player as the VIP", 0x00000004 );

// Native callback 0x002400F0 (command_function).
static void CC_spec_target_clear( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( player && player->IsPlayer() ) player->SetObserverTarget( NULL );
}

// Native object 0x0083797C; callback 0x002400F0; command_function.
static ConCommand spec_target_clear_command( "spec_target_clear", CC_spec_target_clear, "", 0x00000004 );

// Native callback 0x002421D0 (command_function).
static void CC_spec_target( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	CBaseEntity *target = NULL;
	if ( args.ArgC() <= 1 )
	{
		Vector forward;
		player->EyeVectors( &forward );
		trace_t trace;
		UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 3000.0f,
		33579073u, player, COLLISION_GROUP_NONE, &trace );
		if ( trace.DidHit() )
		{
			player->SetObserverTarget( trace.m_pEnt );
			DevMsg( "Observer target set\n" );
			return;
		}
	}
	else
	{
		const char *query = args[1];
		const int length = Q_strlen( query );
		for ( int i = 1; i <= gpGlobals->maxClients; ++i )
		{
			CTerrorPlayer *candidate = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
			if ( !candidate || !candidate->edict() || !candidate->entindex() || !candidate->IsPlayer() ||
				!candidate->IsConnected() || !candidate->IsAlive() ) continue;
			bool matches = !Q_strnicmp( candidate->GetPlayerName(), query, length );
			if ( !matches && ( candidate->GetTeamNumber() == TEAM_SURVIVOR || candidate->GetTeamNumber() == TEAM_L4D1_SURVIVOR ) )
				matches = !Q_strnicmp( GetSurvivorCharacterName( candidate->GetSurvivorCharacter() ), query, length );
			else if ( !matches && candidate->GetTeamNumber() == TEAM_INFECTED )
				matches = !Q_strnicmp( GetZombieClassName( candidate->GetZombieClass() ), query, length );
			if ( matches ) { target = candidate; break; }
		}
		if ( target )
		{
			player->SetObserverTarget( target );
			DevMsg( "Observer target set\n" );
			return;
		}
	}
	player->SetObserverTarget( NULL );
	DevMsg( "Observer target cleared\n" );
}

// Native object 0x00837958; callback 0x002421D0; command_function.
static ConCommand spec_target_command( "spec_target", CC_spec_target, "", 0x00000004 );

#endif
