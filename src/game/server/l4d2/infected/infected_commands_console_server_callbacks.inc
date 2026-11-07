// Transplant into existing src/game/server/l4d2/infected_commands.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x0035FD10 (command_function).
static void CC_z_spawn_old( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	const float height = z_spawn_height.GetFloat();
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 10000.0f,
		33702411u, player, COLLISION_GROUP_NONE, &trace );
	const float distance = z_spawn_const_distance.GetFloat();
	if ( distance >= 0.0f )
	{
		trace.endpos = player->EyePosition() + forward * distance;
		trace.fraction = z_spawn_const_distance.GetFloat() * 0.0001f;
	}
	if ( trace.fraction == 1.0f ) return;
	const char *name = "Common";
	int zombieClass = 0, wound = -1;
	if ( args.ArgC() >= 2 )
	{
		name = args[1];
		if ( !Q_stricmp( name, "mob" ) ) { TheZombieManager->SpawnMob( TheDirector->GetCommonLimit() ); return; }
		zombieClass = ZombieClassFromName( name );
		if ( args.ArgC() >= 4 && !Q_stricmp( args[2], "wound" ) ) wound = atoi( args[3] );
	}
	bool automatic = false, ragdoll = false, inArea = false;
	for ( int i = 2; i < args.ArgC(); ++i )
	{
		automatic = automatic || !Q_stricmp( args[i], "auto" );
		ragdoll = ragdoll || !Q_stricmp( args[i], "ragdoll" );
		inArea = inArea || !Q_stricmp( args[i], "area" );
	}
	Vector position = trace.endpos;
	position.z += height;
	QAngle angles( 0, RandomFloat( 0.0f, 360.0f ), 0 );
	if ( g_bZombieSpawnPositionOverride ) position = g_vecZombieSpawnPosition;
	if ( g_bZombieSpawnAngleOverride ) angles.Init( 0.0f, g_flZombieSpawnAngle, 0.0f );
	if ( automatic && !TheZombieManager->GetRandomPZSpawnPosition( zombieClass, 5, NULL, &position ) )
	{
		Warning( "Couldn't find a %s Spawn position in %d tries\n", name, 5 );
		return;
	}
	if ( !Q_stricmp( name, "witch_bride" ) ) zombieClass = 11;

	// Native35FD10 inlines this tail instead of calling35AF70. Keep it here:
	// old captures height BEFORE parser/automatic lookup; bride ignores ragdoll.
	const Vector &hullMax = g_pGameRules->GetViewVectors()->m_vHullMax;
	const Vector &hullMin = g_pGameRules->GetViewVectors()->m_vHullMin;
	UTIL_TraceHull( position, position, hullMin, hullMax, s_SpawnSolidMask, NULL, COLLISION_GROUP_NONE, &trace );
	TerrorNavArea *area = FindCommandSpawnArea( position );
	if ( ( trace.startsolid || trace.fraction < 1.0f ) && area )
	{
		position = area->GetRandomSpawnPoint();
		position.z += height;
	}
	if ( zombieClass == 7 )
	{
		if ( ragdoll ) Warning( "ragdoll flag is not implemented\n" );
		g_bZombieSpawnBypass = true;
		TheZombieManager->SpawnWitch( position, angles );
		g_bZombieSpawnBypass = false;
		return;
	}
	if ( zombieClass == 11 )
	{
		// Unlike35AF70, original old bride branch does NOT print a ragdoll warning.
		g_bZombieSpawnBypass = true;
		TheZombieManager->SpawnWitchBride( position, angles );
		g_bZombieSpawnBypass = false;
		return;
	}
	if ( zombieClass == 8 || ( zombieClass >= 1 && zombieClass <= 6 ) )
	{
		if ( ragdoll ) Warning( "ragdoll flag is not implemented" );
		CFindCommandInfectedPlayer findPlayer( zombieClass );
		ForEachPlayer( findPlayer );
		CTerrorPlayer *reuse = findPlayer.m_player;
		if ( reuse ) reuse->State_Transition( STATE_OBSERVER_MODE );
		if ( !reuse || ( !z_debug_tank_spawn.GetBool() && zombieClass == 8 ) || tank_raffle_debug.GetBool() )
		{
			g_bZombieSpawnBypass = true;
			if ( zombieClass == 8 )
			{
				CTerrorPlayer *spawned = area && inArea ? TheZombieManager->SpawnTankInArea( area, angles, false ) : TheZombieManager->SpawnTank( position, angles );
				TheDirector->OfferTank( spawned, true );
			}
			else if ( area && inArea ) TheZombieManager->SpawnSpecialInArea( zombieClass, area, angles );
			else TheZombieManager->SpawnSpecial( zombieClass, position, angles );
			g_bZombieSpawnBypass = false;
			return;
		}
		reuse->State_Transition( STATE_ACTIVE );
		reuse->SetAbsOrigin( position );
		reuse->SetAbsAngles( angles );
		TheZombieManager->SetSpecialSpawnPosition( reuse->GetAbsOrigin() );
		g_iNextZombieClass = zombieClass;
		reuse->m_bReusingInfectedSpawnState = true;
		reuse->m_reusedInfectedSpawnHealth = -1;
		reuse->m_reusedInfectedRecharge = -1.0f;
		reuse->Spawn(); // Proven native CTerrorPlayer slot96 ->3269D0.
		if ( z_spawn_health.GetInt() > 0 ) reuse->SetHealth( z_spawn_health.GetInt() );
		reuse->Teleport( &position, &angles, &vec3_origin );
		TheZombieManager->ClearSpecialSpawnPosition();
		return;
	}
	g_bZombieSpawnBypass = true;
	Infected *infected = TheZombieManager->SpawnCommonZombie( area, position, ZombieManager::WANDERER );
	if ( !infected )
	{
		area = FindCommandSpawnArea( position );
		if ( area )
		{
			position = area->GetRandomSpawnPoint();
			position.z += height;
			TheZombieManager->SpawnCommonZombie( area, position, ZombieManager::WANDERER );
		}
		g_bZombieSpawnBypass = false;
		return; // Original retry never applies wound/ragdoll, even on success.
	}
	g_bZombieSpawnBypass = false;
	if ( wound >= 0 )
	{
		Infected *actor = infected->MyInfectedPointer();
		if ( actor ) { actor->m_gibbedLimbForce = vec3_origin; actor->SetRequestedWound( wound ); }
	}
	if ( ragdoll )
	{
		CTakeDamageInfo info( infected, infected, 10000.0f, DMG_CRUSH );
		infected->BecomeRagdoll( info, Vector( 0.0f, 0.0f, 1.0f ) );
	}
}

// Native object 0x0085E8E8; callback 0x0035FD10; command_function.
static ConCommand z_spawn_old_command( "z_spawn_old", CC_z_spawn_old, "[OLD VERSION] <tank|boomer|smoker|witch|hunter|spitter|jockey|charger|mob|common> <auto> <ragdoll> <area>.  Spawns the specified zombie(s) under your cursor, or out in the world/in the targetted nav area if auto or area is specified.", 0x00004004 );

// Native callback 0x003FF7B0 (command_function).
static void CC_z_add( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	Vector forward;
	player->EyeVectors( &forward );
	const bool explicitPosition = args.ArgC() >= 4;
	Vector position;
	if ( explicitPosition ) position.Init( atof( args[1] ), atof( args[2] ), atof( args[3] ) );
	else
	{
		trace_t trace;
		UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 999999.88f,
		33579073u, player, COLLISION_GROUP_NONE, &trace );
		if ( !( trace.fraction < 1.0f ) && !trace.allsolid && !trace.startsolid ) return;
		position = trace.endpos;
	}
	CBaseEntity *entity = CreateEntityByName( "infected", -1, true );
	if ( !entity ) return;
	Vector direction = player->GetAbsOrigin() - position;
	const float length = static_cast< float >( sqrt( ( (double)direction.x * direction.x + (double)direction.y * direction.y ) + (double)direction.z * direction.z ) );
	const float scale = 1.0f / ( length + FLT_EPSILON );
	direction *= scale;
	QAngle angles;
	VectorAngles( direction, angles );
	entity->SetAbsAngles( angles );
	entity->SetAbsOrigin( position + Vector( 0, 0, 10.0f ) );
	UTIL_SetSize( entity, Vector( -13.0f, -13.0f, 0.0f ), Vector( 13.0f, 13.0f, 68.0f ) );
	if ( !explicitPosition ) FindPassableTerrorSpawnPosition( entity, entity->GetAbsOrigin(), &position, true, 33570827u, NULL, 0.0f );
	entity->SetAbsOrigin( position );
	DispatchSpawn( entity, true );
}

// Native object 0x0086FE10; callback 0x003FF7B0; command_function.
static ConCommand z_add_command( "z_add", CC_z_add, "Add an Infected - optional x y z arguments for world position to add them", 0x00004004 );

#endif
