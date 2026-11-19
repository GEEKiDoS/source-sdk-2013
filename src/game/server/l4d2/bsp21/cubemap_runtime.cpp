//========= Copyright Valve Corporation, All rights reserved. ============//
// Included by adapter.cpp after cubemap_sidecar.cpp. Only client world loads
// publish an overlay; the BSP, its embedded pak and other .light readers keep
// their original filesystem paths.

static SRWLOCK g_CubemapRuntimeLock = SRWLOCK_INIT;
static CubemapSidecar g_CubemapRuntimeState;
static void *g_CubemapWorld;
static char g_CubemapWorldBsp[MAX_PATH];
static volatile LONG g_CubemapBackings;

static void CubemapRuntimeClear()
{
	CubemapSidecar old;
	AcquireSRWLockExclusive( &g_CubemapRuntimeLock );
	g_CubemapWorld = NULL;
	g_CubemapWorldBsp[0] = 0;
	g_CubemapRuntimeState.Swap( old );
	ReleaseSRWLockExclusive( &g_CubemapRuntimeLock );
}

static bool CubemapRuntimeCanRetire()
{
	// A native CMemoryFileHandle can keep a server-owned backing alive even
	// after the short-lived registration and the map state have been cleared.
	const LONG count = InterlockedCompareExchange( &g_CubemapBackings, 0, 0 );
	if ( !count ) return true;
	BspLog( "REJECT retirement with live cubemap backings=%ld; owner retained", count );
	return false;
}

static bool CubemapRuntimePath( const char *path, char (&canonical)[MAX_PATH] )
{
	if ( !path ) return false;
	for ( unsigned i = 0; i < MAX_PATH; ++i )
	{
		const char c = path[i];
		canonical[i] = c == '\\' ? '/' : ( c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c );
		if ( !c ) return i != 0;
	}
	return false;
}

// Called after the writer publishes a replacement .light. Invalidate the old
// directory even when the replacement cannot be loaded; its offsets are stale.
static void CubemapRuntimeRefresh( const char *bsp )
{
	if ( !ThreadInMainThread() ) return;
	char path[MAX_PATH];
	if ( !CubemapRuntimePath( bsp, path ) ) return;
	AcquireSRWLockShared( &g_CubemapRuntimeLock );
	void *world = !strcmp( path, g_CubemapWorldBsp ) ? g_CubemapWorld : NULL;
	ReleaseSRWLockShared( &g_CubemapRuntimeLock );
	if ( !world ) return;

	CubemapSidecar next;
	LoadCubemapSidecar( path, next );
	AcquireSRWLockExclusive( &g_CubemapRuntimeLock );
	if ( world == g_CubemapWorld && !strcmp( path, g_CubemapWorldBsp ) )
		g_CubemapRuntimeState.Swap( next );
	ReleaseSRWLockExclusive( &g_CubemapRuntimeLock );
}

typedef void (*CubemapWorldFn)( void *, void * );
static CubemapWorldFn g_CubemapLoad;
static void CubemapRuntimeLoad( void *loader, void *world )
{
	HookCall call;
	// The native brush-model dispatcher already gave the BSP pak PATH_ADD_TO_HEAD.
	// Publish before LoadWorldGuts can download any VTFs (not just its sample loop).
	{
		const char *name = Function<const char *(*)(void *)>( 0x28D9C0 )( static_cast<unsigned char *>(world) + 8 );
		char path[MAX_PATH];
		CubemapSidecar next;
		const bool havePath = CubemapRuntimePath( name, path );
		if ( havePath ) LoadCubemapSidecar( path, next );
		AcquireSRWLockExclusive( &g_CubemapRuntimeLock );
		g_CubemapWorld = havePath ? world : NULL;
		if ( havePath ) V_strncpy( g_CubemapWorldBsp, path, sizeof( g_CubemapWorldBsp ) );
		else g_CubemapWorldBsp[0] = 0;
		g_CubemapRuntimeState.Swap( next );
		ReleaseSRWLockExclusive( &g_CubemapRuntimeLock );
		// Destroy the previous directory before entering code that can Host_Error
		// and leave this frame via the engine's nonlocal abort.
	}
	g_CubemapLoad( loader, world );
}

static const uintptr_t kCubemapUnmountReturn = 0xFAB97;
typedef bool ( *CubemapUnmountFn )( void *, const char *, const char * );
static CubemapUnmountFn g_CubemapUnmount;
static bool CubemapRuntimeUnmount( void *self, const char *name, const char *pathID )
{
	const void *caller = _ReturnAddress();
	HookCall call;
	// The DX12 bridge already hooks UnloadModel. Scope this to its native
	// world branch's RemoveSearchPath call, after it drops cubemap refs.
	if ( caller == reinterpret_cast<void *>( g_Engine + kCubemapUnmountReturn ) )
		CubemapRuntimeClear();
	return g_CubemapUnmount( self, name, pathID );
}

// Use the pinned filesystem's early memory-file branch to obtain a real native
// handle, including Size/Seek/ReadEx/Close. Give it a private, temporary key;
// unrelated Open/Find/FileExists calls never see a mounted .light pak.
class CubemapBacking final : public CMemoryFileBacking
{
public:
	CubemapBacking( IFileSystem *fs, CUtlVector<unsigned char> &bytes ) : CMemoryFileBacking( fs )
	{
		InterlockedIncrement( &g_CubemapBackings );
		V_snprintf( m_Key, sizeof( m_Key ), "__bsp21_cubemap_%llx.vtf", static_cast<unsigned long long>( reinterpret_cast<uintptr_t>(this) ) );
		m_Bytes.Swap( bytes );
		m_pFileName = m_Key;
		m_pData = reinterpret_cast<const char *>( m_Bytes.Base() );
		m_nLength = m_Bytes.Count();
	}
	~CubemapBacking() override
	{
		// Name and VTF bytes already have owners here; do not let the base
		// free them via strdup/optimal-read-buffer allocator conventions.
		m_pFileName = NULL;
		m_pData = NULL;
	}
	static void operator delete( void *memory )
	{
		HookCall call;
		::operator delete( memory );
		InterlockedDecrement( &g_CubemapBackings );
	}
private:
	char m_Key[64];
	CUtlVector<unsigned char> m_Bytes;
};

// OpenEx is IFileSystem slot 69 on pinned filesystem_stdio. Dedicated hosts
// skip all these hooks: they do not download render cubemap VTFs.
typedef FileHandle_t (*CubemapOpenFn)( void *, const char *, const char *, unsigned, const char *, char ** );
static CubemapOpenFn g_CubemapOpen;
static FileHandle_t CubemapRuntimeOpen( void *self, const char *name, const char *mode, unsigned flags,
	const char *pathID, char **resolved )
{
	HookCall call;
	// Native CTexture uses a plain binary read, with NULL or GAME path ID.
	// Do not change writes, explicit paths, purity-tracked opens or other IDs.
	if ( !name || _strnicmp( name, "materials", 9 ) || (name[9] != '/' && name[9] != '\\') ||
		_strnicmp( name + 10, "maps", 4 ) || (name[14] != '/' && name[14] != '\\') ||
		!mode || strcmp( mode, "rb" ) || flags || (pathID && _stricmp( pathID, "GAME" )) || self != FileSystem() )
		return g_CubemapOpen( self, name, mode, flags, pathID, resolved );

	char path[MAX_PATH];
	if ( !CubemapRuntimePath( name, path ) ) return g_CubemapOpen( self, name, mode, flags, pathID, resolved );
	char source[MAX_PATH];
	CUtlVector<unsigned char> bytes;
	AcquireSRWLockShared( &g_CubemapRuntimeLock );
	const bool scoped = g_CubemapWorld && IsCubemapMemberName( g_CubemapWorldBsp, path );
	const bool loaded = scoped && g_CubemapRuntimeState.valid && ReadCubemapSidecarMember( g_CubemapRuntimeState, path, bytes );
	if ( loaded ) V_strncpy( source, g_CubemapRuntimeState.path, sizeof( source ) );
	ReleaseSRWLockShared( &g_CubemapRuntimeLock );

	if ( loaded && bytes.Count() > 0 )
	{
		IFileSystem *fs = static_cast<IFileSystem *>(self);
		CubemapBacking *backing = new CubemapBacking( fs, bytes );
		const int size = backing->m_nLength;
		CMemoryFileBacking *existing = NULL;
		FileHandle_t file = FILESYSTEM_INVALID_HANDLE;
		if ( fs->RegisterMemoryFile( backing, &existing ) )
		{
			file = g_CubemapOpen( self, backing->m_pFileName, mode, flags, pathID, NULL );
			fs->UnregisterMemoryFile( backing );
		}
		else if ( existing )
		{
			// A live backing's address makes normal collisions impossible.
			// On this pin, unregister only drops the registry ref on the last
			// unregister; balance the additional ref acquired on collision.
			fs->UnregisterMemoryFile( existing );
			existing->Release();
		}
		backing->Release();
		if ( file != FILESYSTEM_INVALID_HANDLE )
		{
			if ( resolved ) *resolved = NULL; // There is no standalone VTF file to resolve.
			BspLog( "Cubemap VTF source=%s bytes=%d name=%s", source, size, path );
			return file;
		}
	}

	FileHandle_t file = g_CubemapOpen( self, name, mode, flags, pathID, resolved );
	if ( scoped )
	{
		if ( file != FILESYSTEM_INVALID_HANDLE )
			BspLog( "Cubemap VTF source=native bytes=%u name=%s", FileSystem()->Size( file ), path );
		else BspLog( "Cubemap VTF source=missing name=%s", path );
	}
	return file;
}
