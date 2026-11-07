// Transplant into src/game/shared/l4d2/terror_ammodef.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0084BFB0; constructor 0x00598F64.
ConVar ammo_ammo_pack_max( "ammo_ammo_pack_max", "1", 0x00002006, "" );

// Native object 0x0084C040; constructor 0x00598FC4.
ConVar ammo_chainsaw_max( "ammo_chainsaw_max", "20", 0x00002006, "" );

#endif
