// Transplant into existing src/game/server/props.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native802828 is read by the ordinary phys_shoot callback at19616F.
// Ledger294 retains this real console variable despite its Physics2 name;
// no Physics2 provider, prop factory, or excluded phys2_shoot command is added.
static ConVar phys2_shoot_speed( "phys2_shoot_speed", "250", 0x00000006, "" );

// Native callback 0x00195DB0 (command_function).
static void CC_collect_entity_model_name( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	FileHandle_t file = filesystem->Open( "CollectedEntities.txt", "at" );
	if ( file == FILESYSTEM_INVALID_HANDLE ) return;
	if ( player ) filesystem->FPrintf( file, "%s\n", STRING( player->GetModelName() ) );
	filesystem->Close( file );
}

// Native object 0x008028BC; callback 0x00195DB0; command_function.
static ConCommand collect_entity_model_name_command( "collect_entity_model_name", CC_collect_entity_model_name, "Collect model names of the entities you're pointing at", 0x00004004 );

// Native callback 0x00196080 (command_function).
#pragma float_control( precise, on, push )
static void CC_phys_shoot( const CCommand &args )
{
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player ) return;
	Vector forward;
	player->EyeVectors( &forward );
	trace_t trace;
	UTIL_TraceLine( player->EyePosition(), player->EyePosition() + forward * 100.0f,
		33702411u, player, COLLISION_GROUP_NONE, &trace );
	float speed = phys2_shoot_speed.GetFloat();
	const char *model = "models/props_plants/claypot03.mdl";
	for ( int i = 1; i < args.ArgC(); ++i )
	{
		if ( !Q_strcmp( args[i], "0" ) ) speed = 0.0f;
		else
		{
			const double numeric = V_atod( args[i] );
			const float stored = static_cast< float >( numeric ); // Native FSTdword, no pop before FUCOMIP.
			if ( numeric == 0.0 ) { model = args[i]; DevMsg( "Using model \"%s\"\n", model ); }
			else speed = stored;
		}
	}
	Vector origin = trace.endpos;
	if ( trace.fraction != 1.0f ) origin.z += 24.0f;
	CBaseEntity *prop = CBaseEntity::CreateNoSpawn( "prop_physics", origin, vec3_angle, NULL );
	if ( !prop ) return; // Native failed-create dereference: ledger306.
	prop->SetModelName( model && *model ? AllocPooledString( model ) : NULL_STRING );
	// Native slots104/108/96 are Precache/Activate/Spawn, in that order.
	prop->Precache();
	prop->Activate();
	prop->AddSpawnFlags( SF_PHYSPROP_FORCE_SERVER_SIDE ); // Native m_spawnflags308, NOT m_fEffects204.
	prop->Spawn();
	IPhysicsObject *physics = prop->VPhysicsGetObject();
	if ( physics )
	{
		const Vector velocity = forward * speed;
		physics->AddVelocity( &velocity, NULL );
		const Vector torque( 0, 0, phys_shoot_torque.GetFloat() );
		physics->ApplyTorqueCenter( torque );
	}
}

#pragma float_control( pop )

// Native object 0x00802898; callback 0x00196080; command_function.
static ConCommand phys_shoot_command( "phys_shoot", CC_phys_shoot, "Shoots a phys object.", 0x00004004 );

#endif
