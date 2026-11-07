//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef BSP21_FORMAT_H
#define BSP21_FORMAT_H
#pragma once

#include <stdint.h>
#include "bspfile.h"
#include "tier1/utlvector.h"

// Left 4 Dead 2's BSP21 header keeps the same byte size as dheader_t, but
// stores each lump as { version, file offset, stored length, uncompressed length }.
static const uint32_t BSP21_VERSION = 21;
static const uint32_t BSP21_IDENT = 0x50534256;
static const uint32_t BSP21_WORLDLIGHT_BYTES = 100;
static const uint32_t BSP21_WORLDLIGHT_COMMON_PREFIX = 36;
static const uint32_t BSP21_WORLDLIGHT_COMMON_SUFFIX = 52;
static const uint32_t BSP21_STATIC_PROP_DIFFUSE_MODULATION_OFFSET = 64;
static const uint32_t BSP21_STATIC_PROP_V8_BYTES = 68;
static const uint32_t BSP21_STATIC_PROP_V9_BYTES = 72;
static const GameLumpId_t BSP21_STATIC_PROP_TINT_LUMP = 'ptnt';
static const uint16_t BSP21_STATIC_PROP_TINT_LUMP_VERSION = 1;

struct Bsp21StaticPropTint
{
	uint32_t rgba;
};
static_assert( sizeof( Bsp21StaticPropTint ) == sizeof( uint32_t ), "BSP21 static-prop tint payload" );
inline bool Bsp21StaticPropTintIsUntinted( uint32_t rgba )
{
	return rgba == 0xffffffffu;
}

struct Bsp21Lump
{
	uint32_t version, offset, length, uncompressedLength;
};

struct Bsp21Header
{
	uint32_t ident, version;
	Bsp21Lump lumps[64];
	uint32_t revision;
};
static_assert( sizeof( Bsp21Header ) == 1036, "BSP21 header layout" );

struct Bsp21GameLump
{
	uint32_t id;
	uint16_t flags, version;
	uint32_t offset, length;
};
static_assert( sizeof( Bsp21GameLump ) == 16, "BSP21 game lump layout" );

struct Bsp21Layout
{
	Bsp21Layout() {}
	Bsp21Layout( const Bsp21Layout &source ) { *this = source; }
	Bsp21Layout &operator=( const Bsp21Layout &source )
	{
		if ( this == &source ) return *this;
		header = source.header;
		size = source.size;
		prefix = source.prefix;
		propVersion = source.propVersion;
		propCount = source.propCount;
		modelCount = source.modelCount;
		leafCount = source.leafCount;
		recordsOffset = source.recordsOffset;
		gameLumps.EnsureCapacity( source.gameLumps.Count() );
		gameLumps = source.gameLumps;
		return *this;
	}

	Bsp21Header header;
	uint32_t size, prefix;
	uint32_t propVersion, propCount, modelCount, leafCount, recordsOffset;
	CUtlVector< Bsp21GameLump > gameLumps;
};

// Readers refer to the mounted member, not the enclosing archive.
struct Bsp21Reader
{
	void *context;
	uint32_t size;
	bool ( *read )( void *context, uint32_t offset, void *out, uint32_t bytes );
};

inline bool Bsp21IsHeader( const void *data )
{
	uint32_t ident, version;
	memcpy( &ident, data, sizeof( ident ) );
	memcpy( &version, static_cast< const unsigned char * >( data ) + 4, sizeof( version ) );
	return ident == BSP21_IDENT && version == BSP21_VERSION;
}

inline void Bsp21NormalizeHeader( void *data )
{
	unsigned char *header = static_cast< unsigned char * >( data );
	for ( int i = 0; i < 64; ++i )
	{
		unsigned char *lump = header + 8 + i * 16;
		uint32_t version, offset, length;
		memcpy( &version, lump, 4 );
		memcpy( &offset, lump + 4, 4 );
		memcpy( &length, lump + 8, 4 );
		memcpy( lump, &offset, 4 );
		memcpy( lump + 4, &length, 4 );
		memcpy( lump + 8, &version, 4 );
	}
	uint32_t version = BSPVERSION;
	memcpy( header + 4, &version, 4 );
}

// L4D2 v21 worldlight v1 records insert shadow-cast offset (three floats)
// between the common prefix and suffix. The SDK representation omits it.
inline void Bsp21WorldLightToSdk( void *destination, const void *source )
{
	const unsigned char *src = static_cast< const unsigned char * >( source );
	unsigned char *dst = static_cast< unsigned char * >( destination );
	memcpy( dst, src, BSP21_WORLDLIGHT_COMMON_PREFIX );
	memcpy( dst + BSP21_WORLDLIGHT_COMMON_PREFIX,
		src + BSP21_WORLDLIGHT_COMMON_PREFIX + 12, BSP21_WORLDLIGHT_COMMON_SUFFIX );
}

// L4D2 v8/v9 records use byte 31 for source flags and bytes 64..67 for the
// authored RGBA m_DiffuseModulation tint. SDK v6 has no diffuse-modulation
// field, so conversion drops it from the ordinary record; v9 conversion also
// uses bytes 64..67 for its widened m_Flags field. The tint is carried by the
// BSP21_STATIC_PROP_TINT_LUMP side game lump in v20 output.
inline void Bsp21StaticPropV8ToSdkV6( void *destination, const void *source )
{
	memcpy( destination, source, 60 );
	memset( static_cast< unsigned char * >( destination ) + 60, 0, 4 );
}

inline void Bsp21StaticPropV9ToSdkV10( void *destination, const void *source )
{
	unsigned char *dst = static_cast< unsigned char * >( destination );
	const unsigned char *src = static_cast< const unsigned char * >( source );
	memcpy( dst, src, 60 );
	dst[31] = 0;
	memset( dst + 60, 0, 4 );
	uint32_t flags = static_cast< uint32_t >( src[31] ) | 0x100u;
	memcpy( dst + 64, &flags, sizeof( flags ) );
	memset( dst + 68, 0, 4 );
}

#endif
