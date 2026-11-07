// Compose into l4d2/terror_spawnmenus.cpp. C2E8FA0/C2E8FC0/C2E9CE0..2E9F0A.
static void OpenNativeSpawnMenu( const char *name )
{
	ConVarRef developer( "developer" );
	if ( developer.GetBool() ) static_cast< CBaseViewport * >( gViewPortInterface )->RecreatePanel( name );
	gViewPortInterface->ShowPanel( name, true );
}
static void CC_ChooseTeam( const CCommand & )
{
	if ( g_TerrorModes.IsPlayerControlledZombies() ) gViewPortInterface->ShowPanel( "team", true );
}
static void CC_SpectatorHelp( const CCommand & )
{
	if ( gViewPortInterface ) gViewPortInterface->ShowPanel( "info", true );
}
static void CC_SpectatorMenu( const CCommand &args )
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	if ( player && player->GetObserverMode() == OBS_MODE_NONE ) return;
	bool visible = args.ArgC() == 2 ? atoi( args[1] ) == 1 : true;
	if ( gViewPortInterface ) gViewPortInterface->ShowPanel( "specmenu", visible );
}

static void CC_spawnboomermenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_boomer" ); }

static void CC_spawnchargermenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_charger" ); }

static void CC_spawnhuntermenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_hunter" ); }

static void CC_spawnjockeymenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_jockey" ); }

static void CC_spawnmodemenu( const CCommand & ) { OpenNativeSpawnMenu( "spawnmode" ); }

static void CC_spawnsmokermenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_smoker" ); }

static void CC_spawnspittermenu( const CCommand & ) { OpenNativeSpawnMenu( "spawn_spitter" ); }
