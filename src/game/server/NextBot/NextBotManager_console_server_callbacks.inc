// Transplant into existing src/game/server/NextBot/NextBotManager.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Native8768B0 occupies260 bytes; the next registered object starts8769B4.
static char s_StuckDumpFilename[260] = "";
static bool s_StuckDumpFilenameTerminated = true;
static CountdownTimer s_StuckDumpDelay;

// S4AAAF0's real game-side record and renderer; no surrogate log registry.
struct ConsoleStuckRecord
{
 char identifier[256];
 float duration;
 int entindex;
 Vector origin;
 QAngle angles;
 char behavior[1024];
 Vector approaching;
 float time;
};
static void DrawConsoleStuckDump()
{
 if ( engine->IsPaused() ) { s_StuckDumpDelay.Start( 1.0f ); return; }
 if ( !s_StuckDumpDelay.IsElapsed() || !s_StuckDumpFilename[0] ) return;
 if ( !s_StuckDumpFilenameTerminated )
 {
  // Ledger306: native overflow stores past260 bytes are omitted by the writer.
  // Skip only the now-unterminated filename's dependent open/diagnostic use.
  s_StuckDumpFilename[0] = 0;
  return;
 }
 FileHandle_t file = filesystem->Open( s_StuckDumpFilename, "r", NULL );
 if ( file == FILESYSTEM_INVALID_HANDLE )
 {
  Msg( "Cannot open stuck dump file '%s'\n", s_StuckDumpFilename );
  s_StuckDumpFilename[0] = 0;
  return;
 }
 s_StuckDumpFilename[0] = 0;
 ConsoleStuckRecord record;
 unsigned int initialized = 0; // Only avoids native malformed-record uninitialized reads (ledger306).
 bool header = true;
 char line[256];
 while ( !filesystem->EndOfFile( file ) )
 {
  filesystem->ReadLine( line, sizeof( line ), file );
  char *token = strtok( line, " :" );
  if ( !token ) continue;
  if ( header )
  {
   if ( !strcmp( token, "STUCK" ) )
   {
    char *identifier = strtok( NULL, " \n" );
    if ( identifier ) { Q_strcpy( record.identifier, identifier ); initialized |= 1; }
    strtok( NULL, " " );
    char *duration = strtok( NULL, " \n" );
    if ( duration ) { record.duration = atof( duration ); initialized |= 2; }
    header = false;
   }
  }
  else if ( !strcmp( token, "Entindex" ) )
  {
   char *value = strtok( NULL, " :\n" );
   if ( value ) { record.entindex = atoi( value ); initialized |= 4; }
  }
  else if ( !strcmp( token, "Behavior" ) )
  {
   char *value = strtok( NULL, "\n" );
   if ( value ) { Q_strcpy( record.behavior, value ); initialized |= 8; }
  }
  else if ( !strcmp( token, "Origin" ) || !strcmp( token, "Approaching" ) || !strcmp( token, "Angles" ) )
  {
   const bool origin = !strcmp( token, "Origin" );
   const bool angles = !strcmp( token, "Angles" );
   char *x = strtok( NULL, " :\n" );
   char *y = strtok( NULL, " :\n" );
   char *z = strtok( NULL, " :\n" );
   if ( x && y && z )
   {
    if ( angles )
    {
     // Native parser stores first token at284, then276,280: preserve that order.
     record.angles.z = atof( x ); record.angles.x = atof( y ); record.angles.y = atof( z );
    }
    else
    {
     Vector &position = origin ? record.origin : record.approaching;
     position.x = atof( x ); position.y = atof( y ); position.z = atof( z );
     initialized |= origin ? 16 : 32;
    }
   }
  }
  else if ( !strcmp( token, "Time" ) )
  {
   char *value = strtok( NULL, " :\n" );
   if ( value ) { record.time = atof( value ); initialized |= 64; }
   header = true;
   if ( !( initialized & 64 ) ) continue;
   if ( nb_stuck_dump_filter_time_start.GetFloat() > record.time || record.time > nb_stuck_dump_filter_time_end.GetFloat() ) continue;
   const int entindex = nb_stuck_dump_filter_entindex.GetInt();
   if ( entindex > 0 && ( !( initialized & 4 ) || record.entindex != entindex ) ) continue;
   if ( !( initialized & 2 ) || nb_stuck_dump_filter_min_duration.GetFloat() > record.duration ) continue;
   const char *identifier = nb_stuck_dump_filter_identifier.GetString();
   if ( *identifier && ( !( initialized & 1 ) || Q_strnicmp( identifier, record.identifier, Q_strlen( identifier ) ) ) ) continue;
   if ( ( initialized & ( 16 | 32 ) ) != ( 16 | 32 ) ) continue;
   if ( record.approaching.IsZero( 0.01f ) ) record.approaching = record.origin;
   NDebugOverlay::HorzArrow( record.origin, record.approaching, 3.0f, 255, 100, 0, 255, true, 9999.9f );
   if ( nb_stuck_dump_show_details.GetBool() && ( initialized & ( 1 | 8 ) ) == ( 1 | 8 ) )
    NDebugOverlay::Text( record.origin, CFmtStr( "%s:%s", record.identifier, record.behavior ), false, 9999.9f );
  }
 }
 filesystem->Close( file );
}

// Native completion 0x004AA6D0.
static int Complete_RVA004AA6D0( const char *partial, char commands[64][64] )
{
	const int commandLength = Q_strlen( "nb_stuck_dump_display " );
	const bool validPrefix = Q_strlen( partial ) >= commandLength;
	const char *prefix = validPrefix ? partial + commandLength : NULL;
	const int prefixLength = prefix ? Q_strlen( prefix ) : 0;
	FileFindHandle_t find;
	int count = 0;
	for ( const char *name = filesystem->FindFirst( "logs/*.stuck", &find ); name; name = filesystem->FindNext( find ) )
	{
		if ( count >= 64 ) break;
		// Ledger306: native short input points past its string; skip only dependent match/output.
		if ( prefix && !Q_strnicmp( name, prefix, prefixLength ) )
			Q_snprintf( commands[count++], 64, "nb_stuck_dump_display logs/%s", name );
	}
	// Native4AA6D0 intentionally has no FindClose, including early64 termination.
	return count;
}

// Native callback 0x004AC730 (command_function).
static void CC_nb_assault( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	struct Assault
	{
		bool operator()( INextBot *bot )
		{
			CBaseCombatCharacter *entity = bot->GetEntity();
			if ( entity->GetTeamNumber() == TEAM_INFECTED && entity->IsAlive() ) bot->OnCommandAssault();
			return true;
		}
	} assault;
	TheNextBots().ForEachBot( assault );
}

// Native object 0x00876D5C; callback 0x004AC730; command_function.
static ConCommand nb_assault_command( "nb_assault", CC_nb_assault, "Tell all NextBots to assault", 0x00004004 );

// Native callback 0x004AC6E0 (command_function).
static void CC_nb_rush( const CCommand &args )
{
	CTerrorPlayer *victim = PickLivingSurvivorForForceStart();
	if ( !victim ) return;
	CollectITMob collect( victim, 10000.0f );
	TheNextBots().ForEachBot( collect );
}

// Native object 0x00876D38; callback 0x004AC6E0; command_function.
static ConCommand nb_rush_command( "nb_rush", CC_nb_rush, "Causes all infected to rush the survivors.", 0x00004004 );

// Native callback 0x004A9FA0 (command_function).
static void CC_nb_move_to_position( const CCommand &args )
{
	if ( !UTIL_IsCommandIssuedByServerAdmin() ) return;
	CBasePlayer *player = UTIL_GetCommandClient();
	if ( !player || !player->IsPlayer() ) return;
	if ( args.ArgC() <= 1 )
	{
		ClientPrint( player, HUD_PRINTCONSOLE, "Please specify coordinates: x y z\n" );
		return;
	}
	Vector position( atof( args[1] ), args.ArgC() > 2 ? atof( args[2] ) : 0.0f, args.ArgC() > 3 ? atof( args[3] ) : 0.0f );
	NDebugOverlay::Cross3D( position, 5.0f, 0, 255, 0, true, 10.0f );
	struct MoveToPosition
	{
		explicit MoveToPosition( const Vector &goal ) : m_goal( goal ) {}
		bool operator()( INextBot *bot )
		{
			if ( TheNextBots().IsDebugFilterMatch( bot ) ) bot->OnCommandApproach( m_goal, 0.0f );
			return true;
		}
		Vector m_goal;
	} move( position );
	TheNextBots().ForEachBot( move );
	ClientPrint( player, HUD_PRINTCONSOLE, UTIL_VarArgs( "Instructed NextBots to move to %f, %f, %f.\n", position.x, position.y, position.z ) );
}

// Native object 0x00876730; callback 0x004A9FA0; command_function.
static ConCommand nb_move_to_position_command( "nb_move_to_position", CC_nb_move_to_position, "Tell all NextBots to move to the specified absolute position", 0x00004004 );

// Native callback 0x004AA670 (command_function).
static void CC_nb_stuck_dump_display( const CCommand &args )
{
	if ( args.ArgC() == 2 )
	{
		const int length = Q_strlen( args[1] ) + 1;
		s_StuckDumpFilenameTerminated = length <= sizeof( s_StuckDumpFilename );
		// Native Q_strcpy writes are retained up to the real destination extent.
		// Ledger306 omits only overflow stores, not a repaired/truncated filename.
		Q_memcpy( s_StuckDumpFilename, args[1], MIN( length, static_cast< int >( sizeof( s_StuckDumpFilename ) ) ) );
	}
	else Msg( "Usage: %s <stuck filename>\n", args[0] );
}

// Native object 0x00876DA4; callback 0x004AA670; command_function.
static ConCommand nb_stuck_dump_display_command( "nb_stuck_dump_display", CC_nb_stuck_dump_display, "Given a bot stuck dump filename, display the stuck locations in the environment", 0x00004004, Complete_RVA004AA6D0 );

#endif
