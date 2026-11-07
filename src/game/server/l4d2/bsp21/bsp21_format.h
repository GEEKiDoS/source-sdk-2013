//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef L4D2_BSP21_FORMAT_H
#define L4D2_BSP21_FORMAT_H

#include <stdint.h>
#include "tier1/utlvector.h"

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
static_assert( sizeof( Bsp21Header ) == 1036, "L4D2 BSP header layout" );

struct Bsp21GameLump
{
	uint32_t id;
	uint16_t flags, version;
	uint32_t offset, length;
};
static_assert( sizeof( Bsp21GameLump ) == 16, "L4D2 game lump layout" );

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

// Reads refer to the mounted member, not the enclosing archive. The filesystem
// owns VPK preload bytes, archive selection and search-path precedence.
struct Bsp21Reader
{
	void *context;
	uint32_t size;
	bool ( *read )( void *context, uint32_t offset, void *out, uint32_t bytes );
};

#endif
