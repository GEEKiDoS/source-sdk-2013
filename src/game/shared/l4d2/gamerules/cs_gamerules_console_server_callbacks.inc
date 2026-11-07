// Transplant into existing src/game/shared/l4d2/cs_gamerules.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x0023CD80 (command_function).
static void CC_map_showspawnpoints( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	static const char *const names[] = { "info_player_terrorist", "info_zombie_spawn" };
	for ( int kind = 0; kind < ARRAYSIZE( names ); ++kind )
	{
		CBaseEntity *spot = NULL;
		while ( ( spot = gEntList.FindEntityByClassname( spot, names[kind] ) ) != NULL )
		{
			const bool valid = g_pGameRules->IsSpawnPointValid( spot, NULL );
			const Vector &mins = g_pGameRules->GetViewVectors()->m_vHullMin;
			const Vector &maxs = g_pGameRules->GetViewVectors()->m_vHullMax;
			NDebugOverlay::Box( spot->GetAbsOrigin(), mins, maxs, valid ? 0 : 255, valid ? 255 : 0, 0, 200, 600.0f );
		}
	}
}

// Native object 0x00837000; callback 0x0023CD80; command_function.
static ConCommand map_showspawnpoints_command( "map_showspawnpoints", CC_map_showspawnpoints, "Shows player spawn points (red=invalid)", 0x00000004 );

#endif
