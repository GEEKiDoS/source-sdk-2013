//========= Copyright Valve Corporation, All rights reserved. ============//
// Included by adapter.cpp after its filesystem helpers. The BSP is read only
// for identity; the writable carrier is the existing, loose .light file.
#include "hlight_bsp.h"
#include "tier1/lzmaDecoder.h"
#include "tier1/utlstring.h"
#include "tier1/utlvector.h"
#include <algorithm>
#include <limits.h>

struct CubemapMember
{
	CUtlString name;
	CUtlVector< unsigned char > data;
};

namespace cubemap_sidecar
{
constexpr uint32 kMaxDirectoryBytes = 64 * 1024 * 1024;
constexpr uint32 kMaxCubemapBytes = 64 * 1024 * 1024;
constexpr uint32 kMaxDictionaryBytes = 64 * 1024 * 1024;
constexpr uint32 kIoBytes = 64 * 1024;
constexpr uint32 kZipDescriptor = 1u << 3;
constexpr uint32 kZipEncrypted = ( 1u << 0 ) | ( 1u << 6 ) | ( 1u << 13 );
static_assert( sizeof( ZIP_LocalFileHeader ) == 30 && sizeof( ZIP_FileHeader ) == 46 &&
	sizeof( ZIP_EndOfCentralDirRecord ) == 22, "ZIP wire layout" );

struct Entry
{
	CUtlString name;
	ZIP_FileHeader zip;
	uint32 dataOffset, localEnd; // relative to the pak
	uint32 centralOffset, centralBytes; // relative to the in-memory directory
};

bool Range( uint32 start, uint32 length, uint32 limit )
{
	return start <= limit && length <= limit - start;
}

bool Normalize( const char *path, size_t bytes, char ( &name )[MAX_PATH], bool directory = false )
{
	if ( !path || !bytes || bytes >= sizeof( name ) ) return false;
	size_t component = 0;
	for ( size_t i = 0; i <= bytes; ++i )
	{
		if ( i == bytes || path[i] == '/' || path[i] == '\\' )
		{
			const size_t n = i - component;
			if ( !n && !( directory && i == bytes ) ) return false;
			if ( ( n == 1 && path[component] == '.' ) ||
				( n == 2 && path[component] == '.' && path[component + 1] == '.' ) ) return false;
			name[i] = i == bytes ? '\0' : '/';
			component = i + 1;
			continue;
		}
		const unsigned char c = path[i];
		if ( c < 32 || c == 127 || strchr( "\":*?<>|", c ) ) return false;
		name[i] = c >= 'A' && c <= 'Z' ? char( c + ( 'a' - 'A' ) ) : char( c );
	}
	return true;
}

bool MapPaths( const char *bsp, char ( &map )[MAX_PATH], char ( &light )[MAX_PATH] )
{
	char name[MAX_PATH];
	if ( !bsp || !Normalize( bsp, strlen( bsp ), name ) ) return false;
	const char *stem = !strncmp( name, "maps/", 5 ) ? name + 5 : name;
	const size_t n = strlen( stem );
	const bool hasExtension = n > 4 && !strcmp( stem + n - 4, ".bsp" );
	const int written = V_snprintf( map, sizeof( map ), "maps/%s%s", stem, hasExtension ? "" : ".bsp" );
	return *stem && written > 9 && written < int( sizeof( map ) ) && hlight::SidecarPath( map, light, sizeof( light ) );
}

bool Coordinate( const char *&p )
{
	const bool negative = *p == '-';
	if ( negative ) ++p;
	if ( *p < '0' || *p > '9' ) return false;
	uint64 value = 0;
	do
	{
		value = value * 10 + ( *p++ - '0' );
		if ( value > uint64( INT_MAX ) + negative ) return false;
	} while ( *p >= '0' && *p <= '9' );
	return true;
}

bool TextureInFolder( const char *path, const char *folder, size_t folderBytes )
{
	if ( strncmp( path, folder, folderBytes ) || path[folderBytes] != '/' ) return false;
	const char *leaf = path + folderBytes + 1;
	if ( !strcmp( leaf, "cubemapdefault.vtf" ) || !strcmp( leaf, "cubemapdefault.hdr.vtf" ) ) return true;
	if ( *leaf++ != 'c' || !Coordinate( leaf ) || *leaf++ != '_' || !Coordinate( leaf ) ||
		*leaf++ != '_' || !Coordinate( leaf ) ) return false;
	return !strcmp( leaf, ".vtf" ) || !strcmp( leaf, ".hdr.vtf" );
}

bool TextureName( const char *map, const char *path )
{
	if ( strncmp( path, "materials/maps/", 15 ) ) return false;
	path += 15;
	const char *folder = map + 5; // normalized maps/<relative stem>.bsp
	if ( TextureInFolder( path, folder, strlen( folder ) - 4 ) ) return true;
	// VBSP's defaults use the map basename, including for workshop maps.
	const char *base = strrchr( folder, '/' );
	return base && TextureInFolder( path, base + 1, strlen( base + 1 ) - 4 );
}

bool SameFile( const BY_HANDLE_FILE_INFORMATION &a, const BY_HANDLE_FILE_INFORMATION &b )
{
	return a.dwVolumeSerialNumber == b.dwVolumeSerialNumber && a.nFileIndexHigh == b.nFileIndexHigh &&
		a.nFileIndexLow == b.nFileIndexLow && a.nFileSizeHigh == b.nFileSizeHigh && a.nFileSizeLow == b.nFileSizeLow &&
		!CompareFileTime( &a.ftLastWriteTime, &b.ftLastWriteTime );
}

class DiskFile
{
public:
	DiskFile() = default;
	DiskFile( const DiskFile & ) = delete;
	DiskFile &operator=( const DiskFile & ) = delete;
	~DiskFile() { CloseHandleIfOpen(); }

	bool Open( const char *path )
	{
		// Do not follow a .light symlink or allow in-place writes during a read.
		m_File = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, NULL,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, NULL );
		return m_File != INVALID_HANDLE_VALUE && GetFileType( m_File ) == FILE_TYPE_DISK &&
			GetFileInformationByHandle( m_File, &info ) && !( info.dwFileAttributes & ( FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT ) ) &&
			!info.nFileSizeHigh && info.nFileSizeLow <= INT_MAX;
	}
	bool Create( const char *path )
	{
		m_File = CreateFileA( path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
		return m_File != INVALID_HANDLE_VALUE;
	}
	bool CloseHandleIfOpen()
	{
		if ( m_File == INVALID_HANDLE_VALUE ) return true;
		const bool ok = CloseHandle( m_File ) != FALSE;
		m_File = INVALID_HANDLE_VALUE;
		return ok;
	}
	uint32 Size() const { return info.nFileSizeLow; }
	bool Seek( uint32 offset )
	{
		LARGE_INTEGER pos;
		pos.QuadPart = offset;
		return SetFilePointerEx( m_File, pos, NULL, FILE_BEGIN ) != FALSE;
	}
	bool Read( void *out, uint32 bytes )
	{
		DWORD done = 0;
		return !bytes || ( ReadFile( m_File, out, bytes, &done, NULL ) && done == bytes );
	}
	bool Write( const void *data, uint32 bytes )
	{
		DWORD done = 0;
		return !bytes || ( WriteFile( m_File, data, bytes, &done, NULL ) && done == bytes );
	}
	bool Finish()
	{
		const bool flushed = FlushFileBuffers( m_File ) != FALSE;
		return CloseHandleIfOpen() && flushed;
	}
	static bool ReadAt( void *context, uint32 offset, uint32 bytes, void *out )
	{
		DiskFile &f = *static_cast< DiskFile * >( context );
		return Range( offset, bytes, f.Size() ) && f.Seek( offset ) && f.Read( out, bytes );
	}
	BY_HANDLE_FILE_INFORMATION info = {};
private:
	HANDLE m_File = INVALID_HANDLE_VALUE;
};

class MountedMap
{
public:
	explicit MountedMap( const char *path ) : file( ::Open( path ) ), bytes( file ? FileSystem()->Size( file ) : 0 ) {}
	~MountedMap() { if ( file ) ::Close( file ); }
	static bool ReadAt( void *context, uint32 offset, uint32 length, void *out )
	{
		const MountedMap &map = *static_cast< MountedMap * >( context );
		return map.file && Range( offset, length, map.bytes ) && Range( offset, length, INT_MAX ) &&
			( !length || ReadMountedAt( map.file, offset, out, length ) );
	}
	void *const file;
	const uint32 bytes;
};

bool ValidProperties( const uint8 *props )
{
	uint32 dictionary;
	memcpy( &dictionary, props + 1, sizeof( dictionary ) );
	return props[0] < 225 && dictionary <= kMaxDictionaryBytes;
}

bool DecodeCRC( MountedMap &map, const ShadowMapBspLumpInfo &source, uint32 decodedBytes, uint32 &crc )
{
	uint8 scratch[16384];
	if ( !source.uncompressedSize )
		return ShadowMap_CRC32Lump( MountedMap::ReadAt, &map, source, crc, scratch, sizeof( scratch ) );

	// Same native-lump contract as ValidateServerManifestSources: hash the
	// original, decoded BSP domain (not the adapter's normalized consumers).
	uint8 compressed[16384];
	uint32 input = MIN( source.filelen, uint32( sizeof( compressed ) ) );
	lzma_header_t header;
	if ( !Range( source.fileofs, source.filelen, map.bytes ) || input <= sizeof( header ) ||
		!MountedMap::ReadAt( &map, source.fileofs, input, compressed ) ) return false;
	memcpy( &header, compressed, sizeof( header ) );
	if ( header.id != LZMA_ID || header.actualSize != decodedBytes || !decodedBytes ||
		header.lzmaSize != source.filelen - sizeof( header ) || !ValidProperties( header.properties ) ) return false;

	CLZMAStream stream;
	CRC32_t c;
	CRC32_Init( &c );
	uint32 fileRead = input, used = 0, output = 0;
	while ( true )
	{
		unsigned int consumed = 0, produced = 0;
		if ( !stream.Read( compressed + used, input - used, scratch, sizeof( scratch ), consumed, produced ) ||
			consumed > input - used || produced > decodedBytes - output || ( !consumed && !produced ) ) return false;
		used += consumed;
		output += produced;
		CRC32_ProcessBuffer( &c, scratch, produced );
		if ( used == input && fileRead == source.filelen && output == decodedBytes ) break;
		if ( used == input )
		{
			input = MIN( source.filelen - fileRead, uint32( sizeof( compressed ) ) );
			if ( input && !MountedMap::ReadAt( &map, source.fileofs + fileRead, input, compressed ) ) return false;
			used = 0;
			fileRead += input;
		}
	}
	CRC32_Final( &c );
	crc = c;
	return true;
}

bool MatchesMap( const char *path, const hlight::ManifestModeView &view )
{
	MountedMap map( path );
	ShadowMapBspLumpInfo lumps[HEADER_LUMPS];
	uint32 offset, bytes, version, flags;
	bool compressed;
	if ( !ShadowMap_ReadBspDirectory( MountedMap::ReadAt, &map, lumps, offset, bytes, version, compressed, flags ) ) return false;
	const hlight::ManifestModeDisk &mode = *view.record;
	const uint32 ids[] = { mode.faceLump, mode.lightingLump, mode.worldlightsLump };
	const uint32 expected[] = { mode.facesCRC32, mode.lightingCRC32, mode.worldlightsCRC32 };
	const ShadowMapBspLumpInfo &faces = lumps[ids[0]], &lighting = lumps[ids[1]], &world = lumps[ids[2]];
	const uint32 faceBytes = faces.uncompressedSize ? faces.uncompressedSize : faces.filelen;
	const uint32 lightBytes = lighting.uncompressedSize ? lighting.uncompressedSize : lighting.filelen;
	const uint32 worldBytes = world.uncompressedSize ? world.uncompressedSize : world.filelen;
	const uint32 worldStride = world.version == 1 ? 100 : sizeof( dworldlight_t );
	if ( faces.version != LUMP_FACES_VERSION || lighting.version != LUMP_LIGHTING_VERSION ||
		!faceBytes || faceBytes % sizeof( dface_t ) || faceBytes / sizeof( dface_t ) > MAX_MAP_FACES ||
		lightBytes != mode.lightingBytes || worldBytes % worldStride || worldBytes / worldStride > MAX_MAP_WORLDLIGHTS ||
		( view.removedWorldlightCount && view.removedWorldlights[view.removedWorldlightCount - 1] >= worldBytes / worldStride ) ) return false;
	const uint32 decoded[] = { faceBytes, lightBytes, worldBytes };
	for ( int i = 0; i < ARRAYSIZE( ids ); ++i )
	{
		uint32 crc;
		if ( !DecodeCRC( map, lumps[ids[i]], decoded[i], crc ) || crc != expected[i] ) return false;
	}
	return true;
}

bool Manifest( DiskFile &file, const char *map, const ShadowMapBspLumpInfo ( &lumps )[HEADER_LUMPS],
	uint32 offset, uint32 bytes, uint32 version, bool compressed, uint32 flags, char ( &asset )[MAX_PATH] )
{
	const ShadowMapBspLumpInfo &game = lumps[LUMP_GAME_LUMP];
	const ShadowMapBspLumpInfo &mapFlags = lumps[LUMP_MAP_FLAGS];
	const uint32 maxBytes = sizeof( hlight::ManifestDisk ) + MAX_MAP_WORLDLIGHTS * ( sizeof( ShadowMapLightDisk ) + sizeof( uint32 ) );
	if ( compressed || game.uncompressedSize || mapFlags.uncompressedSize || mapFlags.filelen < sizeof( flags ) ||
		version != hlight::kManifestVersion || bytes < sizeof( hlight::ManifestDisk ) || bytes > maxBytes ) return false;
	uint32 count;
	if ( game.filelen < sizeof( count ) || !DiskFile::ReadAt( &file, game.fileofs, sizeof( count ), &count ) ||
		!count || count > 256 ) return false;
	const uint32 payload = game.fileofs + sizeof( count ) + count * sizeof( dgamelump_t );
	if ( offset < payload || !Range( offset, bytes, game.fileofs + game.filelen ) ) return false;
	bool seen = false;
	for ( uint32 i = 0; i < count; ++i )
	{
		dgamelump_t entry;
		if ( !DiskFile::ReadAt( &file, game.fileofs + sizeof( count ) + i * sizeof( entry ), sizeof( entry ), &entry ) ||
			entry.fileofs < 0 || entry.filelen < 0 ) return false;
		if ( entry.filelen && ( uint32( entry.fileofs ) < payload ||
			!Range( entry.fileofs, ( entry.flags & 1 ) ? 1u : uint32( entry.filelen ), game.fileofs + game.filelen ) ) ) return false;
		if ( entry.id == GAMELUMP_RESTIR_SHADOWMAPS )
		{
			if ( seen ) return false;
			seen = true;
		}
	}
	CUtlVector< uint8 > data;
	data.SetCount( bytes );
	hlight::ManifestView view;
	if ( !seen || !DiskFile::ReadAt( &file, offset, bytes, data.Base() ) ||
		!hlight::ValidateManifest( data.Base(), bytes, flags, view, NULL, 0 ) ||
		!Normalize( view.header->assetPath, strlen( view.header->assetPath ), asset ) ) return false;
	return MatchesMap( map, view.mode[SHADOWMAP_MODE_HDR] );
}
} // namespace cubemap_sidecar

struct CubemapSidecar
{
	char bsp[MAX_PATH] = {}, path[MAX_PATH] = {}; // mounted BSP, absolute loose sidecar
	BY_HANDLE_FILE_INFORMATION file = {};
	uint32 pakOffset = 0, pakBytes = 0, alignment = 0;
	int bspVersion = 0;
	ZIP_EndOfCentralDirRecord end = {};
	bool compatible = true, valid = false;
	CUtlVector< uint8 > directory, comment;
	CUtlVector< cubemap_sidecar::Entry > entries;
	CUtlVector< int > byName, byLocal;

	void Swap( CubemapSidecar &other )
	{
		std::swap( bsp, other.bsp ); std::swap( path, other.path ); std::swap( file, other.file );
		std::swap( pakOffset, other.pakOffset ); std::swap( pakBytes, other.pakBytes ); std::swap( alignment, other.alignment );
		std::swap( bspVersion, other.bspVersion ); std::swap( end, other.end );
		std::swap( compatible, other.compatible ); std::swap( valid, other.valid );
		directory.Swap( other.directory ); comment.Swap( other.comment ); entries.Swap( other.entries );
		byName.Swap( other.byName ); byLocal.Swap( other.byLocal );
	}

	int Find( const char *name ) const
	{
		int lo = 0, hi = byName.Count();
		while ( lo < hi )
		{
			const int mid = lo + ( hi - lo ) / 2;
			const int order = strcmp( entries[byName[mid]].name.Get(), name );
			if ( order == 0 ) return byName[mid];
			if ( order < 0 ) lo = mid + 1;
			else hi = mid;
		}
		return -1;
	}
};

namespace cubemap_sidecar
{
bool ReadLocal( DiskFile &file, const CubemapSidecar &state, const char *name, Entry &entry )
{
	const ZIP_FileHeader &zip = entry.zip;
	const uint32 local = zip.relativeOffsetOfLocalHeader;
	const uint32 limit = state.end.startOfCentralDirOffset;
	ZIP_LocalFileHeader header;
	if ( !Range( local, sizeof( header ), limit ) || !DiskFile::ReadAt( &file, state.pakOffset + local, sizeof( header ), &header ) ||
		header.signature != PKID( 3,4 ) || header.flags != zip.flags || header.compressionMethod != zip.compressionMethod ||
		header.fileNameLength != zip.fileNameLength ) return false;
	const uint32 fields = uint32( header.fileNameLength ) + header.extraFieldLength;
	if ( !Range( local + sizeof( header ), fields, limit ) ) return false;
	char localName[MAX_PATH];
	if ( !file.Read( localName, header.fileNameLength ) || memcmp( localName, name, header.fileNameLength ) ) return false;
	entry.dataOffset = local + sizeof( header ) + fields;
	if ( !Range( entry.dataOffset, zip.compressedSize, limit ) ) return false;
	entry.localEnd = entry.dataOffset + zip.compressedSize;
	if ( !( zip.flags & kZipDescriptor ) )
		return header.crc32 == zip.crc32 && header.compressedSize == zip.compressedSize && header.uncompressedSize == zip.uncompressedSize;

	// Verify the optional classic (not ZIP64) data descriptor, signature or not.
	uint32 desc[4];
	const uint32 available = MIN( limit - entry.localEnd, uint32( sizeof( desc ) ) );
	if ( available < 12 || !DiskFile::ReadAt( &file, state.pakOffset + entry.localEnd, available, desc ) ) return false;
	const uint32 skip = available >= 16 && desc[0] == PKID( 7,8 ) &&
		desc[1] == zip.crc32 && desc[2] == zip.compressedSize && desc[3] == zip.uncompressedSize ? 1 : 0;
	if ( desc[skip] != zip.crc32 || desc[skip + 1] != zip.compressedSize || desc[skip + 2] != zip.uncompressedSize ) return false;
	entry.localEnd += ( 3 + skip ) * sizeof( uint32 );
	return true;
}

bool AllZero( const uint8 *data, uint32 bytes )
{
	while ( bytes-- ) if ( *data++ ) return false;
	return true;
}

bool ReadZip( DiskFile &file, const ShadowMapBspLumpInfo &pak, CubemapSidecar &state )
{
	if ( !hlight::LocatePakDirectory( DiskFile::ReadAt, &file, pak, state.end, state.compatible ) ) return false;
	const ZIP_EndOfCentralDirRecord &end = state.end;
	const uint32 centralEnd = pak.filelen - sizeof( end ) - end.commentLength;
	if ( !end.nCentralDirectoryEntries_Total || end.nCentralDirectoryEntries_Total == 0xffff ||
		end.centralDirectorySize > kMaxDirectoryBytes || end.startOfCentralDirOffset + end.centralDirectorySize != centralEnd ) return false;
	state.directory.SetCount( end.centralDirectorySize );
	state.comment.SetCount( end.commentLength );
	if ( !DiskFile::ReadAt( &file, pak.fileofs + end.startOfCentralDirOffset, end.centralDirectorySize, state.directory.Base() ) ||
		!DiskFile::ReadAt( &file, pak.fileofs + centralEnd + sizeof( end ), end.commentLength, state.comment.Base() ) ) return false;
	if ( end.commentLength >= 6 && !V_strnicmp( reinterpret_cast< const char * >( state.comment.Base() ), "XZP", 3 ) &&
		( state.comment[3] == '1' || state.comment[3] == '2' ) )
	{
		if ( state.comment[4] != ' ' ) return false;
		for ( int i = 5; i < state.comment.Count() && state.comment[i]; ++i )
		{
			if ( state.comment[i] < '0' || state.comment[i] > '9' ) return false;
			state.alignment = state.alignment * 10 + state.comment[i] - '0';
			if ( state.alignment > 32768 ) return false;
		}
		if ( state.alignment & ( state.alignment - 1 ) ) return false;
	}
	uint32 cursor = 0;
	const int count = end.nCentralDirectoryEntries_Total;
	if ( uint64( count ) * sizeof( ZIP_FileHeader ) > end.centralDirectorySize ) return false;
	state.entries.SetCount( count );
	state.byName.SetCount( count );
	state.byLocal.SetCount( count );
	for ( int i = 0; i < count; ++i )
	{
		Entry &entry = state.entries[i];
		ZIP_FileHeader &zip = entry.zip;
		if ( !Range( cursor, sizeof( zip ), end.centralDirectorySize ) ) return false;
		memcpy( &zip, state.directory.Base() + cursor, sizeof( zip ) );
		if ( zip.signature != PKID( 1,2 ) || zip.diskNumberStart || ( zip.flags & kZipEncrypted ) ||
			zip.uncompressedSize > INT_MAX || ( zip.compressionMethod == ZIP_COMPRESSION_NONE && zip.compressedSize != zip.uncompressedSize ) ) return false;
		entry.centralOffset = cursor;
		entry.centralBytes = sizeof( zip ) + zip.fileNameLength + ( state.compatible ? uint32( zip.extraFieldLength ) + zip.fileCommentLength : 0 );
		if ( !Range( cursor, entry.centralBytes, end.centralDirectorySize ) ) return false;
		const char *raw = reinterpret_cast< const char * >( state.directory.Base() + cursor + sizeof( zip ) );
		char name[MAX_PATH];
		if ( !Normalize( raw, zip.fileNameLength, name, true ) || !strcmp( name, PRELOAD_SECTION_NAME ) ||
			!ReadLocal( file, state, raw, entry ) ) return false;
		entry.name = name;
		state.byName[i] = state.byLocal[i] = i;
		cursor += entry.centralBytes;
	}
	const uint32 tail = end.centralDirectorySize - cursor;
	if ( tail && ( !state.alignment || tail >= state.alignment || !AllZero( state.directory.Base() + cursor, tail ) ) ) return false;
	std::sort( state.byName.Base(), state.byName.Base() + count, [&]( int a, int b ) {
		return strcmp( state.entries[a].name.Get(), state.entries[b].name.Get() ) < 0;
	} );
	std::sort( state.byLocal.Base(), state.byLocal.Base() + count, [&]( int a, int b ) {
		return state.entries[a].zip.relativeOffsetOfLocalHeader < state.entries[b].zip.relativeOffsetOfLocalHeader;
	} );
	for ( int i = 1; i < count; ++i )
	{
		if ( !strcmp( state.entries[state.byName[i-1]].name.Get(), state.entries[state.byName[i]].name.Get() ) ||
			state.entries[state.byLocal[i-1]].localEnd > state.entries[state.byLocal[i]].zip.relativeOffsetOfLocalHeader ) return false;
	}
	return true;
}

bool AssetEntry( DiskFile &file, const CubemapSidecar &state, int index )
{
	if ( index < 0 ) return false;
	const Entry &entry = state.entries[index];
	const uint64 sourceDataOffset = uint64( entry.zip.relativeOffsetOfLocalHeader ) + sizeof( ZIP_LocalFileHeader ) +
		entry.zip.fileNameLength + entry.zip.extraFieldLength;
	if ( !entry.zip.uncompressedSize || sourceDataOffset != entry.dataOffset ) return false;
	if ( entry.zip.compressionMethod == ZIP_COMPRESSION_NONE ) return entry.zip.compressedSize == entry.zip.uncompressedSize;
	uint8 header[9];
	return entry.zip.compressionMethod == ZIP_COMPRESSION_LZMA && entry.zip.compressedSize > sizeof( header ) &&
		DiskFile::ReadAt( &file, state.pakOffset + entry.dataOffset, sizeof( header ), header ) &&
		header[2] == 5 && !header[3] && ValidProperties( header + 4 );
}
} // namespace cubemap_sidecar

static bool IsCubemapMemberName( const char *bsp, const char *name )
{
	using namespace cubemap_sidecar;
	char map[MAX_PATH], light[MAX_PATH], normalized[MAX_PATH];
	return name && Normalize( name, strlen( name ), normalized ) && MapPaths( bsp, map, light ) && TextureName( map, normalized );
}

static bool LoadCubemapSidecar( const char *bsp, CubemapSidecar &state )
{
	using namespace cubemap_sidecar;
	// A failed load must never leave the previous map's generation usable.
	state.valid = false;
	state.directory.Purge(); state.comment.Purge(); state.entries.Purge(); state.byName.Purge(); state.byLocal.Purge();
	state.alignment = 0;
	char map[MAX_PATH], light[MAX_PATH];
	if ( !FileSystem() || !MapPaths( bsp, map, light ) ) return false;
	PathTypeQuery_t kind = PATH_IS_NORMAL;
	state.path[0] = 0;
	if ( !FileSystem()->RelativePathToFullPath( light, "GAME", state.path, sizeof( state.path ), FILTER_NONE, &kind ) ||
		kind != PATH_IS_NORMAL || !V_IsAbsolutePath( state.path ) ) return false;
	const char *extension = V_GetFileExtension( state.path );
	if ( !extension || V_stricmp( extension, "light" ) ) return false;
	DiskFile file;
	dheader_t header;
	ShadowMapBspLumpInfo lumps[HEADER_LUMPS];
	uint32 offset, bytes, version, flags;
	bool compressed;
	if ( !file.Open( state.path ) || !DiskFile::ReadAt( &file, 0, sizeof( header ), &header ) ||
		!ShadowMap_ReadBspDirectory( DiskFile::ReadAt, &file, lumps, offset, bytes, version, compressed, flags ) ) return false;
	const ShadowMapBspLumpInfo &pak = lumps[LUMP_PAKFILE];
	if ( !pak.filelen || pak.uncompressedSize ) return false;
	for ( int i = 0; i < HEADER_LUMPS; ++i )
	{
		const ShadowMapBspLumpInfo &l = lumps[i];
		if ( l.filelen && ( l.fileofs < sizeof( header ) || !Range( l.fileofs, l.filelen, file.Size() ) ||
			( i != LUMP_PAKFILE && l.fileofs < pak.fileofs + pak.filelen && pak.fileofs < l.fileofs + l.filelen ) ) ) return false;
	}
	V_strncpy( state.bsp, map, sizeof( state.bsp ) );
	state.bspVersion = header.version;
	state.pakOffset = pak.fileofs;
	state.pakBytes = pak.filelen;
	state.file = file.info;
	char asset[MAX_PATH];
	if ( !ReadZip( file, pak, state ) || !Manifest( file, map, lumps, offset, bytes, version, compressed, flags, asset ) ||
		!AssetEntry( file, state, state.Find( asset ) ) ) return false;
	memcpy( asset + strlen( asset ) - 7, ".hprobe", 8 ); // ValidateManifest proved the .hlight suffix.
	const int probe = state.Find( asset );
	if ( probe >= 0 && !AssetEntry( file, state, probe ) ) return false;
	state.valid = true;
	return true;
}

static bool ReadCubemapSidecarMember( const CubemapSidecar &state, const char *name, CUtlVector< unsigned char > &data )
{
	using namespace cubemap_sidecar;
	data.RemoveAll();
	char normalized[MAX_PATH];
	if ( !state.valid || !name || !Normalize( name, strlen( name ), normalized ) || !TextureName( state.bsp, normalized ) ) return false;
	const int index = state.Find( normalized );
	if ( index < 0 ) return false;
	const Entry &entry = state.entries[index];
	const ZIP_FileHeader &zip = entry.zip;
	if ( !zip.uncompressedSize || zip.uncompressedSize > kMaxCubemapBytes || zip.compressedSize > kMaxCubemapBytes ||
		( zip.compressionMethod != ZIP_COMPRESSION_NONE && zip.compressionMethod != ZIP_COMPRESSION_LZMA ) ) return false;
	DiskFile file;
	if ( !file.Open( state.path ) || !SameFile( file.info, state.file ) ) return false;
	const uint32 offset = state.pakOffset + entry.dataOffset;
	data.SetCount( zip.uncompressedSize );
	bool ok = false;
	if ( zip.compressionMethod == ZIP_COMPRESSION_NONE )
	{
		ok = DiskFile::ReadAt( &file, offset, zip.compressedSize, data.Base() );
	}
	else
	{
		uint8 packed[kIoBytes];
		uint32 input = MIN( zip.compressedSize, uint32( sizeof( packed ) ) );
		if ( input > 9 && DiskFile::ReadAt( &file, offset, input, packed ) &&
			packed[2] == 5 && !packed[3] && ValidProperties( packed + 4 ) )
		{
			CLZMAStream stream;
			stream.InitZIPHeader( zip.compressedSize, zip.uncompressedSize );
			uint32 fileRead = input, used = 0, output = 0;
			while ( true )
			{
				unsigned int consumed = 0, produced = 0;
				if ( !stream.Read( packed + used, input - used, data.Base() + output,
					zip.uncompressedSize - output, consumed, produced ) ||
					consumed > input - used || produced > zip.uncompressedSize - output || ( !consumed && !produced ) ) break;
				used += consumed;
				output += produced;
				if ( used == input && fileRead == zip.compressedSize && output == zip.uncompressedSize ) { ok = true; break; }
				if ( used == input )
				{
					input = MIN( zip.compressedSize - fileRead, uint32( sizeof( packed ) ) );
					if ( input && !file.Read( packed, input ) ) break;
					used = 0;
					fileRead += input;
				}
			}
		}
	}
	if ( ok && ShadowMap_CRC32( data.Base(), zip.uncompressedSize ) == zip.crc32 ) return true;
	data.RemoveAll();
	return false;
}

namespace cubemap_sidecar
{
struct NewEntry
{
	char name[MAX_PATH];
	const CubemapMember *member;
	ZIP_FileHeader zip;
};

uint64 Align( uint64 offset, uint32 alignment )
{
	return alignment ? ( offset + alignment - 1 ) & ~uint64( alignment - 1 ) : offset;
}

bool Copy( DiskFile &source, DiskFile &dest, uint32 start, uint32 bytes )
{
	if ( !bytes ) return true;
	if ( !Range( start, bytes, source.Size() ) || !source.Seek( start ) ) return false;
	uint8 scratch[kIoBytes];
	while ( bytes )
	{
		const uint32 n = MIN( bytes, uint32( sizeof( scratch ) ) );
		if ( !source.Read( scratch, n ) || !dest.Write( scratch, n ) ) return false;
		bytes -= n;
	}
	return true;
}

bool Zero( DiskFile &file, uint32 bytes )
{
	static const uint8 padding[kIoBytes] = {};
	while ( bytes )
	{
		const uint32 n = MIN( bytes, uint32( sizeof( padding ) ) );
		if ( !file.Write( padding, n ) ) return false;
		bytes -= n;
	}
	return true;
}

bool WriteLocal( DiskFile &out, const NewEntry &entry )
{
	const ZIP_FileHeader &zip = entry.zip;
	ZIP_LocalFileHeader header = {};
	header.signature = PKID( 3,4 );
	header.versionNeededToExtract = zip.versionNeededToExtract;
	header.crc32 = zip.crc32;
	header.compressedSize = zip.compressedSize;
	header.uncompressedSize = zip.uncompressedSize;
	header.fileNameLength = zip.fileNameLength;
	header.extraFieldLength = zip.extraFieldLength;
	return out.Write( &header, sizeof( header ) ) && out.Write( entry.name, zip.fileNameLength ) &&
		Zero( out, zip.extraFieldLength ) && out.Write( entry.member->data.Base(), zip.compressedSize );
}

class TemporarySidecar
{
public:
	~TemporarySidecar()
	{
		if ( m_Owned ) { file.CloseHandleIfOpen(); DeleteFileA( path ); }
	}
	bool Create( const char *target )
	{
		static volatile LONG serial;
		for ( int attempt = 0; attempt < 16; ++attempt )
		{
			const int length = V_snprintf( path, sizeof( path ), "%s.cubemap-%08lx-%08lx.tmp", target,
				GetCurrentProcessId(), static_cast< unsigned long >( InterlockedIncrement( &serial ) ) );
			if ( length < 0 || length >= int( sizeof( path ) ) ) return false;
			if ( file.Create( path ) ) { m_Owned = true; return true; }
			const DWORD error = GetLastError();
			if ( error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS ) return false;
		}
		return false;
	}
	bool Replace( const char *target, const BY_HANDLE_FILE_INFORMATION &expected )
	{
		if ( !file.Finish() ) return false;
		DiskFile current;
		if ( !current.Open( target ) || !SameFile( current.info, expected ) || !current.CloseHandleIfOpen() ||
			!MoveFileExA( path, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) ) return false;
		m_Owned = false;
		return true;
	}
	DiskFile file;
private:
	char path[MAX_PATH + 40];
	bool m_Owned = false;
};
} // namespace cubemap_sidecar

static bool WriteCubemapSidecar( const CubemapSidecar &state, const CUtlVector< CubemapMember > &members )
{
	using namespace cubemap_sidecar;
	if ( !state.valid || members.Count() <= 0 || members.Count() >= 0xffff ) return false;
	DiskFile source;
	if ( !source.Open( state.path ) || !SameFile( source.info, state.file ) ) return false;
	CUtlVector< NewEntry > added;
	added.SetCount( members.Count() );
	CUtlVector< uint8 > replaced;
	replaced.SetCount( state.entries.Count() );
	memset( replaced.Base(), 0, replaced.Count() );
	uint32 count = state.entries.Count();
	for ( int i = 0; i < members.Count(); ++i )
	{
		const CubemapMember &member = members[i];
		NewEntry &entry = added[i];
		const uint32 size = member.data.Count();
		if ( !size || size > kMaxCubemapBytes || !Normalize( member.name.Get(), member.name.Length(), entry.name ) ||
			!TextureName( state.bsp, entry.name ) ) return false;
		const int old = state.Find( entry.name );
		if ( old >= 0 ) { if ( replaced[old] ) return false; replaced[old] = 1; --count; }
		entry.member = &member;
		entry.zip = {};
		entry.zip.signature = PKID( 1,2 );
		entry.zip.versionMadeBy = 20;
		entry.zip.versionNeededToExtract = 10;
		entry.zip.compressedSize = entry.zip.uncompressedSize = size;
		entry.zip.fileNameLength = static_cast< uint16 >( strlen( entry.name ) );
	}
	std::sort( added.Base(), added.Base() + added.Count(), []( const NewEntry &a, const NewEntry &b ) { return strcmp( a.name, b.name ) < 0; } );
	for ( int i = 1; i < added.Count(); ++i )
		if ( !strcmp( added[i-1].name, added[i].name ) ) return false;
	count += added.Count();
	if ( count >= 0xffff ) return false;

	// Keep local offsets unchanged, so even the relative offsets in untouched
	// central records can be copied byte for byte. Reclaim any replaced tail
	// (the usual build replaces all the cubemaps, after the lighting assets).
	uint32 retainedEnd = 0;
	uint64 dirBytes = 0;
	for ( int i = 0; i < state.entries.Count(); ++i )
	{
		if ( replaced[i] ) continue;
		retainedEnd = MAX( retainedEnd, state.entries[i].localEnd );
		dirBytes += state.entries[i].centralBytes;
	}
	uint64 pos = retainedEnd;
	for ( int i = 0; i < added.Count(); ++i )
	{
		NewEntry &entry = added[i];
		ZIP_FileHeader &zip = entry.zip;
		zip.relativeOffsetOfLocalHeader = static_cast< uint32 >( pos );
		const uint64 headerEnd = pos + sizeof( ZIP_LocalFileHeader ) + zip.fileNameLength;
		const uint64 data = Align( headerEnd, state.alignment );
		zip.extraFieldLength = static_cast< uint16 >( data - headerEnd );
		pos = data + zip.compressedSize;
		if ( pos > INT_MAX ) return false;
		dirBytes += sizeof( zip ) + zip.fileNameLength + ( state.compatible ? zip.extraFieldLength : 0 );
	}
	const uint64 dirStart = Align( pos, state.alignment );
	const uint64 dirEnd = Align( dirStart + dirBytes, state.alignment );
	const uint64 pakBytes = dirEnd + sizeof( state.end ) + state.end.commentLength;
	// If the pak isn't terminal, append the replacement rather than moving
	// anything that follows it. BSP game-lump offsets are absolute.
	const bool terminal = state.pakOffset + state.pakBytes == source.Size();
	const uint64 pakOffset = terminal ? state.pakOffset : Align( source.Size(), 4 );
	if ( dirEnd - dirStart > kMaxDirectoryBytes || pakOffset + pakBytes > INT_MAX ) return false;
	ZIP_EndOfCentralDirRecord end = state.end;
	end.nCentralDirectoryEntries_ThisDisk = end.nCentralDirectoryEntries_Total = static_cast< uint16 >( count );
	end.startOfCentralDirOffset = static_cast< uint32 >( dirStart );
	end.centralDirectorySize = static_cast< uint32 >( dirEnd - dirStart );

	TemporarySidecar temp;
	if ( !temp.Create( state.path ) ) return false;
	DiskFile &out = temp.file;
	const uint32 prefix = terminal ? state.pakOffset : source.Size();
	if ( !Copy( source, out, 0, prefix ) || !Zero( out, uint32( pakOffset - prefix ) ) ) return false;
	// Retain padding and any opaque local-area bytes, but zero intervening
	// replaced records so a superseded cubemap is no longer present in the pak.
	uint32 copied = 0;
	for ( int i = 0; i < state.byLocal.Count(); ++i )
	{
		const int index = state.byLocal[i];
		if ( !replaced[index] ) continue;
		const Entry &entry = state.entries[index];
		const uint32 start = entry.zip.relativeOffsetOfLocalHeader;
		if ( start >= retainedEnd ) break;
		if ( !Copy( source, out, state.pakOffset + copied, start - copied ) || !Zero( out, entry.localEnd - start ) ) return false;
		copied = entry.localEnd;
	}
	if ( !Copy( source, out, state.pakOffset + copied, retainedEnd - copied ) ) return false;
	for ( int i = 0; i < added.Count(); ++i )
	{
		NewEntry &entry = added[i];
		entry.zip.crc32 = ShadowMap_CRC32( entry.member->data.Base(), entry.zip.compressedSize );
		if ( !WriteLocal( out, entry ) ) return false;
	}
	if ( !Zero( out, uint32( dirStart - pos ) ) ) return false;
	for ( int i = 0; i < state.entries.Count(); ++i )
	{
		const Entry &entry = state.entries[i];
		if ( !replaced[i] && !out.Write( state.directory.Base() + entry.centralOffset, entry.centralBytes ) ) return false;
	}
	for ( int i = 0; i < added.Count(); ++i )
	{
		const NewEntry &entry = added[i];
		if ( !out.Write( &entry.zip, sizeof( entry.zip ) ) || !out.Write( entry.name, entry.zip.fileNameLength ) ||
			( state.compatible && !Zero( out, entry.zip.extraFieldLength ) ) ) return false;
	}
	if ( !Zero( out, uint32( dirEnd - dirStart - dirBytes ) ) || !out.Write( &end, sizeof( end ) ) ||
		!out.Write( state.comment.Base(), end.commentLength ) ) return false;

	// Patch only the two pak table words. Never rewrite or normalize the BSP
	// header (v21 stores version, offset, length instead of offset, length, version).
	const uint32 lump = offsetof( dheader_t, lumps ) + LUMP_PAKFILE * sizeof( lump_t );
	const uint32 words[] = { static_cast< uint32 >( pakOffset ), static_cast< uint32 >( pakBytes ) };
	if ( !out.Seek( lump + ( state.bspVersion == 21 ? sizeof( int ) : 0 ) ) || !out.Write( words, sizeof( words ) ) ) return false;
	// Windows cannot reliably replace an open destination using legacy rename
	// semantics, even when our read handles allow FILE_SHARE_DELETE.
	return source.CloseHandleIfOpen() && temp.Replace( state.path, state.file );
}
