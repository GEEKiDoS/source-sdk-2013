// Compose into shared/soundemittersystem.cpp after g_SoundEmitterSystem.
// Guard CLIENT_DLL && L4D2; C198550, C198B20, C199D70, completions C198670/C198740.
static void CC_ClientSoundEmitterReload( const CCommand & )
{
	soundemitterbase->ModShutdown();
	soundemitterbase->ModShutdown();
	g_pClosecaption = cvar->FindVar( "closecaption" );
	soundemitterbase->ModInit();
	soundemitterbase->ModInit();
}
static int ClientSoundNameCompletion( const char *partial, char commands[64][64], const char *command, int prefixLength )
{
	const char *match = NULL;
	int matchLength = 0;
	if ( Q_strstr( partial, command ) && Q_strlen( partial ) > prefixLength )
	{
		match = partial + prefixLength;
		matchLength = Q_strlen( match );
	}
	int count = 0;
	for ( int i = soundemitterbase->GetSoundCount() - 1; i >= 0 && count < 64; --i )
	{
		const char *name = soundemitterbase->GetSoundName( i );
		if ( name && ( !match || !Q_strnicmp( name, match, matchLength ) ) )
			V_snprintf( commands[count++], 64, "%s %s", command, name );
	}
	return count;
}
static int ClientPlaySoundsCompletion( const char *partial, char commands[64][64] )
{
	return ClientSoundNameCompletion( partial, commands, "snd_playsounds", 15 );
}
static int ClientSetSoundParamCompletion( const char *partial, char commands[64][64] )
{
	return ClientSoundNameCompletion( partial, commands, "snd_setsoundparam", 18 );
}
static void CC_ClientSetSoundParam( const CCommand &args )
{
	if ( args.ArgC() != 4 )
	{
		DevMsg( "Parameters: mix group name, [vol, mute, solo], value" );
		return;
	}
	const int index = soundemitterbase->GetSoundIndex( args[1] );
	if ( !soundemitterbase->IsValidIndex( index ) ) return;
	CSoundParametersInternal *old = soundemitterbase->InternalGetParametersForSound( index );
	if ( !old ) return;
	CSoundParametersInternal parameters;
	parameters.CopyFrom( *old );
	if ( !Q_stricmp( "volume", args[2] ) ) parameters.VolumeFromString( args[3] );
	else if ( !Q_stricmp( "level", args[2] ) ) parameters.SoundLevelFromString( args[3] );
	if ( !( parameters == *old ) ) soundemitterbase->UpdateSoundParameters( args[1], parameters );
}
static void CC_ClientPlaySounds( const CCommand &args )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( !player )
	{
		Msg( "Can't play until a game is started.\n" );
		return;
	}
	if ( args.ArgC() <= 4 )
	{
		player->EmitSound( args[1] );
		return;
	}
	Vector position( atof( args[2] ), atof( args[3] ), atof( args[4] ) );
	const bool absQueriesValid = CBaseEntity::IsAbsQueriesValid();
	CBaseEntity::SetAbsQueriesValid( true );
	{
		CPASAttenuationFilter filter( player );
		EmitSound_t parameters;
		parameters.m_pSoundName = args[1];
		parameters.m_pOrigin = &position;
		parameters.m_flVolume = 0.0f;
		parameters.m_nPitch = 0;
		g_SoundEmitterSystem.EmitSound( filter, 0, parameters );
	}
	CBaseEntity::SetAbsQueriesValid( absQueriesValid );
}
