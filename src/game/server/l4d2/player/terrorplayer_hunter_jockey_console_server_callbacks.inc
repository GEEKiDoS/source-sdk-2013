// Transplant into existing src/game/server/l4d2/terrorplayer_hunter_jockey.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002CB500 (command_function).
static void CC_jockey( const CCommand &args )
{
	CBasePlayer *base = UTIL_GetCommandClient();
	if ( !base ) return; // Ledger306: native picker otherwise dereferences NULL.
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *player, char *classname );
	CTerrorPlayer *target = dynamic_cast< CTerrorPlayer * >( FindPickerEntityClass( base, const_cast< char * >( "player" ) ) );
	if ( !target || target->GetTeamNumber() != TEAM_SURVIVOR ) return;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
	if ( !player ) return;
	target->SetJockeyVictim( player );
	player->SetJockeyAttacker( target );
}

// Native object 0x0084B488; callback 0x002CB500; command_function.
static ConCommand jockey_command( "jockey", CC_jockey, "Giddyup.", 0x00004004 );

// Native callback 0x002CB540 (command_function).
static void CC_jockeyme( const CCommand &args )
{
	CBasePlayer *base = UTIL_GetCommandClient();
	if ( !base ) return; // Ledger306: native picker otherwise dereferences NULL.
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *player, char *classname );
	CTerrorPlayer *target = dynamic_cast< CTerrorPlayer * >( FindPickerEntityClass( base, const_cast< char * >( "player" ) ) );
	if ( !target || target->GetTeamNumber() != TEAM_SURVIVOR ) return;
	CTerrorPlayer *player = static_cast< CTerrorPlayer * >( base );
	if ( !player ) return;
	player->SetJockeyVictim( target );
	target->SetJockeyAttacker( player );
}

// Native object 0x0084B4AC; callback 0x002CB540; command_function.
static ConCommand jockeyme_command( "jockeyme", CC_jockeyme, "Giddyup.", 0x00004004 );

#endif
