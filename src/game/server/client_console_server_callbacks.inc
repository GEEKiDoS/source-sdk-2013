// Transplant into existing src/game/server/client.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x0008EDF0 (command_function).
static void CC_ent_setpos( const CCommand &args )
{
	if ( !sv_cheats->GetBool() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	if ( args.ArgC() < 4 )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Usage:  ent_setpos index x y <optional z>\n" );
		return;
	}
	const int index = V_atoi( args[1] );
	CBaseEntity *entity = UTIL_EntityByIndex( index );
	if ( !entity )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, UTIL_VarArgs( "ent_setpos no entity %d\n", index ) );
		return;
	}
	const Vector old = entity->GetAbsOrigin();
	const Vector value( static_cast<float>( atof( args[2] ) ), static_cast<float>( atof( args[3] ) ),
		args.ArgC() == 5 ? static_cast<float>( atof( args[4] ) ) : old.z );
	entity->SetAbsOrigin( value );
}

// Native object 0x007DC3B8; callback 0x0008EDF0; command_function.
static ConCommand ent_setpos_command( "ent_setpos", CC_ent_setpos, "Move entity to position", 0x00004004 );

// Native callback 0x0008EF40 (command_function).
static void CC_ent_setang( const CCommand &args )
{
	if ( !sv_cheats->GetBool() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	if ( args.ArgC() < 4 )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Usage:  ent_setang index pitch yaw <optional roll>\n" );
		return;
	}
	const int index = V_atoi( args[1] );
	CBaseEntity *entity = UTIL_EntityByIndex( index );
	if ( !entity )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, UTIL_VarArgs( "ent_setang no entity %d\n", index ) );
		return;
	}
	const QAngle old = entity->GetAbsAngles();
	const QAngle value( static_cast<float>( atof( args[2] ) ), static_cast<float>( atof( args[3] ) ),
		args.ArgC() == 5 ? static_cast<float>( atof( args[4] ) ) : old.z );
	entity->SetAbsAngles( value );
}

// Native object 0x007DC3DC; callback 0x0008EF40; command_function.
static ConCommand ent_setang_command( "ent_setang", CC_ent_setang, "Set entity angles", 0x00004004 );

// Native callback 0x00090FC0 (command_function).
static void CC_rangefinder( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return; // Native NULL player use: ledger306 minimal skip.
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 16384.0f,
		1174421507u, player, COLLISION_GROUP_NONE, &trace );
	if ( !( trace.fraction < 1.0f ) && !trace.allsolid && !trace.startsolid )
	{
		Msg( "Rangefinder didn't hit anything\n" );
		return;
	}
	const Vector delta = trace.startpos - trace.endpos;
	const float squared = ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x;
	const float distance = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( squared ) ) );
	Msg( "DISTANCE: %6.2f inches (%6.2f feet)\n", distance, static_cast< double >( 0.083333336f ) * static_cast< double >( distance ) );
	NDebugOverlay::Cross2D( trace.endpos, 16.0f, 255, 0, 255, false, 1.0f );
}

// Native object 0x007DC124; callback 0x00090FC0; command_function.
static ConCommand rangefinder_command( "rangefinder", CC_rangefinder, "Measures distance along a ray", 0x00004004 );

// Native callback 0x00090A90 (command_function).
static void CC_setpos_player( const CCommand &args )
{
	if ( !sv_cheats->GetBool() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	if ( args.ArgC() < 4 )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Usage:  setpos player_index x y <z optional>\n" );
		return;
	}
	CBasePlayer *target = UTIL_PlayerByIndex( atoi( args[1] ) );
	if ( !target || !target->IsPlayer() ) return;
	const Vector old = target->GetAbsOrigin();
	Vector position( atof( args[2] ), atof( args[3] ), args.ArgC() == 5 ? atof( args[4] ) : old.z );
	target->SetAbsOrigin( position );
	if ( !TestEntityPosition( target, target->PlayerSolidMask() ) ) ClientPrint( target, HUD_PRINTCONSOLE, "setpos into world, use noclip to unstick yourself!\n" );
}

// Native object 0x007DC424; callback 0x00090A90; command_function.
static ConCommand setpos_player_command( "setpos_player", CC_setpos_player, "Move specified player to specified origin (must have sv_cheats).", 0x00004004 );

#endif
