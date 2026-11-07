//========= Copyright Valve Corporation, All rights reserved. ============//
#include <bcrypt.h>
#include <limits.h>
#include "bsp21_format.h"
#include "tier1/lzmaDecoder.h"
#include "bspfile.h"
#include "gamebspfile.h"
#pragma comment(lib, "bcrypt.lib")

// Module identities protect the binary ABI seams. Content is admitted by its
// structure, never by a map name or a digest of somebody's installed assets.
static bool CheckFile( const wchar_t *path, const char *expected )
{
	HANDLE file = CreateFileW( path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );
	if ( file == INVALID_HANDLE_VALUE )
	{
		BspLog( "REJECT module open path=%ls win32=%lu", path, GetLastError() );
		return false;
	}
	BCRYPT_ALG_HANDLE algorithm = NULL;
	BCRYPT_HASH_HANDLE hash = NULL;
	NTSTATUS status = BCryptOpenAlgorithmProvider( &algorithm, BCRYPT_SHA256_ALGORITHM, NULL, 0 );
	if ( status >= 0 ) status = BCryptCreateHash( algorithm, &hash, NULL, 0, NULL, 0, 0 );
	unsigned char buffer[65536], digest[32];
	DWORD bytes = 0;
	bool readOK = true;
	while ( status >= 0 )
	{
		if ( !ReadFile( file, buffer, sizeof( buffer ), &bytes, NULL ) ) { readOK = false; break; }
		if ( !bytes ) break;
		status = BCryptHashData( hash, buffer, bytes, 0 );
	}
	CloseHandle( file );
	if ( status >= 0 && readOK ) status = BCryptFinishHash( hash, digest, sizeof( digest ), 0 );
	if ( hash ) BCryptDestroyHash( hash );
	if ( algorithm ) BCryptCloseAlgorithmProvider( algorithm, 0 );
	if ( status < 0 || !readOK )
	{
		BspLog( "REJECT module hash-I/O path=%ls status=%08lx", path, (unsigned long)status );
		return false;
	}
	char actual[65];
	for ( int i = 0; i < 32; ++i ) sprintf_s( actual + i * 2, 3, "%02x", digest[i] );
	bool matches = !strcmp( actual, expected );
	BspLog( "%s module SHA256 path=%ls expected=%s actual=%s", matches ? "PASS" : "REJECT", path, expected, actual );
	return matches;
}

static bool Bsp21Reject( const char *name, const char *reason, int lump = -1 )
{
	BspLog( "REJECT map=%s lump=%d: %s", name, lump, reason );
	return false;
}

static bool Bsp21Range( uint32_t offset, uint32_t length, uint32_t size )
{
	return offset <= size && length <= size - offset;
}

static bool Bsp21ReadAt( const Bsp21Reader &reader, uint32_t offset, void *out, uint32_t bytes )
{
	return Bsp21Range( offset, bytes, reader.size ) && reader.read( reader.context, offset, out, bytes );
}

static bool Bsp21CompressedHeader( const Bsp21Reader &reader, uint32_t offset, uint32_t stored, uint32_t length )
{
	lzma_header_t header;
	return stored >= sizeof( header ) && length <= INT_MAX &&
		Bsp21ReadAt( reader, offset, &header, sizeof( header ) ) &&
		header.id == LZMA_ID && header.actualSize == length && header.lzmaSize <= stored - sizeof( header );
}

// Match vector resize for these POD records/bytes: keep existing elements and
// capacity, and value-initialize only the newly appended tail.
template <class T> static void Bsp21Resize( CUtlVector< T > &data, int count )
{
	int previous = data.Count();
	// Known file extents can exceed 1 GiB; reserve exactly rather than letting
	// CUtlMemory's signed geometric growth overflow before reaching count.
	data.EnsureCapacity( count );
	data.SetCountNonDestructively( count );
	if ( count > previous )
		memset( data.Base() + previous, 0, ( count - previous ) * sizeof( T ) );
}

static bool Bsp21ReadPayload( const Bsp21Reader &reader, uint32_t offset, uint32_t stored,
	uint32_t length, bool compressed, void *out )
{
	if ( !compressed ) return Bsp21ReadAt( reader, offset, out, length );
	if ( !Bsp21CompressedHeader( reader, offset, stored, length ) ) return false;
	CUtlVector< unsigned char > packed;
	Bsp21Resize( packed, static_cast< int >( stored ) );
	return Bsp21ReadAt( reader, offset, packed.Base(), stored ) &&
		CLZMA::Uncompress( packed.Base(), static_cast< unsigned char * >( out ) ) == length;
}

static uint32_t Bsp21GameStoredSize( const Bsp21Layout &layout, int index )
{
	const Bsp21GameLump &lump = layout.gameLumps[index];
	if ( !( lump.flags & 1 ) ) return lump.length;
	uint32_t end = layout.header.lumps[35].offset + layout.header.lumps[35].length;
	for ( int i = index + 1; i < layout.gameLumps.Count(); ++i )
	{
		if ( layout.gameLumps[i].offset )
		{
			end = layout.gameLumps[i].offset;
			break;
		}
	}
	return end - lump.offset;
}
static uint32_t Bsp21LumpLength( const Bsp21Lump &lump )
{
	return lump.uncompressedLength ? lump.uncompressedLength : lump.length;
}

static uint32_t Bsp21LeafCount( const Bsp21Layout &layout )
{
	const Bsp21Lump &leaves = layout.header.lumps[LUMP_LEAFS];
	return Bsp21LumpLength( leaves ) / ( leaves.version ? sizeof( dleaf_t ) : sizeof( dleaf_version_0_t ) );
}

static bool Bsp21ReadLump( const Bsp21Reader &reader, const Bsp21Lump &lump, CUtlVector< unsigned char > &data )
{
	Bsp21Resize( data, static_cast< int >( Bsp21LumpLength( lump ) ) );
	return Bsp21ReadPayload( reader, lump.offset, lump.length, static_cast< uint32_t >( data.Count() ),
		lump.uncompressedLength != 0, data.Base() );
}

static bool Bsp21ValidateWorldlights( const Bsp21Reader &reader, const Bsp21Layout &layout, const char *name )
{
	if ( !layout.header.lumps[LUMP_WORLDLIGHTS].length && !layout.header.lumps[LUMP_WORLDLIGHTS_HDR].length ) return true;
	const Bsp21Lump &vis = layout.header.lumps[LUMP_VISIBILITY];
	uint32_t clusters = 0;
	if ( vis.length )
	{
		CUtlVector< unsigned char > data;
		if ( Bsp21LumpLength( vis ) < 4 || !Bsp21ReadLump( reader, vis, data ) )
			return Bsp21Reject( name, "truncated worldlight visibility directory", LUMP_VISIBILITY );
		memcpy( &clusters, data.Base(), 4 );
		if ( clusters > static_cast< uint32_t >( data.Count() - 4 ) / 8 )
			return Bsp21Reject( name, "worldlight visibility directory extent", LUMP_VISIBILITY );
	}
	const int worldlightLumps[] = { LUMP_WORLDLIGHTS, LUMP_WORLDLIGHTS_HDR };
	for ( int id : worldlightLumps )
	{
		const Bsp21Lump &lump = layout.header.lumps[id];
		if ( !lump.length ) continue;
		CUtlVector< unsigned char > data;
		if ( !Bsp21ReadLump( reader, lump, data ) ) return Bsp21Reject( name, "unreadable worldlights", id );
		uint32_t stride = lump.version ? 100 : sizeof( dworldlight_t );
		for ( uint32_t offset = 0; offset < static_cast< uint32_t >( data.Count() ); offset += stride )
		{
			dworldlight_t light;
			memcpy( &light, data.Base() + offset, 36 );
			memcpy( reinterpret_cast< unsigned char * >( &light ) + 36, data.Base() + offset + ( lump.version ? 48 : 36 ), 52 );
			if ( light.type < emit_surface || light.type > emit_skyambient || light.style < 0 || light.style >= MAX_LIGHTSTYLES ||
				light.cluster < -1 || ( vis.length && light.cluster >= 0 && static_cast< uint32_t >( light.cluster ) >= clusters ) ||
				light.texinfo < -1 || ( light.texinfo >= 0 && static_cast< uint32_t >( light.texinfo ) >= Bsp21LumpLength( layout.header.lumps[LUMP_TEXINFO] ) / sizeof( texinfo_t ) ) )
				return Bsp21Reject( name, "worldlight type/style/cluster/texinfo reference", id );
		}
	}
	return true;
}

static bool Bsp21Array( const unsigned char *data, uint32_t length, uint32_t &cursor, uint32_t stride, uint32_t &count )
{
	if ( !Bsp21Range( cursor, 4, length ) ) return false;
	memcpy( &count, data + cursor, 4 );
	cursor += 4;
	return count <= ( length - cursor ) / stride;
}

static bool Bsp21ValidateDetails( const unsigned char *data, uint32_t length, const Bsp21Layout &layout,
	const uint32_t (&lighting)[2], const char *name )
{
	uint32_t cursor = 0, models, sprites, count;
	if ( !Bsp21Array( data, length, cursor, sizeof( DetailObjectDictLump_t ), models ) )
		return Bsp21Reject( name, "detail model dictionary extent", LUMP_GAME_LUMP );
	for ( uint32_t i = 0; i < models; ++i )
		if ( !memchr( data + cursor + i * DETAIL_NAME_LENGTH, 0, DETAIL_NAME_LENGTH ) )
			return Bsp21Reject( name, "unterminated detail model name", LUMP_GAME_LUMP );
	cursor += models * sizeof( DetailObjectDictLump_t );
	if ( !Bsp21Array( data, length, cursor, sizeof( DetailSpriteDictLump_t ), sprites ) )
		return Bsp21Reject( name, "detail sprite dictionary extent", LUMP_GAME_LUMP );
	cursor += sprites * sizeof( DetailSpriteDictLump_t );
	if ( !Bsp21Array( data, length, cursor, sizeof( DetailObjectLump_t ), count ) || count * sizeof( DetailObjectLump_t ) != length - cursor )
		return Bsp21Reject( name, "detail record extent", LUMP_GAME_LUMP );
	uint16_t previousLeaf = 0;
	for ( uint32_t i = 0; i < count; ++i )
	{
		DetailObjectLump_t detail;
		memcpy( &detail, data + cursor + i * sizeof( detail ), sizeof( detail ) );
		if ( detail.m_Type > DETAIL_PROP_TYPE_SHAPE_TRI || detail.m_Orientation > DETAIL_PROP_ORIENT_SCREEN_ALIGNED_VERTICAL ||
			detail.m_DetailModel >= ( detail.m_Type == DETAIL_PROP_TYPE_MODEL ? models : sprites ) ||
			detail.m_Leaf >= Bsp21LeafCount( layout ) || detail.m_Leaf < previousLeaf )
			return Bsp21Reject( name, "detail type/orientation/model/sprite/leaf reference or leaf order", LUMP_GAME_LUMP );
		if ( detail.m_LightStyleCount )
		{
			if ( detail.m_LightStyleCount >= 100 || detail.m_LightStyles >= 0x1000000 ||
				( !lighting[0] && !lighting[1] ) )
				return Bsp21Reject( name, "detail lighting reference", LUMP_GAME_LUMP );
			for ( int mode = 0; mode < 2; ++mode )
			{
				// HDR-only maps carry an empty dplt placeholder. Validate each
				// authored lighting mode, not an unavailable LDR representation.
				if ( ( lighting[mode] || layout.header.lumps[mode ? LUMP_LIGHTING_HDR : LUMP_LIGHTING].length ) &&
					!Bsp21Range( detail.m_LightStyles, detail.m_LightStyleCount, lighting[mode] ) )
					return Bsp21Reject( name, "detail lighting reference", LUMP_GAME_LUMP );
			}
		}
		previousLeaf = detail.m_Leaf;
	}
	return true;
}

static bool Bsp21ValidateDetailLighting( const unsigned char *data, uint32_t length, uint32_t &count, const char *name )
{
	uint32_t cursor = 0;
	if ( !Bsp21Array( data, length, cursor, sizeof( DetailPropLightstylesLump_t ), count ) ||
		count * sizeof( DetailPropLightstylesLump_t ) != length - cursor )
		return Bsp21Reject( name, "detail lighting extent", LUMP_GAME_LUMP );
	for ( uint32_t i = 0; i < count; ++i )
	{
		DetailPropLightstylesLump_t light;
		memcpy( &light, data + cursor + i * sizeof( light ), sizeof( light ) );
		if ( light.m_Style >= MAX_LIGHTSTYLES ) return Bsp21Reject( name, "detail lightstyle index", LUMP_GAME_LUMP );
	}
	return true;
}


static bool Bsp21ValidateProps( const unsigned char *data, uint32_t length, Bsp21Layout &layout, const char *name )
{
	uint32_t stride;
	switch ( layout.propVersion )
	{
	case 4: stride = 56; break;
	case 5: stride = 60; break;
	case 6: stride = 64; break;
	case 8: stride = 68; break;
	case 9: stride = 72; break;
	default: return Bsp21Reject( name, "unsupported static-prop version", 35 );
	}
	uint32_t cursor = 0;
	if ( length < 4 ) return Bsp21Reject( name, "truncated static-prop dictionary", 35 );
	memcpy( &layout.modelCount, data, 4 );
	cursor = 4;
	if ( layout.modelCount > ( length - cursor ) / 128 ) return Bsp21Reject( name, "static-prop dictionary extent", 35 );
	for ( uint32_t i = 0; i < layout.modelCount; ++i )
		if ( !memchr( data + cursor + i * 128, 0, 128 ) ) return Bsp21Reject( name, "unterminated static-prop model name", 35 );
	cursor += layout.modelCount * 128;
	if ( !Bsp21Range( cursor, 4, length ) ) return Bsp21Reject( name, "missing static-prop leaf count", 35 );
	memcpy( &layout.leafCount, data + cursor, 4 );
	cursor += 4;
	if ( layout.leafCount > ( length - cursor ) / 2 ) return Bsp21Reject( name, "static-prop leaf extent", 35 );
	for ( uint32_t i = 0; i < layout.leafCount; ++i )
	{
		uint16_t leaf;
		memcpy( &leaf, data + cursor + i * 2, 2 );
		if ( leaf >= Bsp21LeafCount( layout ) ) return Bsp21Reject( name, "static-prop BSP leaf index", LUMP_GAME_LUMP );
	}
	cursor += layout.leafCount * 2;
	if ( !Bsp21Range( cursor, 4, length ) ) return Bsp21Reject( name, "missing static-prop count", 35 );
	memcpy( &layout.propCount, data + cursor, 4 );
	cursor += 4;
	if ( layout.propCount > ( length - cursor ) / stride || layout.propCount * stride != length - cursor )
		return Bsp21Reject( name, "static-prop record extent", 35 );
	layout.recordsOffset = cursor;
	for ( uint32_t i = 0; i < layout.propCount; ++i )
	{
		const unsigned char *prop = data + cursor + i * stride;
		uint16_t model, firstLeaf, leafCount;
		memcpy( &model, prop + 24, 2 );
		memcpy( &firstLeaf, prop + 26, 2 );
		memcpy( &leafCount, prop + 28, 2 );
		if ( model >= layout.modelCount || !Bsp21Range( firstLeaf, leafCount, layout.leafCount ) )
			return Bsp21Reject( name, "static-prop model/leaf index", 35 );
	}
	return true;
}

static bool ReadBsp21Layout( const Bsp21Reader &reader, Bsp21Layout &layout, const char *name )
{
	// Admission checks the layouts and references that this adapter rewrites:
	// the v21 header, worldlights, and all four consumed game lumps. Ordinary
	// geometry/topology/physics/ZIP payloads have the same formats and SDK trust
	// boundary as v20 loose maps; this is not a separate hostile-BSP sandbox.
	layout.gameLumps.RemoveAll();
	layout.size = reader.size;
	layout.prefix = sizeof( Bsp21Header );
	layout.propVersion = layout.propCount = layout.modelCount = layout.leafCount = layout.recordsOffset = 0;
	if ( reader.size > INT_MAX || !Bsp21ReadAt( reader, 0, &layout.header, sizeof( layout.header ) ) )
		return Bsp21Reject( name, "truncated or oversized BSP header" );
	if ( layout.header.ident != 0x50534256 || layout.header.version != 21 )
		return Bsp21Reject( name, "not a version-21 BSP" );
	for ( int i = 0; i < 64; ++i )
	{
		const Bsp21Lump &lump = layout.header.lumps[i];
		if ( !lump.length ) continue;
		if ( lump.offset < sizeof( Bsp21Header ) || !Bsp21Range( lump.offset, lump.length, reader.size ) )
			return Bsp21Reject( name, "lump outside mounted file", i );
		if ( lump.uncompressedLength && !Bsp21CompressedHeader( reader, lump.offset, lump.length, lump.uncompressedLength ) )
			return Bsp21Reject( name, "invalid compressed lump header/extent", i );
		if ( i != 40 && lump.offset + lump.length > layout.prefix ) layout.prefix = lump.offset + lump.length;
		if ( i == LUMP_LEAFS && ( lump.version > 1 ||
			Bsp21LumpLength( lump ) % ( lump.version ? sizeof( dleaf_t ) : sizeof( dleaf_version_0_t ) ) ) )
			return Bsp21Reject( name, "invalid BSP leaf version/record extent", i );
		if ( i == 15 || i == 54 )
		{
			uint32_t length = lump.uncompressedLength ? lump.uncompressedLength : lump.length;
			if ( lump.version > 1 || length % ( lump.version ? 100 : 88 ) )
				return Bsp21Reject( name, "invalid worldlight version/record extent", i );
		}
	}
	if ( !Bsp21ValidateWorldlights( reader, layout, name ) ) return false;
	const Bsp21Lump &game = layout.header.lumps[35];
	if ( !game.length ) return true;
	uint32_t count;
	if ( game.uncompressedLength || game.length < 4 || !Bsp21ReadAt( reader, game.offset, &count, 4 ) || count > ( game.length - 4 ) / 16 )
		return Bsp21Reject( name, "invalid game-lump directory extent", 35 );
	Bsp21Resize( layout.gameLumps, static_cast< int >( count ) );
	if ( count && !Bsp21ReadAt( reader, game.offset + 4, layout.gameLumps.Base(), count * 16 ) )
		return Bsp21Reject( name, "truncated game-lump directory", 35 );
	uint32_t directoryEnd = game.offset + 4 + count * 16;
	uint32_t gameEnd = game.offset + game.length;
	uint32_t lighting[2] = {};
	int detailIndex = layout.gameLumps.Count();
	for ( int i = 0; i < layout.gameLumps.Count(); ++i )
	{
		const Bsp21GameLump &lump = layout.gameLumps[i];
		if ( !lump.length ) continue; // Native compressed directories can end with a sentinel.
		for ( int previous = 0; previous < i; ++previous )
			if ( layout.gameLumps[previous].id == lump.id ) return Bsp21Reject( name, "duplicate game-lump ID", LUMP_GAME_LUMP );
		if ( lump.flags & ~1 || lump.length > INT_MAX || lump.offset < directoryEnd || lump.offset > gameEnd )
			return Bsp21Reject( name, "invalid game-lump flags/offset", 35 );
		uint32_t stored = Bsp21GameStoredSize( layout, i );
		if ( !Bsp21Range( lump.offset, stored, gameEnd ) ) return Bsp21Reject( name, "game-lump payload extent", 35 );
		if ( lump.flags & 1 && !Bsp21CompressedHeader( reader, lump.offset, stored, lump.length ) )
			return Bsp21Reject( name, "invalid compressed game lump", 35 );
		switch ( lump.id )
		{
		case GAMELUMP_STATIC_PROPS:
		case GAMELUMP_DETAIL_PROP_LIGHTING:
		case GAMELUMP_DETAIL_PROP_LIGHTING_HDR:
		{
			CUtlVector< unsigned char > data;
			Bsp21Resize( data, static_cast< int >( lump.length ) );
			if ( !Bsp21ReadPayload( reader, lump.offset, stored, lump.length, ( lump.flags & 1 ) != 0, data.Base() ) )
				return Bsp21Reject( name, "unreadable game-lump payload", LUMP_GAME_LUMP );
			if ( lump.id == GAMELUMP_STATIC_PROPS )
			{
				layout.propVersion = lump.version;
				if ( !Bsp21ValidateProps( data.Base(), lump.length, layout, name ) ) return false;
			}
			else if ( lump.version != GAMELUMP_DETAIL_PROP_LIGHTING_VERSION ||
				!Bsp21ValidateDetailLighting( data.Base(), lump.length, lighting[lump.id == GAMELUMP_DETAIL_PROP_LIGHTING_HDR], name ) )
				return Bsp21Reject( name, "invalid detail lighting layout", LUMP_GAME_LUMP );
			break;
		}
		case GAMELUMP_DETAIL_PROPS:
			if ( lump.version != GAMELUMP_DETAIL_PROPS_VERSION ) return Bsp21Reject( name, "unsupported detail-prop version", LUMP_GAME_LUMP );
			detailIndex = i;
			break;
		// No engine/game consumer requests other IDs; their bounded bytes remain opaque.
		}
	}
	if ( detailIndex != layout.gameLumps.Count() )
	{
		const Bsp21GameLump &lump = layout.gameLumps[detailIndex];
		CUtlVector< unsigned char > data;
		Bsp21Resize( data, static_cast< int >( lump.length ) );
		if ( !Bsp21ReadPayload( reader, lump.offset, Bsp21GameStoredSize( layout, detailIndex ), lump.length, ( lump.flags & 1 ) != 0, data.Base() ) )
			return Bsp21Reject( name, "unreadable detail-prop payload", LUMP_GAME_LUMP );
		if ( !Bsp21ValidateDetails( data.Base(), lump.length, layout, lighting, name ) ) return false;
	}
	return true;
}
