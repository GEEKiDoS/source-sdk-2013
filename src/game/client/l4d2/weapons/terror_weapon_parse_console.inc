// Compose into shared/l4d2/terror_weapon_parse.cpp, CLIENT_DLL && L4D2. C30F2A0.
static void CC_WeaponReparseClient( const CCommand & )
{
	for ( int id = 0; id < 56; ++id )
	{
		CTerrorWeaponInfo *info = const_cast< CTerrorWeaponInfo * >( GetTerrorWeaponInfo( id ) );
		if ( !info ) continue;
		char filename[128];
		V_snprintf( filename, sizeof( filename ), "scripts/%s", info->szClassName );
		KeyValues *data = ReadEncryptedKVFile( filesystem, filename, GameRules()->GetEncryptionKey() );
		if ( data ) { info->Parse( data, info->szClassName ); data->deleteThis(); }
	}
}
