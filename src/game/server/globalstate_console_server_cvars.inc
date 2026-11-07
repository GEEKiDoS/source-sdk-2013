// Transplant into src/game/server/globalstate.cpp; replace any already-landed same-object definition.
// Server only: client has separately recovered registrations. No new VPC row.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// S:0x001D0B40. Update existing global state or create it for this map.
static void RegenerationForceChanged( IConVar *variable, const char *oldValue, float oldFloat )
{
	ConVar *value = static_cast< ConVar * >( variable );
	const int index = GlobalEntity_GetIndex( "player_regenerates_health" );
	if ( index >= 0 )
		GlobalEntity_SetState( index, value->GetBool() ? GLOBAL_ON : GLOBAL_OFF );
	else
		GlobalEntity_Add( "player_regenerates_health", STRING( gpGlobals->mapname ), value->GetBool() ? GLOBAL_ON : GLOBAL_OFF );
}

// Native object 0x00822AC0; constructor 0x0058D6FE.
ConVar sv_regeneration_force_on( "sv_regeneration_force_on", "0", 0x00004004, "Cheat to test regenerative health systems", RegenerationForceChanged );

#endif
