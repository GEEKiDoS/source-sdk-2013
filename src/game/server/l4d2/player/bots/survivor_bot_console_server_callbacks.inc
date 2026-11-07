// Transplant into existing src/game/server/l4d2/survivor_bot.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// S2FA710 calls native case-sensitive V_strcmp, not a broken comparator.
static int CompareConsoleCompletionStrings( const void *left, const void *right )
{
 return Q_strcmp( static_cast< const char * >( left ), static_cast< const char * >( right ) );
}

extern ConVar director_no_survivor_bots;

// Native completion 0x002FA720.
static int Complete_RVA002FA720( const char *partial, char commands[64][64] )
{
	const char *space = Q_strstr( partial, " " );
	if ( !space ) return 0;
	char command[64];
	V_StrSlice( partial, 0, space - partial, command, sizeof( command ) );
	const char *prefix = space + 1;
	const int prefixLength = Q_strlen( prefix );
	int count = 0;
	static const char *const special[] = { "ammo", "health" };
	for ( int i = 0; i < ARRAYSIZE( special ); ++i )
		if ( !Q_strnicmp( special[i], prefix, prefixLength ) )
		{
			Q_snprintf( commands[count++], 64, "%s %s", command, special[i] );
			if ( count == 64 ) return count;
		}
	for ( int id = 1; id < 45; ++id )
	{
		const char *name = WeaponIDToAlias( id );
		if ( !Q_strnicmp( name, prefix, prefixLength ) )
		{
			Q_snprintf( commands[count++], 64, "%s %s", command, name );
			if ( count == 64 ) return count;
		}
	}
	CMeleeWeaponInfoStore *store = MeleeWeaponInfoStore();
	for ( int i = 0; i < store->GetMeleeWeaponCount(); ++i )
	{
		const CMeleeWeaponInfo *info = store->GetMeleeWeaponInfo( i );
		if ( info && !Q_strnicmp( info->m_szName, prefix, prefixLength ) )
		{
			Q_snprintf( commands[count++], 64, "%s %s", command, info->m_szName );
			if ( count == 64 ) return count;
		}
	}
	qsort( commands, count, 64, CompareConsoleCompletionStrings );
	return count;
}

// Native callback 0x0038B6D0 (command_function).
static void CC_sb_add( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	director_no_survivor_bots.SetValue( 0 );
	const int character = args.ArgC() > 1 && *args[1] ? GetTerrorSurvivorCharacterForName( args[1] ) : SurvivorCharacter_Unknown;
	TheDirector->CreateSurvivorBot( static_cast< SurvivorCharacterType >( character ) );
}

// Native object 0x00862EEC; callback 0x0038B6D0; command_function.
static ConCommand sb_add_command( "sb_add", CC_sb_add, "Add a Survivor Bot.", 0x00000004 );

// Native callback 0x0038B720 (command_function).
static void CC_sb_force_max_intensity( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	SurvivorBot *bot = dynamic_cast< SurvivorBot * >( TheNextBots().GetSelected() );
	if ( !bot ) return;
	for ( int i = 0; i < 5; ++i ) bot->m_survivorIntensity.Add( 4 );
}

// Native object 0x008636A8; callback 0x0038B720; command_function.
static ConCommand sb_force_max_intensity_command( "sb_force_max_intensity", CC_sb_force_max_intensity, "Force intensity of selected SurvivorBot to maximum level.", 0x00004004 );

// Native callback 0x0038E410 (command_function).
static void CC_sb_give( const CCommand &args )
{
	if ( args.ArgC() != 2 ) return;
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ||
			player->GetTeamNumber() != TEAM_SURVIVOR || !dynamic_cast< INextBot * >( player ) ) continue;
		player->GiveNamedItemInternal( args[1], 0, true, NULL );
	}
}

// Native object 0x008638C4; callback 0x0038E410; command_function.
static ConCommand sb_give_command( "sb_give", CC_sb_give, "Give an item to each Survivor bot", 0x00004004, Complete_RVA002FA720 );

// Native callback 0x0038E3D0 (command_function).
static void CC_sb_give_random_weapon( const CCommand &args )
{
	static const char *const names[] = { "weapon_autoshotgun", "weapon_rifle", "weapon_smg", "weapon_hunting_rifle", "weapon_pumpshotgun" };
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ||
			player->GetTeamNumber() != TEAM_SURVIVOR || !dynamic_cast< INextBot * >( player ) ) continue;
		const int selected = RandomInt( 0, ARRAYSIZE( names ) - 1 );
		CBaseEntity *item = player->GiveNamedItemInternal( names[selected], 0, true, NULL );
		if ( item ) player->Weapon_Switch( static_cast< CBaseCombatWeapon * >( item ) );
	}
}

// Native object 0x008638A0; callback 0x0038E3D0; command_function.
static ConCommand sb_give_random_weapon_command( "sb_give_random_weapon", CC_sb_give_random_weapon, "Give a random weapon to each Survivor bot", 0x00004004 );

// Native callback 0x00397BC0 (legacy_function).
void SurvivorBot::CC_sb_move_to_cursor(  )
{
	CBasePlayer *player = UTIL_GetListenServerHost();
	if ( !player ) return;
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 999999.88f,
		33636875u, player, COLLISION_GROUP_NONE, &trace );
	if ( !( trace.fraction < 1.0f ) && !trace.allsolid && !trace.startsolid ) return;
	struct MoveSurvivors
	{
		explicit MoveSurvivors( const Vector &goal ) : m_goal( goal ) {}
		bool operator()( INextBot *nextbot )
		{
			SurvivorBot *bot = dynamic_cast< SurvivorBot * >( nextbot );
			if ( bot && bot->IsAlive() ) bot->m_debugMoveGoal = m_goal;
			return true;
		}
		Vector m_goal;
	} move( trace.endpos );
	TheNextBots().ForEachBot( move );
}

// Native object 0x008636CC; callback 0x00397BC0; legacy_function.
static ConCommand sb_move_to_cursor_command( "sb_move_to_cursor", SurvivorBot::CC_sb_move_to_cursor, "Sends survivor bots to cursor target", 0x00000004 );

#endif
