// Transplant into existing src/game/shared/l4d2/terror_weapon_parse.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x003ECC90 (command_function).
static void CC_weapon_reparse_server( const CCommand &args )
{
	for ( int id = 0; id < 56; ++id )
	{
		CTerrorWeaponInfo *info = const_cast< CTerrorWeaponInfo * >( GetTerrorWeaponInfo( id ) );
		if ( !info ) continue;
		char filename[128];
		Q_snprintf( filename, sizeof( filename ), "scripts/%s", info->szClassName );
		KeyValues *data = ReadEncryptedKVFile( filesystem, filename, g_pGameRules->GetEncryptionKey() );
		if ( data ) { info->Parse( data, info->szClassName ); data->deleteThis(); }
	}
}

// Native object 0x0086DD00; callback 0x003ECC90; command_function.
static ConCommand weapon_reparse_server_command( "weapon_reparse_server", CC_weapon_reparse_server, "Reloads the weapon script files", 0x00004004 );

#endif
