// Transplant into existing src/game/server/l4d2/terrorplayer_transition.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00308330 (command_function).
static void CC_dump_avatar_info( const CCommand &args )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CBasePlayer *player = UTIL_PlayerByIndex( i );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() ) continue;
		KeyValues *avatar = TheDirector->GetPlayerAvatarInfo( player->edict() );
		const char *avatarName = avatar ? avatar->GetString( "avatar", "" ) : "<null>";
		const char *team = avatar ? avatar->GetString( "team", "" ) : "<null>";
		Msg( "%d: %s: team = '%s', avatar = '%s'\n", player->entindex(), player->GetPlayerName(), team, avatarName );
	}
}

// Native object 0x008565F0; callback 0x00308330; command_function.
static ConCommand dump_avatar_info_command( "dump_avatar_info", CC_dump_avatar_info, "Print out avatar info of each plaeyr", 0x00000004 );

// Native callback 0x002B67F0 (command_function).
static void CC_test_point( const CCommand &args )
{
	if ( args.ArgC() != 4 ) return;
	const Vector point( atof( args[1] ), atof( args[2] ), atof( args[3] ) );
	trace_t trace;
	UTIL_TraceLine( point + Vector( 0, 0, 5.0f ), point - Vector( 0, 0, 10000.0f ),
		33636875u, NULL, COLLISION_GROUP_NONE, &trace );
	const bool inMap = !trace.startsolid && trace.fraction > 0.0f && trace.fraction < 1.0f;
	DevMsg( "Pos %f %f %f is%s in the map.\n", point.x, point.y, point.z, inMap ? "" : " not" );
}

// Native object 0x008494BC; callback 0x002B67F0; command_function.
static ConCommand test_point_command( "test_point", CC_test_point, "", 0x00000004 );

#endif
