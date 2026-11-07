//========= Copyright Valve Corporation, All rights reserved. ============//
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <intrin.h>
#include <stdarg.h>
#include "third_party/minhook-1.3.4/include/MinHook.h"
#include "bsp21_adapter.h"
#include "bsp21_format.h"
#include "tier0/dbg.h"
#include "filesystem.h"
#include "datacache/imdlcache.h"
#include "materialsystem/imaterialsystem.h"
#include "vstdlib/jobthread.h"
#include "tier1/strtools.h"
#include "l4d2/gamerules/terror_mode.h"

static void BspLog( const char *format, ... );
#include "preflight.cpp"

// The engine/filesystem ABI is pinned; mounted content is structurally checked.
// Only private consumer headers are normalized. Public reads and checksums keep
// the original bytes and offsets, whether the BSP is loose or a VPK member.
static uintptr_t g_Engine, g_Filesystem;
static uintptr_t g_PakReturn;
static const unsigned kSourceHeaderSize = sizeof( Bsp21Header );
static Bsp21Layout g_Source;
static char g_SourceName[MAX_PATH];
static bool g_Installed, g_SourceMap;
struct PropTint { uint32_t rgba; float rgb[3]; };
static CUtlVector< PropTint > g_PropTint;
static int g_PropCount;
static volatile LONG g_InFlight;
static thread_local LONG g_ThreadCalls;
static thread_local uint64_t g_HookEpoch;
// The retirement barrier also checks suspended instruction pointers in our
// owner image, covering the small entry/exit windows outside this counter.
class HookCall
{
public:
	HookCall() { InterlockedIncrement(&g_InFlight); m_Epoch=g_HookEpoch; ++g_ThreadCalls; }
	~HookCall()
	{
		// Host_AbortServer can unwind C++ frames or skip them. Its epoch
		// handoff accounts abandoned calls once in either native CRT mode.
		if(m_Epoch==g_HookEpoch) { --g_ThreadCalls; InterlockedDecrement(&g_InFlight); }
	}
private:
	uint64_t m_Epoch;
};

template <class T> static T &Engine( uintptr_t rva ) { return *reinterpret_cast<T *>( g_Engine + rva ); }
template <class T> static T Function( uintptr_t rva ) { return reinterpret_cast<T>( g_Engine + rva ); }
template <class T> static T Method( void *object, unsigned slot ) { return reinterpret_cast<T>( (*reinterpret_cast<void ***>(object))[slot] ); }
static void BspLog( const char *format, ... )
{
	char message[1024];
	va_list args; va_start(args,format); vsnprintf_s(message,sizeof(message),_TRUNCATE,format,args); va_end(args);
	Msg("[BSP21] %s\n",message);
}
static __declspec(noreturn) void Fail( const char *reason )
{
	BspLog( "FATAL %s; owner and trampoline storage retained", reason );
	// Platform fatal exit remains fatal even in stripped-logging builds.
	Plat_FatalError( "BSP21 adapter: %s\n", reason );
}
static void (*g_AbortServer)();
static __declspec(noreturn) void AbortServer()
{
	HookCall call;
	if(!ThreadInMainThread()) Fail("native host abort outside serialized owner thread");
	LONG abandoned=g_ThreadCalls;
	g_ThreadCalls=0;
	++g_HookEpoch;
	InterlockedExchangeAdd(&g_InFlight,-abandoned);
	BspLog("HOST_ABORT abandoned wrapper calls=%ld; native nonlocal exit retained",abandoned);
	// The pinned body increments the native abort counter and longjmps. No
	// abandoned adapter frame can return normally; removal is main-thread only.
	g_AbortServer();
	Fail("noreturn native Host_AbortServer unexpectedly returned");
}
static uint32_t Read32( const void *data ) { uint32_t value; memcpy( &value, data, 4 ); return value; }
static void Put32( void *data, uint32_t value ) { memcpy( data, &value, 4 ); }
static bool IsBsp21( const void *data )
{
	return Read32( data ) == 0x50534256 && Read32( static_cast< const unsigned char * >( data ) + 4 ) == 21;
}
static void NormalizeHeader( void *data )
{
	unsigned char *header = static_cast<unsigned char *>(data);
	for ( int i = 0; i < 64; ++i )
	{
		unsigned char *lump = header + 8 + i * 16;
		uint32_t version = Read32(lump), offset = Read32(lump + 4), length = Read32(lump + 8);
		Put32(lump, offset); Put32(lump + 4, length); Put32(lump + 8, version);
	}
	Put32( header + 4, 20 );
}
static void *BaseFS() { return reinterpret_cast<unsigned char *>(Engine<void *>(0x7500E0)) + 8; }
static void *Open( const char *name )
{
	void *fs = Engine<void *>(0x7500E0);
	return Method<void *(*)(void *,const char *,const char *,int,const char *,void *)>(fs,69)(fs,name,"rb",0,"GAME",NULL);
}
static int Read( void *out, int bytes, void *file ) { void *fs=BaseFS(); return Method<int (*)(void *,void *,int,void *)>(fs,0)(fs,out,bytes,file); }
static void Close( void *file ) { void *fs=BaseFS(); Method<void (*)(void *,void *)>(fs,3)(fs,file); }
static void Seek( void *file, int offset ) { void *fs=BaseFS(); Method<void (*)(void *,void *,int,int)>(fs,4)(fs,file,offset,0); }
static void CopyString( void *out, const char *in ) { Function<void (*)(void *,const char *,int)>(0x282DB0)(out,in,260); }

typedef bool (*ValidFn)(void *,const char *,bool);

static IFileSystem *FileSystem() { return reinterpret_cast< IFileSystem * >( Engine<void *>( 0x7500E0 ) ); }
static bool ReadMountedAt( void *file, uint32_t offset, void *out, uint32_t bytes )
{
	Seek( file, offset );
	return Read( out, bytes, file ) == static_cast< int >( bytes );
}
static Bsp21Reader MountedReader( void *file )
{
	Bsp21Reader reader = { file, FileSystem()->Size( file ), ReadMountedAt };
	return reader;
}
static ValidFn g_Valid;
static bool MapValid( void *self, const char *name, bool quiet )
{
	HookCall call;
	// Preserve the SDK validator's filename policy and framing.
	if ( !name || !*name ) return g_Valid( self, name, quiet );
	char nativeName[MAX_PATH];
	CopyString( nativeName, name );
	const char *base = Function<const char *(*)(const char *)>( 0x282150 )( name );
	for ( const char *p = base; p && *p; ++p )
		if ( *p <= 31 || strchr( "\"'*/:;<>?\\|", *p ) ) return g_Valid( self, name, quiet );
	void *file = Open( nativeName );
	unsigned char header[8];
	bool source = file && Read( header, sizeof( header ), file ) == sizeof( header ) && IsBsp21( header );
	if ( !source )
	{
		if ( file ) Close( file );
		return g_Valid( self, name, quiet );
	}
	Bsp21Layout layout;
	bool valid = ReadBsp21Layout( MountedReader( file ), layout, nativeName );
	Close( file );
	if ( !valid ) return false;
	CopyString( &Engine<char>( 0x69FDC0 ), nativeName );
	BspLog( "Map_IsValid accepted mounted BSP21 path=%s", nativeName );
	return true;
}

struct BspLumpFileHeader
{
	int offset, id, version, length, revision;
};
struct BspLumpFile
{
	FileHandle_t file;
	int index;
	BspLumpFileHeader header;
};
static_assert( sizeof( BspLumpFileHeader ) == 20, "Native lump-file header size" );
static_assert( sizeof( BspLumpFile ) == 32, "Pinned x64 lump-file slot size" );

// Original engine 0xEA0B0 loads overrides before collision or render lumps.
// The SDK retains their consumers and cleanup, but omits this initialization.
static void LoadLumpFiles()
{
	IBaseFileSystem *fs = reinterpret_cast< IBaseFileSystem * >( BaseFS() );
	BspLumpFile *lumps = reinterpret_cast< BspLumpFile * >( &Engine<char>( 0x69F420 ) );
	for ( int index = 0; index < 128; ++index )
	{
		const char *suffix = "l";
		if ( g_TerrorModes.IsBaseMode( "survival" ) )
			suffix = "h";
		else if ( g_TerrorModes.IsBaseMode( "scavenge" ) )
			suffix = "s";

		char base[MAX_PATH], filename[MAX_PATH];
		V_StripExtension( &Engine<char>( 0x69F310 ), base, sizeof( base ) );
		Q_snprintf( filename, sizeof( filename ), "%s_%s_%d.lmp", base, suffix, index );
		if ( !fs->FileExists( filename, NULL ) )
			break;

		FileHandle_t file = fs->Open( filename, "rb", NULL );
		if ( !file )
		{
			Function<void (*)()>( 0x105210 )();
			Function<void (*)( const char *, ... )>( 0x1A13C0 )(
				"CMapLoadHelper::Init, failed to load lump file %s\n", filename );
			return;
		}

		BspLumpFileHeader header = {};
		fs->Read( &header, sizeof( header ), file );
		if ( static_cast< unsigned int >( header.id ) >= 64 )
		{
			Warning( "Found invalid lump file '%s'. Lump Id: %d\n", filename, header.id );
			// Native leaves this invalid-ID handle open and unretained.
			continue;
		}

		BspLumpFile &lump = lumps[header.id];
		if ( lump.file )
			fs->Close( lump.file );
		lump.file = file;
		lump.index = index;
		lump.header = header;
	}
}
typedef uintptr_t (*InitFn)(void *,const char *);
static InitFn g_Init;
static uintptr_t DiskInit( void *model, const char *loadName )
{
	HookCall call;
	if ( Engine<int>( 0x69FDA8 ) ) return g_Init( model, loadName );
	const char *name = model ? Function<const char *(*)(void *)>( 0x28D9C0 )( static_cast< unsigned char * >( model ) + 8 ) : loadName;
	void *file = Open( name );
	unsigned char header[8];
	if ( !file || Read( header, sizeof( header ), file ) != sizeof( header ) || !IsBsp21( header ) )
	{
		if ( file ) Close( file );
		g_SourceMap = false;
		return g_Init( model, loadName );
	}
	if ( !ReadBsp21Layout( MountedReader( file ), g_Source, name ) )
	{
		Close( file );
		Function<void (*)( const char *, ... )>( 0x1A13C0 )( "Invalid BSP21 map %s; see lump diagnostic above\n", name );
		return 0;
	}
	CopyString( g_SourceName, name );
	++Engine<int>( 0x69FDA8 );
	Engine<void *>( 0x69FDA0 ) = NULL;
	Engine<void *>( 0x69FD98 ) = file;
	Engine<char>( 0x69F200 ) = 0;
	memset( &Engine<char>( 0x69F420 ), 0, 2048 );
	CopyString( &Engine<char>( 0x69F310 ), name );
	memcpy( &Engine<char>( 0x69EDF0 ), &g_Source.header, kSourceHeaderSize );
	NormalizeHeader( &Engine<char>( 0x69EDF0 ) );
	Seek( file, kSourceHeaderSize );
	CopyString( &Engine<char>( 0x69F200 ), loadName );
	if ( !Engine<int>( 0x477620 ) ) Engine<int>( 0x477620 ) = Engine<int>( 0x69F1F8 );
	Function<void (*)(int)>( 0x1B8C80 )( 20 );
	Engine<void *>( 0x69FDA0 ) = &Engine<char>( 0x470BC8 );
	g_SourceMap = true;
	LoadLumpFiles();
	BspLog( "CMapLoadHelper disk private header normalized map=%s revision=%u", name, g_Source.header.revision );
	return reinterpret_cast< uintptr_t >( Engine<void *>( 0x69FDA0 ) );
}
typedef void (*MemoryFn)(void *,void *,void *,int,int);
static MemoryFn g_Memory;
struct Bsp21Memory
{
	const unsigned char *data;
	uint32_t size;
};
static bool ReadMemoryAt( void *context, uint32_t offset, void *out, uint32_t bytes )
{
	const Bsp21Memory &memory = *static_cast< const Bsp21Memory * >( context );
	if ( !Bsp21Range( offset, bytes, memory.size ) ) return false;
	memcpy( out, memory.data + offset, bytes );
	return true;
}
static void MemoryInit( void *model, void *context, void *data, int size, int error )
{
	HookCall call;
	if ( error || size < static_cast< int >( kSourceHeaderSize ) || !IsBsp21( data ) || Engine<int>( 0x69FDA8 ) )
	{
		if ( !Engine<int>( 0x69FDA8 ) ) g_SourceMap = false;
		g_Memory( model, context, data, size, error );
		return;
	}
	const char *name = Function<const char *(*)(void *)>( 0x28D9C0 )( static_cast< unsigned char * >( model ) + 8 );
	void *file = Open( name );
	uint32_t fileSize = file ? FileSystem()->Size( file ) : 0;
	if ( file ) Close( file );
	Bsp21Memory memory = { static_cast< const unsigned char * >( data ), static_cast< uint32_t >( size ) };
	Bsp21Reader reader = { &memory, fileSize, ReadMemoryAt };
	// The queue excludes the embedded pak. Every other declared byte must be
	// available in this allocation before CMapLumpHelper can index it.
	if ( !ReadBsp21Layout( reader, g_Source, name ) || memory.size < g_Source.prefix || memory.size > fileSize )
	{
		Function<void (*)( const char *, ... )>( 0x1A13C0 )( "Invalid BSP21 queued map %s or truncated lump prefix\n", name );
		return;
	}
	CopyString( g_SourceName, name );
	++Engine<int>( 0x69FDA8 );
	Engine<void *>( 0x69FDA0 ) = NULL;
	Engine<void *>( 0x69FD98 ) = NULL;
	memset( &Engine<char>( 0x69F420 ), 0, 2048 );
	CopyString( &Engine<char>( 0x69F310 ), name );
	Function<void (*)(const char *,char *,int)>( 0x2810C0 )( &Engine<char>( 0x69F310 ), &Engine<char>( 0x69F200 ), 260 );
	// Attach, do not duplicate or modify the queue-owned source prefix.
	Function<void (*)(void *,void *,int,int,int)>( 0x28D0E0 )( &Engine<char>( 0x69ECC0 ), data, size, size, 0 );
	memcpy( &Engine<char>( 0x69EDF0 ), &g_Source.header, kSourceHeaderSize );
	NormalizeHeader( &Engine<char>( 0x69EDF0 ) );
	Engine<int>( 0x477620 ) = Engine<int>( 0x69F1F8 );
	Function<void (*)(int)>( 0x1B8C80 )( 20 );
	Engine<void *>( 0x69FDA0 ) = &Engine<char>( 0x470BC8 );
	g_SourceMap = true;
	BspLog( "CMapLoadHelper memory private header normalized map=%s prefix=%d", name, size );
}

typedef bool (*DigestFn)(void *,const char *);
static DigestFn g_CRC, g_MD5;
static __declspec(noinline) bool Digest( void *out, const char *name, bool md5 )
{
	void *file=Open(name); unsigned char header[1036], block[65536], context[88]={0};
	if ( !file ) return false;
	if ( Read( header, sizeof( header ), file ) != sizeof( header ) ) { Close( file ); return false; }
	if ( !IsBsp21( header ) ) { Close( file ); return ( md5 ? g_MD5 : g_CRC )( out, name ); }
	Bsp21Layout layout;
	if ( !ReadBsp21Layout( MountedReader( file ), layout, name ) ) { Close( file ); return false; }
	if ( md5 ) Function<void (*)(void *)>(0x291E70)(context);
	for ( int i=1; i<64; ++i )
	{
		int length=Read32(header+8+16*i+8); Seek(file,Read32(header+8+16*i+4));
		while ( length>0 )
		{
			int count=length>static_cast<int>(sizeof(block))?static_cast<int>(sizeof(block)):length;
			if ( Read(block,count,file)!=count ) { Close(file); return false; }
			if ( md5 ) Function<void (*)(void *,const void *,unsigned)>(0x292690)(context,block,count);
			else Function<void (*)(void *,const void *,int)>(0x28E0D0)(out,block,count);
			length-=count;
		}
	}
	Close(file);
	if ( md5 ) Function<void (*)(void *,void *)>(0x291DB0)(out,context);
	BspLog("source %s computed from original bytes numeric lump order",md5?"MD5":"CRC32");
	return true;
}
static bool CRC( void *out,const char *name ) { HookCall call; return Digest( out, name, false ); }
static bool MD5( void *out,const char *name ) { HookCall call; return Digest( out, name, true ); }

static bool ReadRawAt( void *file, uint32_t offset, void *out, uint32_t bytes )
{
	Method<void (*)(void *,int64_t,int)>( file, 3 )( file, offset, SEEK_SET );
	return Method<intptr_t (*)(void *,void *,intptr_t,intptr_t)>( file, 6 )( file, out, bytes, bytes ) == bytes;
}
typedef intptr_t (*FreadFn)(void *,void *,intptr_t,intptr_t,void *);
static FreadFn g_Fread;
static intptr_t PakRead( void *self, void *out, intptr_t capacity, intptr_t size, void *file )
{
	void *caller = _ReturnAddress();
	HookCall call;
	intptr_t result = g_Fread( self, out, capacity, size, file );
	if ( caller == reinterpret_cast< void * >( g_Filesystem + g_PakReturn ) && size == 1036 && result == 1036 && IsBsp21( out ) )
	{
		int64_t position = Method<int64_t (*)(void *)>( file, 4 )( file );
		Method<void (*)(void *,int64_t,int)>( file, 3 )( file, 0, SEEK_END );
		int64_t length = Method<int64_t (*)(void *)>( file, 4 )( file );
		Bsp21Reader reader = { file, length > INT_MAX ? 0 : static_cast< uint32_t >( length ), ReadRawAt };
		Bsp21Layout layout;
		bool valid = ReadBsp21Layout( reader, layout, "embedded pak" );
		Method<void (*)(void *,int64_t,int)>( file, 3 )( file, position, SEEK_SET );
		if ( !valid ) return 0;
		NormalizeHeader( out );
		BspLog( "embedded-pak private header normalized; mounted source offsets unchanged" );
	}
	return result;
}

// Pinned CPackedStoreFileHandle layout (SDK filesystem and dedicated owners).
struct PackedFileHandle
{
	int archive, offset, size, cursor;
	const void *preload;
	uint16_t preloadSize;
	void *owner, *header, *name;
};
static_assert( sizeof( PackedFileHandle ) == 56, "SDK packed handle ABI" );
static_assert( offsetof( PackedFileHandle, owner ) == 32, "SDK packed handle owner ABI" );
struct PackedVersion
{
	void *store;
	int version;
};
static CThreadFastMutex g_PackedVersionsMutex;
static CUtlVector< PackedVersion > g_PackedVersions;
typedef int ( *PackedReadFn )( void *, PackedFileHandle *, void *, int );
typedef void ( *PackedDestroyFn )( void * );
static PackedReadFn g_PackedRead;
static PackedDestroyFn g_PackedDestroy;

static int PackedStoreVersion( void *store )
{
	AUTO_LOCK( g_PackedVersionsMutex );
	for ( const PackedVersion &entry : g_PackedVersions )
		if ( entry.store == store ) return entry.version;
	// Match GetFileHandle's physical directory filename, but use our own OS
	// handle: probing must not disturb the native shared archive cursor/cache.
	const unsigned char *bytes = static_cast< const unsigned char * >( store );
	char path[MAX_PATH];
	sprintf_s( path, "%s%s.vpk", reinterpret_cast< const char * >( bytes + 4 ), bytes[532] ? "_dir" : "" );
	HANDLE file = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	uint32_t header[3] = {};
	DWORD count = 0;
	bool read = file != INVALID_HANDLE_VALUE && ReadFile( file, header, sizeof( header ), &count, NULL );
	if ( file != INVALID_HANDLE_VALUE ) CloseHandle( file );
	int version = -1;
	if ( read && count == sizeof( header ) && header[0] == 0x55AA1234 &&
		( header[1] == 1 || header[1] == 2 ) && header[2] == Read32( bytes + 524 ) )
		version = header[1];
	else
		BspLog( "REJECT VPK directory header path=%s; embedded reads unavailable", path );
	g_PackedVersions.AddToTail( PackedVersion{ store, version } );
	return version;
}
static int PackedRead( void *store, PackedFileHandle *handle, void *out, int bytes )
{
	HookCall call;
	if ( handle->archive != 0x7FFF ) return g_PackedRead( store, handle, out, bytes );
	int version = PackedStoreVersion( store );
	if ( version < 0 ) return 0;
	if ( version != 1 ) return g_PackedRead( store, handle, out, bytes );
	// SDK ReadData adds directorySize + sizeof(v2 header), even for v1.
	// Its synchronous cache path never retains the handle pointer. Leave the
	// native preload bytes, read counts, locking and cache entirely in charge.
	PackedFileHandle adjusted = *handle;
	adjusted.offset -= 16;
	int read = g_PackedRead( store, &adjusted, out, bytes );
	handle->cursor = adjusted.cursor;
	return read;
}
static void PackedDestroy( void *store )
{
	HookCall call;
	{
		AUTO_LOCK( g_PackedVersionsMutex );
		for ( int i = 0; i < g_PackedVersions.Count(); ++i )
		{
			if ( g_PackedVersions[i].store == store )
			{
				g_PackedVersions.Remove( i );
				break;
			}
		}
	}
	g_PackedDestroy( store );
}

static volatile LONG g_MountedFiles;
// Exact CStdFilesystemFile virtual order, verified against both pinned owners.
// AddPackFile and its later ZIP reopens use this low-level ABI rather than
// IBaseFileSystem. Forward VPK members to that public mounted-file interface.
class MountedBspFile
{
public:
	explicit MountedBspFile( FileHandle_t file ) : m_File( file ), m_Size( FileSystem()->Size( file ) ), m_Error( 0 )
	{
		InterlockedIncrement( &g_MountedFiles );
	}
	virtual ~MountedBspFile()
	{
		HookCall call;
		if ( m_File ) FileSystem()->Close( m_File );
	}
	static void operator delete( void *memory )
	{
		// The scalar deleting destructor still returns through this DLL after
		// the allocator call. Keep the live object and wrapper counted until
		// that call has returned, even though the C++ destructor already ran.
		HookCall call;
		::operator delete( memory );
		InterlockedDecrement( &g_MountedFiles );
	}
	virtual void SetBufferSize( size_t ) { HookCall call; } // Native read-only file has no stdio buffer.
	virtual int Close()
	{
		HookCall call;
		FileSystem()->Close( m_File );
		m_File = NULL;
		return 0;
	}
	virtual void Seek( int64_t offset, int origin )
	{
		HookCall call;
		int64_t position = origin == SEEK_CUR ? FileSystem()->Tell( m_File ) + offset :
			origin == SEEK_END ? m_Size - offset : offset;
		if ( position < 0 || position > INT_MAX ) { m_Error = 1; return; }
		FileSystem()->Seek( m_File, static_cast< int >( position ), FILESYSTEM_SEEK_HEAD );
	}
	virtual int64_t Tell() { HookCall call; return FileSystem()->Tell( m_File ); }
	virtual int Eof() { HookCall call; return FileSystem()->Tell( m_File ) >= m_Size; }
	virtual size_t Read( void *out, size_t capacity, size_t bytes )
	{
		HookCall call;
		if ( bytes > INT_MAX || bytes > capacity ) { m_Error = 1; return 0; }
		int read = FileSystem()->Read( out, static_cast< int >( bytes ), m_File );
		if ( read < 0 ) { m_Error = 1; return 0; }
		return read;
	}
	virtual size_t Write( const void *, size_t ) { HookCall call; m_Error = 1; return 0; }
	virtual int SetMode( int ) { HookCall call; m_Error = 1; return -1; }
	virtual int Print( const char *, va_list ) { HookCall call; m_Error = 1; return -1; }
	virtual int Error() { HookCall call; return m_Error; }
	virtual int Flush() { HookCall call; return 0; }
	virtual char *Gets( char *out, int bytes )
	{
		HookCall call;
		return FileSystem()->ReadLine( out, bytes, m_File );
	}
	virtual int SectorSize() { HookCall call; return 1; }
private:
	FileHandle_t m_File;
	uint32_t m_Size;
	int m_Error;
};
typedef void *( *FopenFn )( void *, const char *, const char *, int, int64_t * );
static FopenFn g_Fopen;
static thread_local bool g_OpeningMountedBsp;
static void *MountedOpen( void *self, const char *name, const char *mode, int flags, int64_t *size )
{
	HookCall call;
	// Limit the bridge to the verified embedded-pak consumer's BSP file kind.
	// Public Open already resolves VPK members; this is only its raw-file seam.
	size_t length = strlen( name );
	if ( !g_OpeningMountedBsp && !strcmp( mode, "rb" ) && length > 4 && !_stricmp( name + length - 4, ".bsp" ) &&
		( V_stristr( name, ".vpk\\" ) || V_stristr( name, ".vpk/" ) ) )
	{
		g_OpeningMountedBsp = true;
		FileHandle_t file = FileSystem()->Open( name, mode, NULL );
		g_OpeningMountedBsp = false;
		if ( !file ) return NULL;
		if ( size ) *size = FileSystem()->Size( file );
		return new MountedBspFile( file );
	}
	return g_Fopen( self, name, mode, flags, size );
}

typedef uintptr_t (*WorldlightsFn)(void *,bool);
static WorldlightsFn g_Worldlights;
static __declspec(noinline) uintptr_t Worldlights( void *helper, bool hdr )
{
	if ( !g_SourceMap ) return g_Worldlights( helper, hdr );
	unsigned lump = Read32( static_cast< unsigned char * >( helper ) + 40 );
	if ( lump != 15 && lump != 54 ) Fail( "worldlight helper has unexpected lump identity" );
	if ( !g_Source.header.lumps[lump].version ) return g_Worldlights( helper, hdr );
	int bytes = *static_cast< int * >( helper );
	if ( bytes < 0 || bytes % 100 ) Fail( "invalid BSP21 worldlight record extent" );
	if ( !bytes ) return g_Worldlights( helper, hdr );
	unsigned count = bytes / 100;
	const unsigned char *source = *reinterpret_cast< unsigned char ** >( static_cast< unsigned char * >( helper ) + 24 );
	CUtlVector< unsigned char > canonical;
	Bsp21Resize( canonical, static_cast< int >( count * 88 ) );
	unsigned char localHelper[304];
	for ( unsigned i = 0; i < count; ++i )
	{
		memcpy( canonical.Base() + i * 88, source + i * 100, 36 );
		memcpy( canonical.Base() + i * 88 + 36, source + i * 100 + 48, 52 );
	}
	memcpy( localHelper, helper, sizeof( localHelper ) );
	Put32( localHelper, count * 88 );
	*reinterpret_cast< void ** >( localHelper + 24 ) = canonical.Base();
	uintptr_t result = g_Worldlights( localHelper, hdr );
	BspLog( "worldlights v1 map=%s decoded=%u native88 bytes=%u", g_SourceName, count, count * 88 );
	return result;
}
static uintptr_t WorldlightsEntry( void *helper, bool hdr )
{
	HookCall call;
	return Worldlights(helper,hdr);
}

typedef int (*GameIntFn)(int);
typedef bool (*GameLoadFn)(int,void *,int);
typedef void (*PropManagerFn)(void *);
static GameIntFn g_GameVersion;
static GameLoadFn g_GameLoad;
static PropManagerFn g_PropsShutdown;
static int GameVersion( int id )
{
	void *caller=_ReturnAddress();
	HookCall call;
	int version=g_GameVersion(id);
	if(g_SourceMap && caller==reinterpret_cast<void *>(g_Engine+0x2064D9) && id==0x73707270)
	{
		if ( version == 8 ) return 6;
		if ( version == 9 ) return 10;
	}
	return version;
}
static bool GameLoad( int id, void *data, int bytes )
{
	void *caller = _ReturnAddress();
	HookCall call;
	if ( !g_SourceMap ) return g_GameLoad( id, data, bytes );
	int index = g_Source.gameLumps.Count();
	for ( int i = 0; i < g_Source.gameLumps.Count(); ++i )
		if ( g_Source.gameLumps[i].id == static_cast< uint32_t >( id ) ) index = i;
	if ( index == g_Source.gameLumps.Count() ) return false;
	const Bsp21GameLump &lump = g_Source.gameLumps[index];
	if ( bytes < 0 || lump.length > static_cast< uint32_t >( bytes ) || !lump.length ) return false;
	Bsp21Memory memory = { Engine<unsigned char *>( 0x69ECC0 ), Engine<uint32_t>( 0x69ECE0 ) };
	void *file = memory.data ? NULL : Open( &Engine<char>( 0x69FC20 ) );
	if ( !memory.data && !file ) return false;
	Bsp21Reader reader = memory.data ? Bsp21Reader{ &memory, memory.size, ReadMemoryAt } : MountedReader( file );
	bool loaded = Bsp21ReadPayload( reader, lump.offset, Bsp21GameStoredSize( g_Source, index ), lump.length, ( lump.flags & 1 ) != 0, data );
	if ( file ) Close( file );
	if ( !loaded ) return false;
	// All other consumers get the original payload. Only the native static-prop
	// manager's private destination is converted to its SDK record layout.
	if ( caller != reinterpret_cast< void * >( g_Engine + 0x2058E5 ) || id != 0x73707270 ) return true;
	if ( !Bsp21ValidateProps( static_cast< const unsigned char * >( data ), lump.length, g_Source, g_SourceName ) ) return false;
	if ( g_Source.propVersion != 8 && g_Source.propVersion != 9 ) return true;
	Bsp21Resize( g_PropTint, static_cast< int >( g_Source.propCount ) );
	unsigned stride = g_Source.propVersion == 8 ? 68 : 72;
	unsigned char *p = static_cast< unsigned char * >( data ) + g_Source.recordsOffset;
	unsigned char *native = p;
	for ( unsigned i = 0; i < g_Source.propCount; ++i, p += stride, native += ( g_Source.propVersion == 8 ? 64 : 72 ) )
	{
		g_PropTint[i].rgba = Read32( p + 64 );
		for ( int channel = 0; channel < 3; ++channel ) g_PropTint[i].rgb[channel] = p[64 + channel] * ( 1.0f / 255.0f );
		unsigned flags = p[31];
		// Preserve the established conversion: native CPU/GPU quality bytes,
		// authored alpha and force_disablex360 have no SDK field equivalent.
		if ( g_Source.propVersion == 8 )
		{
			memmove( native, p, 60 );
			Put32( native + 60, 0 );
		}
		else
		{
			native[31] = 0;
			Put32( native + 60, 0 );
			Put32( native + 64, flags | 0x100 );
			Put32( native + 68, 0 );
		}
	}
	g_PropCount = g_Source.propCount;
	BspLog( "sprp%u map=%s private manager view count=%u; RGB tint retained", g_Source.propVersion, g_SourceName, g_Source.propCount );
	return true;
}
static void PropsShutdown( void *self )
{
	HookCall call;
	g_PropsShutdown(self); g_PropCount=0;
	BspLog("static prop shutdown tint state released");
}

typedef int (*DrawFn)(void *,unsigned);
typedef void (*DrawArrayFn)(void *,void **,int,bool,bool);
static DrawFn g_Draw;
static DrawArrayFn g_DrawArray;
static int TintIndex( void *renderable )
{
	int index=*reinterpret_cast<int *>(static_cast<unsigned char *>(renderable)+60)&0x7fffffff;
	return index<g_PropCount && g_PropTint[index].rgba!=0xffffffff?index:-1;
}
static int Draw( void *renderable,unsigned flags )
{
	HookCall call;
	int index=g_PropCount?TintIndex(renderable):-1;
	if ( index<0 ) return g_Draw(renderable,flags);
	CMDLCacheCriticalSection cacheLock( reinterpret_cast<IMDLCache *>(Engine<void *>(0x7A7C20)) );
	float *color=&Engine<float>(0x4713E8); float saved[3]; memcpy(saved,color,sizeof(saved));
	memcpy(color,g_PropTint[index].rgb,sizeof(saved));
	int result=g_Draw(renderable,flags); memcpy(color,saved,sizeof(saved));
	return result;
}
static void DrawArray( void *self,void **props,int count,bool depth,bool collision )
{
	HookCall call;
	if ( !g_PropCount || collision || !*reinterpret_cast<int *>(Engine<uintptr_t>(0x77BF08)+88) ) { g_DrawArray(self,props,count,depth,collision); return; }
	int start=0;
	for(int i=0;i<count;++i)
	{
		if(TintIndex(props[i])<0) continue;
		if(i>start) g_DrawArray(self,props+start,i-start,depth,false);
		Draw(props[i],1u|(depth?0x40000000u:0)); start=i+1;
	}
	if(start<count) g_DrawArray(self,props+start,count-start,depth,false);
}

struct Hook
{
	uintptr_t rva, dedicatedRva;
	void *replacement;
	void **original;
	bool filesystem;
	bool clientOnly;
	const char *instructions;
};
#define HOOK(rva,fn,original,bytes) {rva,0,reinterpret_cast<void *>(fn),reinterpret_cast<void **>(&original),false,false,bytes}
#define CLIENT_HOOK(rva,fn,original,bytes) {rva,0,reinterpret_cast<void *>(fn),reinterpret_cast<void **>(&original),false,true,bytes}
static Hook g_Hooks[]={
	HOOK(0xFA130,MapValid,g_Valid,"\x48\x89\x5c\x24\x08\x57\x48\x81\xec\x50\x05\x00\x00\x41\x0f\xb6"),
	HOOK(0xFEBF0,DiskInit,g_Init,"\x48\x89\x5c\x24\x10\x57\x48\x83\xec\x30\x8b\x05\xa8\x11\x5a\x00"),
	HOOK(0x104C30,MemoryInit,g_Memory,"\x48\x89\x5c\x24\x10\x48\x89\x6c\x24\x18\x56\x48\x83\xec\x30\x83"),
	HOOK(0x163A60,CRC,g_CRC,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x56\x57\x41\x56\x48\x81"),
	HOOK(0x163D60,MD5,g_MD5,"\x48\x89\x5c\x24\x08\x48\x89\x74\x24\x10\x48\x89\x7c\x24\x20\x55"),
	HOOK(0x1042E0,WorldlightsEntry,g_Worldlights,"\x40\x56\x41\x56\x41\x57\x48\x83\xec\x50\x4c\x8b\x05\xaf\xba\x59"),
	HOOK(0x100CB0,GameVersion,g_GameVersion,"\x44\x8b\x0d\xe9\xdf\x59\x00\x41\x83\xe9\x01\x49\x63\xd1\x78\x22"),
	HOOK(0x101E60,GameLoad,g_GameLoad,"\x40\x55\x41\x55\x48\x83\xec\x38\x44\x8b\x1d\x31\xce\x59\x00\x4c"),
	HOOK(0x19FF20,AbortServer,g_AbortServer,"\x48\x83\xec\x28\xff\x05\xd6\x24\x5b\x00\x48\x8d\x0d\x4f\x27\x5b"),
	HOOK(0x205D50,PropsShutdown,g_PropsShutdown,"\x40\x53\x48\x83\xec\x20\x80\xb9\x98\x00\x00\x00\x00\x48\x8b\xd9"),
	CLIENT_HOOK(0x203420,Draw,g_Draw,"\x4c\x8b\xdc\x49\x89\x5b\x10\x49\x89\x6b\x18\x49\x89\x73\x20\x57"),
	CLIENT_HOOK(0x203B90,DrawArray,g_DrawArray,"\x4c\x8b\xdc\x49\x89\x5b\x08\x49\x89\x6b\x10\x49\x89\x73\x18\x57"),
	{0x19970,0x1D4B0,reinterpret_cast<void *>(PakRead),reinterpret_cast<void **>(&g_Fread),true,false,"\x4c\x8b\xdc\x49\x89\x5b\x08\x49\x89\x6b\x10\x49\x89\x73\x18\x49"},
	{0x194D0,0x1D010,reinterpret_cast<void *>(MountedOpen),reinterpret_cast<void **>(&g_Fopen),true,false,"\x48\x89\x5c\x24\x10\x57\x48\x81\xec\x70\x02\x00\x00\x49\x8b\xd8"},
	{0x5D300,0xC2AB0,reinterpret_cast<void *>(PackedRead),reinterpret_cast<void **>(&g_PackedRead),true,false,"\x4c\x89\x44\x24\x18\x48\x89\x4c\x24\x08\x55\x56\x41\x54\x41\x55"},
	{0x59B30,0xBF380,reinterpret_cast<void *>(PackedDestroy),reinterpret_cast<void **>(&g_PackedDestroy),true,false,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10\x48\x89\x74\x24\x18\x57"}
};
static HMODULE g_OwnerModule;
static bool g_HooksOwned;
static DWORD g_OwnerThread;
static IMaterialSystem *g_LifecycleMaterials;

class EnginePause
{
public:
	EnginePause() : m_FS(NULL), m_MaterialLock(NULL), m_FSPaused(false), m_PoolPaused(false), m_MaterialLocked(false) {}
	bool Begin()
	{
		if(!ThreadInMainThread()) { BspLog("REJECT hook transaction outside engine main thread"); return false; }
		m_FS=reinterpret_cast<IFileSystem *>(Engine<void *>(0x7500E0));
		if(!m_FS) { BspLog("REJECT hook transaction missing engine filesystem"); return false; }
		// Drain the material queue before pausing pools on which it may depend.
		if(g_LifecycleMaterials) { m_MaterialLock=g_LifecycleMaterials->Lock(); m_MaterialLocked=true; }
		m_FS->AsyncFinishAll(-1); // negative filesystem priority maps to JP_LOW: all priorities
		if(!m_FS->AsyncSuspend()) { BspLog("REJECT filesystem AsyncSuspend"); return false; }
		m_FSPaused=true;
		if(g_pThreadPool) { g_pThreadPool->SuspendExecution(); m_PoolPaused=true; }
		BspLog("QUIESCENCE render=%d filesystem=1 global_pool=%d main_thread=%lu",
			m_MaterialLocked,m_PoolPaused,GetCurrentThreadId());
		return true;
	}
	~EnginePause()
	{
		if(m_PoolPaused) g_pThreadPool->ResumeExecution();
		if(m_FSPaused && !m_FS->AsyncResume()) Fail("filesystem AsyncResume failed after hook transaction");
		if(m_MaterialLocked) g_LifecycleMaterials->Unlock(m_MaterialLock);
	}
private:
	IFileSystem *m_FS;
	MaterialLock_t m_MaterialLock;
	bool m_FSPaused, m_PoolPaused, m_MaterialLocked;
};

static void HookFailure( const char *operation, MH_STATUS status )
{
	DWORD thread=0,error=0;
	const char *detail=MH_BSP21Failure(&thread,&error);
	BspLog("REJECT %s status=%s detail=%s thread=%lu error=%08lx in_flight=%ld",
		operation,MH_StatusToString(status),detail,thread,error,InterlockedCompareExchange(&g_InFlight,0,0));
}
static void ReleaseSource()
{
	g_Source.gameLumps.RemoveAll();
	g_SourceName[0] = 0;
	g_PropTint.RemoveAll();
}
static bool ReleaseHooks()
{
	if(!g_HooksOwned) return true;
	// First stop all new detour entries, retaining every trampoline and the
	// owner reference. Only then drain native calls made by existing wrappers.
	MH_STATUS status=MH_DisableHook(MH_ALL_HOOKS);
	if(status!=MH_OK) { HookFailure("MH_DisableHook",status); return false; }
	const ULONGLONG deadline=GetTickCount64()+5000;
	do
	{
		status=MH_Uninitialize();
		if(status!=MH_ERROR_IN_FLIGHT) break;
		// This is a drain barrier, not a retry of failed suspension/context APIs.
		Sleep(1);
	} while(GetTickCount64()<deadline);
	if(status!=MH_OK) { HookFailure("MH_Uninitialize/drain",status); return false; }
	// MountedOpen can publish a raw-file object until its last wrapper returns.
	// Check the retained vtables only after no-new-entries and the owner-IP drain.
	if ( InterlockedCompareExchange( &g_MountedFiles, 0, 0 ) )
	{
		BspLog( "REJECT retirement with live mounted files=%ld; owner retained", g_MountedFiles );
		return false;
	}
	// No cache entry can be published after the owner instructions are drained.
	g_PackedVersions.RemoveAll();
	g_HooksOwned=false;
	BspLog("DRAIN all hook/native/trampoline calls complete; owner instruction windows clear");
	return true;
}
static bool ModulePath( const wchar_t *name, HMODULE module, wchar_t (&path)[MAX_PATH] )
{
	if(!module) { BspLog("REJECT required module absent name=%ls",name); return false; }
	DWORD length=GetModuleFileNameW(module,path,MAX_PATH);
	if(!length || length>=MAX_PATH)
	{
		BspLog("REJECT module path name=%ls length=%lu capacity=%d win32=%lu",name,length,MAX_PATH,GetLastError());
		return false;
	}
	return true;
}
bool L4D2_BSP21Install()
{
	if(!ThreadInMainThread()) { BspLog("REJECT installation outside engine main thread"); return false; }
	if(g_Installed || g_HooksOwned || g_OwnerModule) { BspLog("REJECT duplicate or retained installer ownership"); return false; }
	HMODULE engine=GetModuleHandleW(L"engine.dll"), filesystem=GetModuleHandleW(L"filesystem_stdio.dll");
	bool dedicated=filesystem==NULL;
	if(dedicated) filesystem=GetModuleHandleW(L"dedicated.dll");
	wchar_t enginePath[MAX_PATH],filesystemPath[MAX_PATH];
	if(!ModulePath(L"engine.dll",engine,enginePath) ||
		!ModulePath(dedicated?L"dedicated.dll":L"filesystem_stdio.dll",filesystem,filesystemPath)) return false;
	if(!CheckFile(enginePath,"adba182f610ae3ccac3e42430e23c3eefc3f5f3b6b36d1b7b09225ec6016c5a6") ||
		!CheckFile(filesystemPath,dedicated?"35679c6738164f0058598ae9293f6967cc7f2ef1d2b0e278c781343758effe39":"a8700336a3fd448515798d26d3df2a0fda351e569887b8af2ed30cc97331decb")) return false;
	g_Engine=reinterpret_cast<uintptr_t>(engine); g_Filesystem=reinterpret_cast<uintptr_t>(filesystem);
	if(g_PropCount || Engine<int>(0x69FDA8)) { BspLog("REJECT install with live map helpers/props"); return false; }
	g_PakReturn=dedicated?0xC783:0x4143;
	for ( Hook &hook : g_Hooks ) if ( hook.filesystem && dedicated ) hook.rva = hook.dedicatedRva;
	const unsigned char embeddedOffset[] = { 0x81,0x3e,0xff,0x7f,0,0,0x75,0x0d,0x41,0x8b,0x86,0x0c,2,0,0,0x83,0xc0,0x1c,0x44,3,0xf8 };
	if ( memcmp( reinterpret_cast< void * >( g_Filesystem + ( dedicated ? 0xC2BAA : 0x5D3FA ) ), embeddedOffset, sizeof( embeddedOffset ) ) )
	{
		BspLog( "REJECT packed-store embedded-offset instruction mismatch" );
		return false;
	}
	const unsigned char pakCall[]={0xff,0x90,0xd0,3,0,0};
	if(memcmp(reinterpret_cast<void *>(g_Filesystem+g_PakReturn-6),pakCall,sizeof(pakCall))) { BspLog("REJECT embedded callsite bytes"); return false; }
	if(memcmp(reinterpret_cast<void *>(g_Engine+0x2064D4),"\xe8\xd7\xa7\xef\xff",5) ||
		memcmp(reinterpret_cast<void *>(g_Engine+0x2058E0),"\xe8\x7b\xc5\xef\xff",5))
	{
		BspLog("REJECT native sprp consumer callsite bytes"); return false;
	}
	for(const Hook &hook:g_Hooks)
	{
		if ( dedicated && hook.clientOnly )
			continue;
		const void *address=reinterpret_cast<void *>((hook.filesystem?g_Filesystem:g_Engine)+hook.rva);
		if(memcmp(address,hook.instructions,16)) { BspLog("REJECT live instruction mismatch module=%s rva=%zx",hook.filesystem?"filesystem-owner":"engine",hook.rva); return false; }
	}
	g_LifecycleMaterials=NULL;
	if(!dedicated)
	{
		HMODULE module=GetModuleHandleW(L"materialsystem.dll");
		CreateInterfaceFn factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):NULL;
		if(factory) g_LifecycleMaterials=static_cast<IMaterialSystem *>(factory(MATERIAL_SYSTEM_INTERFACE_VERSION,NULL));
		if(!g_LifecycleMaterials) { BspLog("REJECT client material-system quiescence interface"); return false; }
	}
	EnginePause pause;
	if(!pause.Begin()) { ReleaseSource(); return false; }
	if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
		reinterpret_cast<LPCWSTR>(&L4D2_BSP21Install),&g_OwnerModule))
	{
		BspLog("REJECT owner module retention win32=%lu",GetLastError()); ReleaseSource(); return false;
	}
	g_OwnerThread=GetCurrentThreadId();
	MH_STATUS status=MH_Initialize();
	if(status!=MH_OK)
	{
		BspLog("REJECT MH_Initialize=%s",MH_StatusToString(status));
		ReleaseSource(); FreeLibrary(g_OwnerModule); g_OwnerModule=NULL; return false;
	}
	g_HooksOwned=true;
	const IMAGE_DOS_HEADER *dos=reinterpret_cast<const IMAGE_DOS_HEADER *>(g_OwnerModule);
	const IMAGE_NT_HEADERS64 *nt=reinterpret_cast<const IMAGE_NT_HEADERS64 *>(reinterpret_cast<const unsigned char *>(dos)+dos->e_lfanew);
	MH_BSP21SetOwner(g_OwnerModule,nt->OptionalHeader.SizeOfImage,&g_InFlight);
	size_t hookCount = 0;
	for(const Hook &hook:g_Hooks)
	{
		// Dedicated hosts have no client render state for the tint wrappers.
		if ( dedicated && hook.clientOnly )
			continue;
		void *address=reinterpret_cast<void *>((hook.filesystem?g_Filesystem:g_Engine)+hook.rva);
		status=MH_CreateHook(address,hook.replacement,hook.original);
		if(status!=MH_OK) { BspLog("REJECT MH_CreateHook rva=%zx status=%s",hook.rva,MH_StatusToString(status)); break; }
		status=MH_QueueEnableHook(address);
		if(status!=MH_OK) { BspLog("REJECT MH_QueueEnableHook rva=%zx status=%s",hook.rva,MH_StatusToString(status)); break; }
		++hookCount;
	}
	if(status==MH_OK) status=MH_ApplyQueued();
	if(status!=MH_OK)
	{
		HookFailure("installation",status);
		if(!ReleaseHooks()) Fail("installation rollback failed; cannot return ordinary initialization failure");
		ReleaseSource(); FreeLibrary(g_OwnerModule); g_OwnerModule=NULL; return false;
	}
	g_Installed=true;
	BspLog("INSTALL module-pinned BSP21/VPK readers hooks=%zu thread=%lu",hookCount,g_OwnerThread);
	return true;
}
bool L4D2_BSP21Remove()
{
	if(!g_HooksOwned) return true;
	if(GetCurrentThreadId()!=g_OwnerThread || !ThreadInMainThread())
	{
		BspLog("REJECT removal outside installing engine main thread"); return false;
	}
	if ( g_PropCount || Engine<int>( 0x69FDA8 ) )
	{
		BspLog( "REJECT removal with live props=%d map_helpers=%d", g_PropCount, Engine<int>( 0x69FDA8 ) ); return false;
	}
	EnginePause pause;
	if(!pause.Begin() || !ReleaseHooks()) return false;
	g_Installed=false; g_SourceMap=false;
	ReleaseSource();
	HMODULE owner=g_OwnerModule; g_OwnerModule=NULL; g_OwnerThread=0;
	FreeLibrary(owner);
	BspLog("REMOVE detours restored; source/owner retained resources released");
	return true;
}
