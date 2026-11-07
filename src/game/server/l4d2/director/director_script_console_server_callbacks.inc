// Transplant into existing src/game/server/l4d2/director_script.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x002BD800 (legacy_function).
static void CC_warp_all_survivors_to_battlefield(  )
{
	TheDirector->WarpAllSurvivorsToBattlefield();
}

// Native object 0x00849A94; callback 0x002BD800; legacy_function.
static ConCommand warp_all_survivors_to_battlefield_command( "warp_all_survivors_to_battlefield", CC_warp_all_survivors_to_battlefield, "Warp all Survivors to the battlefield", 0x00004004 );

// Native callback 0x002BD8C0 (legacy_function).
static void CC_warp_all_survivors_to_checkpoint(  )
{
	TheDirector->WarpAllSurvivorsToCheckpoint();
}

// Native object 0x00849AB8; callback 0x002BD8C0; legacy_function.
static ConCommand warp_all_survivors_to_checkpoint_command( "warp_all_survivors_to_checkpoint", CC_warp_all_survivors_to_checkpoint, "Warp all Survivors to the exit checkpoint", 0x00004004 );

// Native callback 0x002BD7B0 (legacy_function).
static void CC_warp_all_survivors_to_finale(  )
{
	TheDirector->WarpAllSurvivorsToFinale();
}

// Native object 0x00849A70; callback 0x002BD7B0; legacy_function.
static ConCommand warp_all_survivors_to_finale_command( "warp_all_survivors_to_finale", CC_warp_all_survivors_to_finale, "Warp all Survivors to the finale radio", 0x00004004 );

// Native callback 0x002BD900 (command_function).
static void CC_warp_all_survivors_here( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 10000.0f,
		33636875u, player, COLLISION_GROUP_NONE, &trace );
	if ( trace.fraction == 1.0f ) return;
	TerrorNavArea *area = ToTerrorNavArea( TheNavMesh->GetNearestNavArea( trace.endpos, false, 10000.0f, false, true, 0 ) );
	if ( !area ) return;
	WarpSurvivorToArea warp( area );
	ForEachPlayer( warp );
}

// Native object 0x00849ADC; callback 0x002BD900; command_function.
static ConCommand warp_all_survivors_here_command( "warp_all_survivors_here", CC_warp_all_survivors_here, "Warp all survivors to the player's cursor", 0x00004004 );

// Native callback 0x002BD3A0 (command_function).
static void CC_warp_far_survivor_here( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	CTeam *team = GetGlobalTeam( TEAM_SURVIVOR );
	if ( !team ) return; // Native missing team dereference: ledger306.
	CTerrorPlayer *farthest = NULL;
	float maximum = 0.0f;
	for ( int i = 0; i < team->GetNumPlayers(); ++i )
	{
		CBasePlayer *base = team->GetPlayer( i );
		if ( !base || !base->IsPlayer() ) continue; // Native clears invalid slot then rereads: ledger306.
		CTerrorPlayer *candidate = ToTerrorPlayer( base );
		if ( candidate == player || !candidate->IsAlive() ) continue;
		const Vector delta = player->GetAbsOrigin() - candidate->GetAbsOrigin();
		const float squared = ( delta.y * delta.y + delta.z * delta.z ) + delta.x * delta.x;
		const float distance = _mm_cvtss_f32( _mm_sqrt_ss( _mm_set_ss( squared ) ) );
		if ( distance > maximum ) { farthest = candidate; maximum = distance; }
	}
	if ( !farthest ) return;
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 999999.88f,
		33579073u, player, COLLISION_GROUP_NONE, &trace );
	if ( !( trace.fraction < 1.0f ) && !trace.allsolid && !trace.startsolid ) return;
	Vector position = trace.endpos + Vector( 0, 0, 10.0f );
	farthest->Teleport( &position, &vec3_angle, &vec3_origin );
	INextBot *bot = dynamic_cast< INextBot * >( farthest );
	if ( bot ) bot->Reset();
	if ( farthest->IsIncapacitated() ) farthest->OnRevived();
}

// Native object 0x00849B00; callback 0x002BD3A0; command_function.
static ConCommand warp_far_survivor_here_command( "warp_far_survivor_here", CC_warp_far_survivor_here, "Teleport the farthest away Survivor to your cursor position", 0x00004004 );

// Native callback 0x002BD310 (command_function).
static void CC_warp_to_start_area( const CCommand &args )
{
	CTerrorPlayer *player = ToTerrorPlayer( UTIL_GetCommandClient() );
	if ( !player || !player->IsPlayer() ) return;
	Vector position;
	QAngle angles;
	TerrorNavArea *area = NULL;
	if ( TerrorGameRules()->GetPlayerSpawnPosition( player->GetSurvivorCharacter(), &position, &angles, &area ) && area )
	{
		player->Teleport( &position, &angles, NULL );
		DevMsg( "Start area is %d\n", area->GetID() );
	}
	else Warning( "warp_to_start_area: ERROR - Cannot determine start area\n" );
}

// Native object 0x00849B24; callback 0x002BD310; command_function.
static ConCommand warp_to_start_area_command( "warp_to_start_area", CC_warp_to_start_area, "Warp yourself to a survivor spawn area", 0x00004004 );

#endif
