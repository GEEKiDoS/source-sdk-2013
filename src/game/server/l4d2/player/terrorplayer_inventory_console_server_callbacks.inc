// Transplant into existing src/game/server/l4d2/terrorplayer_inventory.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00305250 (command_function).
static void CC_z_removeitems( const CCommand &args )
{
	for ( int i = 1; i <= gpGlobals->maxClients; ++i )
	{
		CTerrorPlayer *player = ToTerrorPlayer( UTIL_PlayerByIndex( i ) );
		if ( !player || !player->edict() || !player->entindex() || !player->IsPlayer() || !player->IsConnected() || player->GetTeamNumber() != TEAM_SURVIVOR ) continue;
		for ( int id = 0; id < 56; ++id )
		{
			CTerrorWeapon *weapon = player->GetWeaponOfType( id );
			if ( !weapon ) continue;
			if ( id == 1 )
			{
				if ( weapon->IsDualWielding() )
				{
					CTerrorGun *gun = weapon->GetTerrorGun();
					if ( gun ) gun->RemoveDualWeapons( false );
				}
			}
			else
			{
				player->Weapon_Detach( weapon );
				UTIL_Remove( weapon );
			}
		}
	}
}

// Native object 0x00855EE8; callback 0x00305250; command_function.
static ConCommand z_removeitems_command( "z_removeitems", CC_z_removeitems, "Removes all items from survivors", 0x00004004 );

#endif
