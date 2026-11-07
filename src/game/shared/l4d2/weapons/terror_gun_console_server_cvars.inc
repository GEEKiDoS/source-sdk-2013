// Transplant into src/game/shared/l4d2/terror_gun.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )


// Native object 0x0086C528; constructor 0x005A6484.
ConVar survivor_accuracy_upgrade_factor( "survivor_accuracy_upgrade_factor", "0.5", 0x00006004, "" );

#endif
