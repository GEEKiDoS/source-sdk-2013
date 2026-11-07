// Transplant into existing src/game/server/gameinterface.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x000ED5D0 (command_function).
static void CC_sv_showhitboxes_cursor( const CCommand &args )
{
	const int index = UTIL_GetCommandClientIndex();
	CBasePlayer *player = index > 0 ? UTIL_PlayerByIndex( index ) : NULL;
	if ( !player ) return; // Ledger306: skip native null-player picker use only.
	extern CBaseEntity *FindPickerEntity( CBasePlayer * );
	CBaseEntity *entity = FindPickerEntity( player );
	if ( entity ) sv_showhitboxes.SetValue( entity->edict() ? entity->entindex() : 0 );
}

// Native object 0x007F7AD0; callback 0x000ED5D0; command_function.
static ConCommand sv_showhitboxes_cursor_command( "sv_showhitboxes_cursor", CC_sv_showhitboxes_cursor, "Show the hitboxes of the pointed-at entity", 0x00004004 );

#endif
