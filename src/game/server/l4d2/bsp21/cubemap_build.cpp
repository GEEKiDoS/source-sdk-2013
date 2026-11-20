// Included by adapter.cpp: pinned engine buildcubemaps -> IBSPPACK004 seam.
// Native capture, VTEX, disk cleanup and restart stay in charge. Only the
// private pack object and two BSP-path checks are substituted for this call.
static const uintptr_t kBuildMapName = 0x5359DC;
static const uintptr_t kBuildWritableReturn = 0x6B6A6;
static const uintptr_t kBuildPackFactoryReturn = 0x6BA4C;
static const uintptr_t kBuildWritePathReturn = 0x6BC42;

struct CubemapBuild
{
	char bsp[MAX_PATH];
	CubemapSidecar sidecar;
	CUtlVector<CubemapMember> members;
	int expectedSamples, capturedSamples, writes;
	bool loaded, failed, hdr;
};

// Host_AbortServer can longjmp across the native build. Keep the owned state
// off that stack and clear it explicitly in the existing abort wrapper.
static thread_local CubemapBuild *g_CubemapBuild;
static void ResetCubemapBuild()
{
	delete g_CubemapBuild;
	g_CubemapBuild = NULL;
}

static void RejectCubemapBuild( const char *reason, const char *name )
{
	g_CubemapBuild->failed = true;
	BspLog( "buildcubemaps: %s (%s); sidecar not changed for this bounce", reason, name );
}

static CubemapMember *BeginCubemapMember( const char *name, const void *data, int size )
{
	CubemapBuild &build = *g_CubemapBuild;
	if ( build.failed ) return NULL;
	if ( !build.loaded || !IsCubemapMemberName( build.bsp, name ) || size < 16 || memcmp( data, "VTF\0", 4 ) )
	{
		RejectCubemapBuild( "invalid VTF pack input", name );
		return NULL;
	}
	char normalized[MAX_PATH];
	V_strncpy( normalized, name, sizeof( normalized ) );
	V_FixSlashes( normalized, '/' );
	V_strlower( normalized );
	for ( const CubemapMember &member : build.members )
	{
		if ( !strcmp( member.name.String(), normalized ) )
		{
			RejectCubemapBuild( "duplicate VTF pack input", name );
			return NULL;
		}
	}
	CubemapMember &member = build.members[build.members.AddToTail()];
	member.name = normalized;
	return &member;
}

// First six IBSPPACK004 slots, as defined in public/ibsppack.h. The factory
// below hands this object only to R_BuildCubemapSamples at its pinned callsite.
class CubemapSidecarPack
{
public:
	virtual void LoadBSPFile( IFileSystem *fs, char *name )
	{
		CubemapBuild &build = *g_CubemapBuild;
		if ( build.failed ) return;
		if ( fs != FileSystem() || _stricmp( name, build.bsp ) )
			RejectCubemapBuild( "unexpected BSP pack input", name );
		else if ( build.writes && !LoadCubemapSidecar( build.bsp, build.sidecar ) )
			RejectCubemapBuild( "cannot reopen updated sidecar", name );
		else
			build.loaded = true;
	}

	virtual void WriteBSPFile( char *name )
	{
		CubemapBuild &build = *g_CubemapBuild;
		if ( build.failed ) return;
		if ( _stricmp( name, build.sidecar.path ) || !build.loaded || !build.members.Count() ||
			build.capturedSamples != build.expectedSamples )
		{
			BspLog( "buildcubemaps: captured %d/%d sample VTFs", build.capturedSamples, build.expectedSamples );
			RejectCubemapBuild( "incomplete cubemap pack", name );
			return;
		}
		if ( !WriteCubemapSidecar( build.sidecar, build.members ) )
		{
			RejectCubemapBuild( "could not replace sidecar", name );
			return;
		}
		++build.writes;
		CubemapRuntimeRefresh( build.bsp );
		BspLog( "buildcubemaps: wrote %d %s cubemap members to %s (BSP untouched)",
			build.members.Count(), build.hdr ? "HDR" : "LDR", name );
	}

	virtual void ClearPackFile()
	{
		CubemapBuild &build = *g_CubemapBuild;
		build.loaded = false;
		build.capturedSamples = 0;
		build.members.Purge();
	}

	virtual void AddFileToPack( const char *name, const char *fullPath )
	{
		CubemapBuild &build = *g_CubemapBuild;
		if ( build.failed ) return;
		const size_t suffix = strlen( name );
		const bool hdr = suffix >= 8 && !_stricmp( name + suffix - 8, ".hdr.vtf" );
		if ( !IsCubemapMemberName( build.bsp, name ) || hdr != build.hdr )
		{
			RejectCubemapBuild( "unexpected VTEX output name", name );
			return;
		}
		FileHandle_t file = FileSystem()->Open( fullPath, "rb", NULL );
		if ( !file )
		{
			RejectCubemapBuild( "cannot open VTEX output", fullPath );
			return;
		}
		const unsigned bytes = FileSystem()->Size( file );
		CUtlVector<unsigned char> vtf;
		bool read = bytes >= 16 && bytes <= 256u * 1024 * 1024;
		if ( read )
		{
			vtf.SetCount( bytes );
			read = Read( vtf.Base(), bytes, file ) == static_cast<int>( bytes );
		}
		Close( file ); // Native removes this VTEX output as soon as the call returns.
		if ( !read )
			RejectCubemapBuild( "cannot read VTEX output", fullPath );
		else
		{
			if ( CubemapMember *member = BeginCubemapMember( name, vtf.Base(), vtf.Count() ) )
			{
				member->data.Swap( vtf );
				++build.capturedSamples;
			}
		}
	}

	virtual void AddBufferToPack( const char *name, void *bytes, int size, bool text )
	{
		if ( text ) RejectCubemapBuild( "unexpected text pack input", name );
		else if ( CubemapMember *member = BeginCubemapMember( name, bytes, size ) )
		{
			// The engine frees the cubemapdefault serialization on return.
			member->data.SetCount( size );
			memcpy( member->data.Base(), bytes, size );
		}
	}

	virtual void SetHDRMode( bool hdr ) { g_CubemapBuild->hdr = hdr; }
};
static CubemapSidecarPack g_CubemapSidecarPack;

static void *CubemapPackInterface( const char *name, int *status )
{
	const bool match = g_CubemapBuild && !strcmp( name, "IBSPPACK004" );
	if ( status ) *status = match ? IFACE_OK : IFACE_FAILED;
	return match ? &g_CubemapSidecarPack : NULL;
}

typedef CreateInterfaceFn ( *GetPackFactoryFn )( void * );
static GetPackFactoryFn g_GetPackFactory;
static CreateInterfaceFn GetCubemapPackFactory( void *module )
{
	const void *caller = _ReturnAddress();
	HookCall call;
	if ( g_CubemapBuild && caller == reinterpret_cast<void *>( g_Engine + kBuildPackFactoryReturn ) )
		return CubemapPackInterface;
	return g_GetPackFactory( module );
}

typedef bool ( *CubemapWritableFn )( void *, const char *, const char * );
static CubemapWritableFn g_CubemapWritable;
static bool CubemapWritable( void *self, const char *name, const char *pathID )
{
	const void *caller = _ReturnAddress();
	HookCall call;
	if ( g_CubemapBuild && caller == reinterpret_cast<void *>( g_Engine + kBuildWritableReturn ) &&
		!_stricmp( name, g_CubemapBuild->bsp ) )
		return true; // The sidecar writer checks/opens its own destination; never open the map writable.
	return g_CubemapWritable( self, name, pathID );
}

typedef const char *( *CubemapFullPathFn )( void *, const char *, const char *, char *, int, PathTypeFilter_t, PathTypeQuery_t * );
static CubemapFullPathFn g_CubemapFullPath;
static const char *CubemapFullPath( void *self, const char *name, const char *pathID, char *dest,
	int size, PathTypeFilter_t filter, PathTypeQuery_t *type )
{
	const void *caller = _ReturnAddress();
	HookCall call;
	if ( caller != reinterpret_cast<void *>( g_Engine + kBuildWritePathReturn ) )
		return g_CubemapFullPath( self, name, pathID, dest, size, filter, type );
	if ( g_CubemapBuild )
	{
		const char *path = g_CubemapBuild->sidecar.path;
		if ( !_stricmp( name, g_CubemapBuild->bsp ) && size > 0 && strlen( path ) < static_cast<size_t>( size ) )
		{
			V_strncpy( dest, path, size );
			return dest;
		}
		if ( size > 0 ) dest[0] = '\0';
		return NULL;
	}
	const char *path = g_CubemapFullPath( self, name, pathID, dest, size, filter, type );
	if ( !path ) return NULL;
	// This lookup uses a different path ID from the mounted BSP probe. Do
	// not allow a shadowing path to send the stock packer to a retail BSP.
	unsigned char header[8];
	FileHandle_t file = FileSystem()->Open( path, "rb", NULL );
	const bool sdk = file && Read( header, sizeof( header ), file ) == sizeof( header ) &&
		Read32( header ) == BSP21_IDENT && Read32( header + 4 ) >= MINBSPVERSION &&
		Read32( header + 4 ) <= BSPVERSION;
	if ( file ) Close( file );
	if ( sdk ) return path;
	BspLog( "buildcubemaps refused: non-SDK BSP write destination %s", path );
	dest[0] = '\0';
	return NULL;
}

// Each sample is captured as maps/<map>/cX_Y_Z(.hdr).vtf, named by its origin alone, so env_cubemaps sharing an
// origin (c1m3_mall has two such pairs) produce one VTF and the engine packs it once. Returns -1 if unreadable.
static int CountCubemapSampleNames( const char *bsp, const Bsp21Header &header )
{
	const Bsp21Lump &lump = header.lumps[LUMP_CUBEMAPS];
	CUtlVector< unsigned char > data;
	void *file = Open( bsp );
	if ( !file ) return -1;
	const Bsp21Reader reader = MountedReader( file );
	bool read;
	if ( header.version == BSP21_VERSION )
		read = Bsp21ReadLump( reader, lump, data );
	else
	{
		// The v20 on-disk order is offset, length, version, fourCC.
		Bsp21Resize( data, static_cast< int >( lump.offset ) );
		read = Bsp21ReadAt( reader, lump.version, data.Base(), lump.offset );
	}
	Close( file );
	if ( !read || data.Count() % sizeof( dcubemapsample_t ) ) return -1;
	const dcubemapsample_t *samples = reinterpret_cast< const dcubemapsample_t * >( data.Base() );
	const int count = data.Count() / sizeof( dcubemapsample_t );
	int names = 0;
	for ( int i = 0; i < count; ++i )
	{
		bool repeated = false;
		for ( int j = 0; j < i && !repeated; ++j )
			repeated = !memcmp( samples[i].origin, samples[j].origin, sizeof( samples[i].origin ) );
		if ( !repeated ) ++names;
	}
	return names;
}

typedef void ( *BuildCubemapsFn )( int );
static BuildCubemapsFn g_BuildCubemaps;
static void BuildCubemaps( int bounces )
{
	HookCall call;
	const char *bsp = &Engine<char>( kBuildMapName );
	if ( g_CubemapBuild || !ThreadInMainThread() ) return;
	if ( !*bsp || bounces <= 0 )
	{
		g_BuildCubemaps( bounces ); // Native diagnostics/no pack step.
		return;
	}
	Bsp21Header header;
	void *file = Open( bsp );
	const bool known = file && Read( &header, sizeof( header ), file ) == sizeof( header ) &&
		header.ident == BSP21_IDENT && header.version >= MINBSPVERSION && header.version <= BSP21_VERSION;
	if ( file ) Close( file );
	if ( !known )
	{
		BspLog( "buildcubemaps refused: cannot verify BSP format of %s; no BSP written", bsp );
		return;
	}
	g_CubemapBuild = new CubemapBuild{};
	CubemapBuild &build = *g_CubemapBuild;
	V_strncpy( build.bsp, bsp, sizeof( build.bsp ) );
	if ( !LoadCubemapSidecar( bsp, build.sidecar ) )
	{
		ResetCubemapBuild();
		if ( header.version <= BSPVERSION ) g_BuildCubemaps( bounces );
		else BspLog( "buildcubemaps refused: BSP21 map %s needs a valid .light sidecar; no BSP written", bsp );
		return;
	}
	const Bsp21Lump &lump = header.lumps[LUMP_CUBEMAPS];
	// The v20 on-disk order is offset, length, version, fourCC.
	const uint32_t sampleBytes = header.version == BSP21_VERSION ?
		( lump.uncompressedLength ? lump.uncompressedLength : lump.length ) : lump.offset;
	if ( sampleBytes % sizeof( dcubemapsample_t ) || sampleBytes / sizeof( dcubemapsample_t ) > MAX_MAP_CUBEMAPSAMPLES )
	{
		BspLog( "buildcubemaps refused: invalid cubemap sample lump in %s", bsp );
		ResetCubemapBuild();
		return;
	}
	build.expectedSamples = CountCubemapSampleNames( bsp, header );
	if ( build.expectedSamples < 0 )
	{
		BspLog( "buildcubemaps refused: cannot read the cubemap sample lump of %s", bsp );
		ResetCubemapBuild();
		return;
	}
	BspLog( "buildcubemaps: redirect %s -> %s", build.bsp, build.sidecar.path );
	g_BuildCubemaps( bounces );
	ResetCubemapBuild();
}
