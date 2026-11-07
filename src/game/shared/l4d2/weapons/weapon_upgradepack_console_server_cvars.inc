// Transplant into src/game/shared/l4d2/weapon_upgradepack.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x008685F8; constructor 0x005A42B4.
ConVar ammo_pack_use_duration( "ammo_pack_use_duration", "3", 0x00006004, "" );

#endif
