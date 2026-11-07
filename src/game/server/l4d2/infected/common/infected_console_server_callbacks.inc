// Transplant into existing src/game/server/l4d2/infected.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native callback 0x00351CA0 (command_function).
static void CC_z_kill( const CCommand &args )
{
	extern CBaseEntity *FindPickerEntity( CBasePlayer *player );
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return; // Ledger306: skip native null-player picker dereference only.
	CBaseEntity *entity = FindPickerEntity( player );
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	CTakeDamageInfo info( actor, actor, static_cast< float >( actor->GetHealth() ) + 1.0f, 0x01000000 );
	actor->TakeDamage( info );
}

// Native object 0x0085C860; callback 0x00351CA0; command_function.
static ConCommand z_kill_command( "z_kill", CC_z_kill, "Kill the pointed-at common infected", 0x00004004 );

// Native callback 0x00351A50 (command_function).
static void CC_z_play_activity( const CCommand &args )
{
	extern CBaseEntity *FindPickerEntity( CBasePlayer *player );
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return; // Ledger306: skip native null-player picker dereference only.
	CBaseEntity *entity = FindPickerEntity( player );
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	const int activity = ActivityList_IndexForName( args[1] );
	if ( activity == -1 ) return;
	const int sequence = actor->SelectWeightedSequence( static_cast< Activity >( activity ) );
	if ( sequence != -1 ) actor->ResetSequence( sequence );
}

// Native object 0x0085C83C; callback 0x00351A50; command_function.
static ConCommand z_play_activity_command( "z_play_activity", CC_z_play_activity, "Play a specific activity on the pointed-at common infected", 0x00004004 );

// Native callback 0x00352330 (command_function).
static void CC_z_wound( const CCommand &args )
{
	extern CBaseEntity *FindPickerEntity( CBasePlayer *player );
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return; // Ledger306: skip native null-player picker dereference only.
	CBaseEntity *entity = FindPickerEntity( player );
	CBaseAnimating *base = entity ? entity->GetBaseAnimating() : NULL;
	if ( !base ) return;
	const int wound = atoi( args[1] );
	if ( wound < -1 ) return;
	Infected *actor = dynamic_cast< Infected * >( base );
	if ( !actor ) return; // Ledger306: keep parsing, skip only wrong-type native force/setter use.
	actor->m_gibbedLimbForce = vec3_origin;
	actor->SetRequestedWound( wound );
}

// Native object 0x0085C884; callback 0x00352330; command_function.
static ConCommand z_wound_command( "z_wound", CC_z_wound, "Cause a specified wound on the pointed-at common infected", 0x00004004 );

// Native callback 0x003FD9F0 (command_function).
static void CC_z_increment_head( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *, char * );
	CBaseEntity *entity = player ? FindPickerEntityClass( player, const_cast<char*>( "infected" ) ) : NULL;
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	MDLCACHE_CRITICAL_SECTION();
	const int group = actor->FindBodygroupByName( "Head" );
	if ( group < 0 || group >= actor->GetNumBodyGroups() ) return;
	const int old = actor->GetBodygroup( group );
	const int count = actor->GetBodygroupCount( group );
	if ( count != 0 ) actor->SetBodygroup( group, static_cast<int>( ( static_cast<unsigned int>( old ) + 1u ) ) % count );
}

// Native object 0x0086FCCC; callback 0x003FD9F0; command_function.
static ConCommand z_increment_head_command( "z_increment_head", CC_z_increment_head, "Switch to next head bodygroup", 0x00004004 );

// Native callback 0x003FDA90 (command_function).
static void CC_z_increment_lower( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *, char * );
	CBaseEntity *entity = player ? FindPickerEntityClass( player, const_cast<char*>( "infected" ) ) : NULL;
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	MDLCACHE_CRITICAL_SECTION();
	const int group = actor->FindBodygroupByName( "LowerBody" );
	if ( group < 0 || group >= actor->GetNumBodyGroups() ) return;
	const int old = actor->GetBodygroup( group );
	const int count = actor->GetBodygroupCount( group );
	if ( count != 0 ) actor->SetBodygroup( group, static_cast<int>( ( static_cast<unsigned int>( old ) + 1u ) ) % count );
}

// Native object 0x0086FDA4; callback 0x003FDA90; command_function.
static ConCommand z_increment_lower_command( "z_increment_lower", CC_z_increment_lower, "Switch to next lower body bodygroup", 0x00004004 );

// Native callback 0x003FDA40 (command_function).
static void CC_z_increment_upper( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *, char * );
	CBaseEntity *entity = player ? FindPickerEntityClass( player, const_cast<char*>( "infected" ) ) : NULL;
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	MDLCACHE_CRITICAL_SECTION();
	const int group = actor->FindBodygroupByName( "UpperBody" );
	if ( group < 0 || group >= actor->GetNumBodyGroups() ) return;
	const int old = actor->GetBodygroup( group );
	const int count = actor->GetBodygroupCount( group );
	if ( count != 0 ) actor->SetBodygroup( group, static_cast<int>( ( static_cast<unsigned int>( old ) + 1u ) ) % count );
}

// Native object 0x0086FD80; callback 0x003FDA40; command_function.
static ConCommand z_increment_upper_command( "z_increment_upper", CC_z_increment_upper, "Switch to next upper body bodygroup", 0x00004004 );

// Native callback 0x004011C0 (command_function).
static void CC_z_increment_skin( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	extern CBaseEntity *FindPickerEntityClass( CBasePlayer *, char * );
	CBaseEntity *entity = player ? FindPickerEntityClass( player, const_cast<char*>( "infected" ) ) : NULL;
	CBaseAnimating *actor = entity ? entity->GetBaseAnimating() : NULL;
	if ( !actor ) return;
	MDLCACHE_CRITICAL_SECTION();
	CStudioHdr *studio = actor->GetModelPtr();
	if ( studio && studio->numskinfamilies() != 0 )
		actor->m_nSkin = static_cast<int>( ( static_cast<unsigned int>( actor->m_nSkin ) + 1u ) ) % studio->numskinfamilies();
}

// Native object 0x0086FDC8; callback 0x004011C0; command_function.
static ConCommand z_increment_skin_command( "z_increment_skin", CC_z_increment_skin, "Switch to next skin", 0x00004004 );

// Native callback 0x00352010 (command_function).
static void CC_z_unwound_all( const CCommand &args )
{
	struct Unwound
	{
		bool operator()( INextBot *bot )
		{
			CBaseAnimating *actor = bot->GetEntity()->GetBaseAnimating();
			Infected *infected = dynamic_cast< Infected * >( actor );
			if ( infected ) infected->SetRequestedWound( -1 ); // Ledger306 skips only native wrong-type setter.
			return true;
		}
	} unwound;
	TheNextBots().ForEachBot( unwound );
}

// Native object 0x0085C8CC; callback 0x00352010; command_function.
static ConCommand z_unwound_all_command( "z_unwound_all", CC_z_unwound_all, "Clears all wounds from all infected", 0x00004004 );

// Native callback 0x00351FF0 (command_function).
static void CC_z_wound_all( const CCommand &args )
{
	struct Wound
	{
		bool operator()( INextBot *bot )
		{
			CBaseCombatCharacter *entity = bot->GetEntity();
			CBaseAnimating *actor = entity->GetBaseAnimating();
			if ( !actor ) return true;
			// Native gender/cutout lookup and RNG occur EVEN for non-Infected actors.
			const InfectedDamageCutoutSet *set = GetInfectedDamageCutouts( entity->GetGender() );
			const int wound = RandomInt( 0, set->m_Cutout.Count() - 1 );
			Infected *infected = dynamic_cast< Infected * >( actor );
			if ( infected ) infected->SetRequestedWound( wound ); // Ledger306 skips only wrong-type setter, not RNG.
			return true;
		}
	} wound;
	TheNextBots().ForEachBot( wound );
}

// Native object 0x0085C8A8; callback 0x00351FF0; command_function.
static ConCommand z_wound_all_command( "z_wound_all", CC_z_wound_all, "Cause a random wound on every common infected", 0x00004004 );

// Native callback 0x004056B0 (command_function).
static void CC_z_debug_path_stress_test( const CCommand &args )
{
	Infected *infected = dynamic_cast< Infected * >( gEntList.FindEntityByClassname( NULL, "infected" ) );
	const int count = args.ArgC() > 1 ? atoi( args[1] ) : 1000;
	if ( !infected ) return;
	Path path;
	InfectedPathCost cost( infected );
	CWorld *world = GetWorldEntity();
	if ( !world ) return;
	Vector mins, maxs;
	world->GetWorldBounds( mins, maxs );
	VPROF_BUDGET( "z_debug_path_stress_test", "z_debug_path_stress_test" );
	for ( int i = 0; i < count; ++i )
	{
		Vector destination;
		destination.x = RandomFloat( mins.x, maxs.x );
		destination.y = RandomFloat( mins.y, maxs.y );
		destination.z = RandomFloat( mins.z, maxs.z );
		path.Compute( infected, destination, cost, 0.0f );
	}
}

// Native object 0x0086FC3C; callback 0x004056B0; command_function.
static ConCommand z_debug_path_stress_test_command( "z_debug_path_stress_test", CC_z_debug_path_stress_test, "Stress-tests Path::Compute() on an Infected. Optional argument is number of times to invoke Compute()", 0x00004004 );

// Native callback 0x003FE870 (command_function).
static void CC_z_decals( const CCommand &args )
{
	Infected *infected[128];
	int count = 0;
	for ( CBaseEntity *entity = gEntList.FirstEnt(); entity; entity = gEntList.NextEnt( entity ) )
	{
		Infected *actor = dynamic_cast< Infected * >( entity );
		if ( !actor ) continue;
		if ( count < ARRAYSIZE( infected ) ) infected[count] = actor;
		++count; // Native writes beyond its fixed128 buffer: ledger306 skips only the invalid stores.
	}
	if ( count < 1 ) Msg( "No infected to decal.\n" );
	int painted = 0;
	while ( painted < 25 )
	{
		const int selected = RandomInt( 0, count - 1 );
		// Preserve even native reversed-bound RNG calls; only uninitialized/out-of-bounds array reads are skipped.
		if ( selected < 0 || selected >= count || selected >= ARRAYSIZE( infected ) ) { ++painted; continue; }
		Infected *actor = infected[selected];
		if ( actor )
		{
			Vector mins, maxs;
			actor->CollisionProp()->WorldSpaceAABB( &mins, &maxs );
			Vector point;
			point.x = RandomFloat( mins.x, maxs.x );
			point.y = RandomFloat( mins.y, maxs.y );
			point.z = RandomFloat( mins.z, maxs.z );
			QAngle angles;
			angles.x = static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f * 180.0f - 90.0f;
			angles.y = static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f * 180.0f - 90.0f;
			angles.z = static_cast< float >( RandomInt( 0, 32767 ) ) * 0.000030518509f * 180.0f - 90.0f;
			Vector direction;
			AngleVectors( angles, &direction );
			Ray_t ray;
			const Vector start = point - direction * 300.0f;
			const Vector end = start + direction * 600.0f;
			ray.Init( start, end );
			trace_t trace;
			enginetrace->ClipRayToCollideable( ray, MASK_ALL, actor->GetCollideable(), &trace );
			if ( trace.fraction == 1.0f ) continue; // Native doesn't increment the25 counter on a miss.
			UTIL_ImpactTrace( &trace, 64, NULL );
		}
		++painted;
	}
	Msg( "%d decals painted on %d zombies.\n", painted, count );
}

// Native object 0x0086FDEC; callback 0x003FE870; command_function.
static ConCommand z_decals_command( "z_decals", CC_z_decals, "Splat decals on all infected", 0x00004004 );

#endif
