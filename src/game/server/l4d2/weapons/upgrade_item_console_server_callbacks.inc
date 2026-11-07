// Transplant into existing src/game/server/l4d2/upgrade_item.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

namespace
{
const char *const s_ConsoleUpgradeNames[] = { "INCENDIARY_AMMO", "EXPLOSIVE_AMMO", "LASER_SIGHT" };
// S351B30 is case-insensitive, ends with the real -1 sentinel.
int ConsoleUpgradeFromName( const char *name )
{
 for ( int i = 0; i < ARRAYSIZE( s_ConsoleUpgradeNames ); ++i )
  if ( name && !Q_stricmp( name, s_ConsoleUpgradeNames[i] ) ) return i;
 return -1;
}
}

// Native completion 0x002FAA00.
static int Complete_RVA002FAA00( const char *partial, char commands[64][64] )
{
	const char *space = Q_strstr( partial, " " );
	if ( !space ) return 0;
	char command[64];
	V_StrSlice( partial, 0, space - partial, command, sizeof( command ) );
	const char *prefix = space + 1;
	const int prefixLength = Q_strlen( prefix );
	int count = 0;
	for ( int upgrade = 0; upgrade < 3; ++upgrade )
		if ( !Q_strnicmp( s_ConsoleUpgradeNames[upgrade], prefix, prefixLength ) )
		{
			Q_snprintf( commands[count++], 64, "%s %s", command, s_ConsoleUpgradeNames[upgrade] );
			if ( count == 64 ) return count;
		}
	return count;
}

// Native callback 0x0030A010 (command_function).
static void CC_upgrade_add( const CCommand &args )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( !player || !player->IsPlayer() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || args.ArgC() < 2 ) return;
	const int upgrade = ConsoleUpgradeFromName( args[1] );
	if ( upgrade == -1 )
	{
		UTIL_SayText( UTIL_VarArgs( "%s INVALID UPGRADE '%s'\n", player->GetPlayerName(), args[1] ), player );
		return;
	}
	player->GiveUpgrade( upgrade );
}

// Native object 0x00855F0C; callback 0x0030A010; command_function.
static ConCommand upgrade_add_command( "upgrade_add", CC_upgrade_add, "Adds an upgrade to the survivor", 0x00004004, Complete_RVA002FAA00 );

// Native callback 0x002FFE60 (command_function).
static void CC_upgrade_remove( const CCommand &args )
{
	CTerrorPlayer *player = dynamic_cast< CTerrorPlayer * >( UTIL_GetCommandClient() );
	if ( !player || !player->IsPlayer() || !player->IsAlive() || player->GetTeamNumber() != TEAM_SURVIVOR || args.ArgC() < 2 ) return;
	const int upgrade = ConsoleUpgradeFromName( args[1] );
	if ( upgrade == -1 )
	{
		UTIL_SayText( UTIL_VarArgs( "%s INVALID UPGRADE '%s'\n", player->GetPlayerName(), args[1] ), player );
		return;
	}

	CTerrorWeapon *primary = dynamic_cast< CTerrorWeapon * >( player->Weapon_GetSlot( 0 ) );
	if ( primary ) primary->RemoveUpgrade( upgrade );
}

// Native object 0x00855F30; callback 0x002FFE60; command_function.
static ConCommand upgrade_remove_command( "upgrade_remove", CC_upgrade_remove, "Removes an upgrade from the survivor", 0x00004004, Complete_RVA002FAA00 );

#endif
