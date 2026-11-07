// Transplant into existing src/game/shared/SoundEmitterSystem.cpp. Not a standalone compiled TU.
// Merge required includes before memdbgon; cvar_fragments definitions precede this payload.
#if defined( L4D2 ) && !defined( CLIENT_DLL )

// Original1D6380/091C90 server dictionary state. Server never requests caption
// blocks here; retain the actual sorted lookup/header/filename reader state,
// without fabricating an asynchronous block service.
struct CServerCaptionDictionary
{
 CServerCaptionDictionary() { Q_memset( &m_Header, 0, sizeof( m_Header ) ); }
 CUtlSortVector< CaptionLookup_t, CCaptionLookupLess > m_Directory;
 CompiledCaptionHeader_t m_Header;
 CUtlSymbol m_Filename;
};
static CUtlVector< CServerCaptionDictionary > s_ServerCaptionDictionaries;

static bool LoadServerCaptionDictionary( CServerCaptionDictionary &entry, const char *fullpath )
{
 FileHandle_t file = filesystem->Open( fullpath, "rb", NULL );
 if ( file == FILESYSTEM_INVALID_HANDLE ) return false;
 CUtlBuffer buffer;
 filesystem->Read( &entry.m_Header, sizeof( entry.m_Header ), file );
 if ( entry.m_Header.magic != COMPILED_CAPTION_FILEID )
 {
  Warning( "Invalid file id for %s\n", fullpath );
  return false; // Original091C90 doesn't close the invalid-header handle.
 }
 if ( entry.m_Header.version != COMPILED_CAPTION_VERSION )
 {
  Warning( "Invalid file version for %s\n", fullpath );
  return false; // Preserve the same native handle leak.
 }
 if ( static_cast< unsigned int >( entry.m_Header.directorysize ) > 64u * 1024u )
 {
  Warning( "Invalid directory size %d for %s\n", entry.m_Header.directorysize, fullpath );
  return false; // Same native invalid-header branch, not SDK's fatal Error().
 }
 const int bytes = entry.m_Header.directorysize * sizeof( CaptionLookup_t );
 entry.m_Directory.EnsureCapacity( entry.m_Header.directorysize );
 buffer.EnsureCapacity( bytes );
 const int readBytes = filesystem->Read( buffer.Base(), bytes, file );
 filesystem->Close( file );
 if ( readBytes == bytes )
 {
  entry.m_Directory.CopyArray( static_cast< const CaptionLookup_t * >( buffer.PeekGet() ), entry.m_Header.directorysize );
  entry.m_Directory.RedoSort( true );
 }
 // Native copies uninitialized directory bytes on a short read. Ledger306:
 // omit only that dependent copy/sort; retain native success, filename and log.
 entry.m_Filename = fullpath;
 return true;
}
static void AddServerCaptionFile( const char *filename )
{
 const int required = filesystem->GetSearchPath( "GAME", true, NULL, 0 );
 if ( required <= 0 ) return; // Native strtok on uninitialized zero-length search buffer: ledger306.
 char *paths = static_cast< char * >( stackalloc( required + 1 ) );
 filesystem->GetSearchPath( "GAME", true, paths, required );
 for ( char *path = strtok( paths, ";" ); path; path = strtok( NULL, ";" ) )
 {
  char fullpath[260];
  Q_snprintf( fullpath, sizeof( fullpath ), "%s%s", path, filename );
  Q_FixSlashes( fullpath, '\\' );
  Q_strlower( fullpath );
  const int index = s_ServerCaptionDictionaries.AddToTail();
  if ( LoadServerCaptionDictionary( s_ServerCaptionDictionaries[index], fullpath ) )
   DevMsg( "Server: added caption file: %s\n", fullpath );
  else s_ServerCaptionDictionaries.Remove( index );
 }
}

// Native1D5E20: the SAME directories reloaded by sv_soundemitter_reload are
// consulted before actual dispatch. Empty entries (length<=2) suppress captions.
static CUtlRBTree< CRC32_t, unsigned short > s_ServerMissingCaptionHashes( 0, 0, DefLessFunc( CRC32_t ) );
static CUtlMap< CRC32_t, float, unsigned short > s_ServerCaptionRepeat( DefLessFunc( CRC32_t ) );

static void ReloadServerCaptionDictionaries()
{
 s_ServerCaptionDictionaries.Purge();
 AddServerCaptionFile( "resource/closecaption_english.dat" );
 AddServerCaptionFile( "resource/subtitles_english.dat" );
}

bool FindServerCaptionHash( const char *token, bool warn, CRC32_t *hash )
{
 CaptionLookup_t search;
 search.SetHash( token );
 *hash = search.hash;
 for ( int i = 0; i < s_ServerCaptionDictionaries.Count(); ++i )
 {
  CServerCaptionDictionary &dictionary = s_ServerCaptionDictionaries[i];
  const int index = dictionary.m_Directory.Find( search );
  if ( index != dictionary.m_Directory.InvalidIndex() )
   return dictionary.m_Directory[index].length > 2;
 }
 if ( warn && cc_showmissing.GetBool() && s_ServerMissingCaptionHashes.Find( *hash ) == s_ServerMissingCaptionHashes.InvalidIndex() )
 {
  s_ServerMissingCaptionHashes.Insert( *hash );
  Msg( "Missing caption for %s\n", token );
 }
 return false;
}

// Native1D6090 COMISS/JBE keeps unordered cutoff/time entries; remove only
// cutoff>last. Native1D62E0 rejects unordered elapsed/interval just like JBE.
// Native1D62E0 reads CGlobalVarsBase::realtime at offset0, NOT curtime at+12.
#pragma float_control( precise, on, push )
bool AllowServerCaptionRepeat( CRC32_t hash )
{
 if ( gpGlobals->maxClients == 1 ) return true;
 const float now = gpGlobals->realtime;
 const float cutoff = now - cc_norepeat.GetFloat();
 for ( unsigned short i = s_ServerCaptionRepeat.FirstInorder(); i != s_ServerCaptionRepeat.InvalidIndex(); )
 {
  const unsigned short next = s_ServerCaptionRepeat.NextInorder( i );
  if ( cutoff > s_ServerCaptionRepeat[i] ) s_ServerCaptionRepeat.RemoveAt( i );
  i = next;
 }
 const unsigned short found = s_ServerCaptionRepeat.Find( hash );
 if ( found == s_ServerCaptionRepeat.InvalidIndex() )
 {
  s_ServerCaptionRepeat.Insert( hash, now );
  return true;
 }
 const float elapsed = now - s_ServerCaptionRepeat[found];
 if ( !( elapsed > cc_norepeat.GetFloat() ) ) return false;
 s_ServerCaptionRepeat[found] = now;
 return true;
}

#pragma float_control( pop )

static void ClearServerCaptionRepeat()
{
 // Native1D7820 clears this map after sound overrides/log cleanup, not on
 // dictionary reload. The process-lifetime missing-warning set is not cleared.
 s_ServerCaptionRepeat.Purge();
}

static void EmitServerCloseCaption( IRecipientFilter &filter, int entindex, bool fromplayer,
 const char *token, CUtlVector< Vector > &origins, float duration, bool warnifmissing, bool direct )
{
 // Native1D6580 allows multiplayer. Only the single-player global switch gates.
 if ( gpGlobals->maxClients == 1 && !g_pClosecaption->GetBool() ) return;
 if ( duration < 0.0f )
 {
  const char *wav = soundemitterbase->GetWavFileForSound( token, GENDER_NONE );
  duration = wav ? enginesound->GetSoundDuration( CTerrorEngineSoundName( wav ).Get() ) : 2.0f;
 }
 char lowercase[256];
 Q_strncpy( lowercase, token, sizeof( lowercase ) );
 Q_strlower( lowercase );
 if ( Q_strstr( lowercase, "\\" ) ) Hack_FixEscapeChars( lowercase );
 CRecipientFilter recipients;
 recipients.CopyFrom( static_cast< CRecipientFilter & >( filter ) );
 // Native1B4E90 removes actual slots, preserving duplicate-slot and ordering semantics.
 recipients.RemoveBots();
 if ( !direct ) CBaseEntity::RemoveRecipientsIfNotCloseCaptioning( recipients );
 extern bool AttenuateCaption( const char *token, const Vector &listener, CUtlVector< Vector > &origins );
 for ( int i = recipients.GetRecipientCount() - 1; i >= 0; --i )
 {
  CBasePlayer *player = UTIL_PlayerByIndex( recipients.GetRecipientIndex( i ) );
  if ( player && AttenuateCaption( lowercase, player->GetAbsOrigin(), origins ) ) recipients.RemoveRecipient( player );
 }
 if ( recipients.GetRecipientCount() <= 0 ) return;
 char base[256];
 Q_strncpy( base, lowercase, sizeof( base ) );
 bool gendered = false;
 CBaseEntity *actor = CBaseEntity::Instance( entindex );
 if ( actor )
 {
  const int gender = static_cast< int >( GetTerrorSoundActorGender( STRING( actor->GetModelName() ) ) );
  if ( gender == 1 || gender == 21 )
  {
   Q_strncat( lowercase, "_male", sizeof( lowercase ) );
   gendered = true;
  }
  else if ( gender == 2 || gender == 22 )
  {
   Q_strncat( lowercase, "_female", sizeof( lowercase ) );
   gendered = true;
  }
 }
 CRC32_t hash = 0;
 // Native ignores warnifmissing here: both lookup attempts pass true.
 if ( !( FindServerCaptionHash( lowercase, true, &hash ) ||
  ( gendered && FindServerCaptionHash( base, true, &hash ) ) ) || !AllowServerCaptionRepeat( hash ) ) return;
 UserMessageBegin( recipients, direct ? "CloseCaptionDirect" : "CloseCaption" );
 WRITE_UBITLONG( hash, 32 );
 const float scaled = duration * 10.0f; // Native MULSS, then CVTTSS2SI, not a wide product.
 int encoded = _mm_cvttss_si32( _mm_set_ss( scaled ) );
 if ( encoded > 65535 ) encoded = 65535;
 else if ( encoded < 0 ) encoded = 0;
 // Native clamps at65535 THEN writes15 bits. Do not repair to32767.
 WRITE_UBITLONG( static_cast< unsigned int >( encoded ), 15 );
 WRITE_UBITLONG( fromplayer ? 1 : 0, 1 );
 MessageEnd();
}

// Native callback 0x001D36B0 (command_function).
static void CC_sv_soundemitter_spew( const CCommand &args )
{
	if ( args.ArgC() == 2 )
	{
		// Main-approved absent soundemitter-module DescribeSound(+184) boundary.
		// No approximate/fabricated output replaces that one module call.
	}
	else Msg( "Usage:  soundemitter_spew < sndname >\n" );
}

// Native object 0x00823048; callback 0x001D36B0; command_function.
static ConCommand sv_soundemitter_spew_command( "sv_soundemitter_spew", CC_sv_soundemitter_spew, "Print details about a sound.", 0x00000006 );

// Native callback 0x001D7570 (command_function).
static void CC_sv_soundemitter_reload( const CCommand &args )
{
	ReloadServerCaptionDictionaries();
}

// Native object 0x00822FB8; callback 0x001D7570; command_function.
static ConCommand sv_soundemitter_reload_command( "sv_soundemitter_reload", CC_sv_soundemitter_reload, "Flushes the sounds.txt system", 0x00000004 );

#endif
