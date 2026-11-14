//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shared disk contract of the vrad_restir runtime-shadowmap sidecar
//          (game lump 'rshd'). Used by the baker (bsp_output.cpp), the client
//          (shadowmaps_dx12.cpp, through engine->LoadGameLump), the server
//          (shadowmap_transmit.cpp, through the BSP file) and the fixture
//          runner. Header-only: declarations, literals and the one validator
//          all consumers share, so there is exactly one parser.
//
//          Design (hook-free): the selected lights are REMOVED from the mode's
//          LUMP_WORLDLIGHTS(_HDR) by the baker and carried here in full, so the
//          engine never applies them to models/ambient; the runtime supplies
//          their direct term. Lightmaps/VHV/PPL/detail lighting are baked
//          without the selected direct term but with every bounce. There is no
//          baked visibility alpha: an independent traced R8 sun plane and exact
//          receiver geometry travel in this sidecar. Conversion bakes both modes,
//          so detail/PPL data stay shared and ordinary.
//
//          Scalars are little-endian 32-bit, four-byte aligned. Offsets are byte
//          offsets from the start of the 'rshd' payload.
//
//=============================================================================//
#ifndef SHADOWMAP_BSP_H
#define SHADOWMAP_BSP_H
#ifdef _WIN32
#pragma once
#endif

#include "tier0/platform.h"
#include "tier0/dbg.h"
#include "tier1/checksum_crc.h"
#include "tier1/strtools.h"
#include "bspfile.h"
#include "bspflags.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>

//-----------------------------------------------------------------------------
// Public literals (exact values are part of the on-disk contract)
//-----------------------------------------------------------------------------
#define LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR	0x00040000	// LDR mode: selected lights removed from worldlights, carried in rshd
#define LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR		0x00080000

#define GAMELUMP_RESTIR_SHADOWMAPS					'rshd'

#define SHADOWMAP_MODE_LDR		0
#define SHADOWMAP_MODE_HDR		1
#define SHADOWMAP_MODE_COUNT	2
#define SHADOWMAP_MODE_BIT( mode )	( 1u << (mode) )

// Shadow-only emitter size defaults / ranges (plan step 3). Runtime never reapplies defaults; they are resolved at bake.
#define SHADOWMAP_DEFAULT_SUN_ANGULAR_RADIUS	0.27f	// degrees, cone half-angle (SunSpreadAngle absent)
#define SHADOWMAP_DEFAULT_LOCAL_SOURCE_RADIUS	4.0f	// world units (_shadow_radius absent)
#define SHADOWMAP_MAX_SUN_ANGULAR_RADIUS		90.0f	// exclusive upper bound, degrees

// Error strings (exact text is asserted by tests and shown in the console)
#define SHADOWMAP_ERR_INVALID_METADATA		"Shadowmaps: invalid BSP lighting metadata"
#define SHADOWMAP_ERR_INVALID_EMITTER_SIZE	"Shadowmaps: invalid shadow emitter size"
#define SHADOWMAP_ERR_REQUIRES_DX12			"Shadowmaps: this BSP requires DX12 shadow lighting"
#define SHADOWMAP_ERR_SHADER_UNAVAILABLE	"Shadowmaps: required native shader unavailable"
#define SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE	"Shadowmaps: server caster transmission unavailable"
#define SHADOWMAP_ERR_CASTER_TRAVERSAL		"Shadowmaps: world caster traversal unavailable"
#define SHADOWMAP_ERR_RESIDENCY				"Shadowmaps: insufficient shadow residency"
#define SHADOWMAP_ERR_RECEIVER_MAPPING		"Shadowmaps: unresolved baked sun receiver"
#define SHADOWMAP_RECEIVER_BUMPED			0x1u
#define SHADOWMAP_RECEIVER_DISPLACEMENT	0x2u

//-----------------------------------------------------------------------------
// Disk records
//-----------------------------------------------------------------------------
#pragma pack( push, 4 )

struct ShadowMapLightDisk					// 116 bytes
{
	dworldlight_t	light;					// the complete original worldlight record (origin, intensity (/255 linear), normal,
											// cluster, type, style, stopdot/stopdot2, exponent, radius, attenuation, flags, texinfo, owner)
	int32			sourceEntity;			// BSP entity index provenance, NOT a network entindex; -1 unknown
	float			shadowSunAngularRadius;	// degrees, sun only (0 for locals)
	float			shadowSourceRadius;		// world units, local lights only (0 for sun)
	float			startFade, endFade, capDist;	// the baker's VRAD falloff controls (ReSTIRGpuLight::fade xyz: _fifty/_zero percent
											// distance quintic fade and distance cap); locals only, so the runtime direct term uses the
											// same falloff the baked bounce used. capDist > 0 always (baker default 1e22); 0 for the sun.
	uint32			reserved;				// zero
};


struct ShadowMapReceiverFaceDisk			// 96 bytes
{
	uint32 dfaceIndex, modelIndex, lightingOffset, firstSunVisibility;
	uint32 luxelW, luxelH, numChannels, numStyles;	// final encoded RGB metadata; mask has neither channels nor styles
	uint32 flags, firstTriangle, triangleCount, reserved;	// exact contiguous scalar/triangle prefixes; reserved zero
	float worldToLuxel[2][4];						// affine position -> local luxel, already subtracts lightmap mins
	float plane[4];								// oriented normal xyz, distance w (dot(normal, position) == w)
};

struct ShadowMapReceiverTriangleDisk		// 60 bytes
{
	float position[3][3];						// actual unpushed baked geometry
	float luxel[3][2];							// matching order, preserved displacement UV endpoints * (W-1,H-1)
};

#pragma pack( pop )

COMPILE_TIME_ASSERT( sizeof( dworldlight_t ) == 88 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapLightDisk ) == 116 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapReceiverFaceDisk ) == 96 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapReceiverTriangleDisk ) == 60 );


typedef bool ( *ShadowMapBspReadFn )( void *context, uint32 offset, uint32 bytes, void *out );

struct ShadowMapBspLumpInfo
{
	uint32 fileofs, filelen, version, uncompressedSize;
};

inline bool ShadowMap_IsFiniteFloat( float f )				{ return f == f && f <= FLT_MAX && f >= -FLT_MAX; }
inline bool ShadowMap_ValidSunAngularRadius( float degrees )	{ return ShadowMap_IsFiniteFloat( degrees ) && degrees >= 0.0f && degrees < SHADOWMAP_MAX_SUN_ANGULAR_RADIUS; }
inline bool ShadowMap_ValidLocalSourceRadius( float units )	{ return ShadowMap_IsFiniteFloat( units ) && units >= 0.0f; }
inline uint32 ShadowMap_LevelFlagDirect( int mode )				{ return mode == SHADOWMAP_MODE_HDR ? LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR : LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR; }


inline bool ShadowMap_FiniteVector( const Vector &v )	{ return ShadowMap_IsFiniteFloat( v.x ) && ShadowMap_IsFiniteFloat( v.y ) && ShadowMap_IsFiniteFloat( v.z ); }

// CRC helper shared by writer and readers (one definition of what is hashed).
inline uint32 ShadowMap_CRC32( const void *bytes, uint32 count )
{
	CRC32_t crc;
	CRC32_Init( &crc );
	if ( count )
		CRC32_ProcessBuffer( &crc, bytes, (int)count );
	CRC32_Final( &crc );
	return (uint32)crc;
}

//-----------------------------------------------------------------------------
// Reading the 'rshd' manifest and lumps straight from a BSP-format file: the map's own BSP for the identity lumps,
// maps/<name>.light for the manifest (the server has no game-lump API, and the manifest is not in the map). External
// .lmp overrides are not supported (the CRCs would mismatch -> rejection).
// `ReadFile` is a caller-supplied callback so this header stays
// filesystem-agnostic: it must read exactly `bytes` at `offset` into `out`.
//-----------------------------------------------------------------------------

// Reads the BSP header (and lump table) and the game-lump dictionary entry for 'rshd'. Returns false when the file
// is not a readable version-19..21 BSP. `rshdOffset/rshdBytes/rshdVersion` are zero when absent; compressed game
// lumps (uncompressedSize != 0 / LZMA) are reported as absent with `compressed` = true (converted maps are never
// written compressed by the baker).
inline bool ShadowMap_ReadBspDirectory( ShadowMapBspReadFn read, void *context, ShadowMapBspLumpInfo lumps[HEADER_LUMPS],
	uint32 &rshdOffset, uint32 &rshdBytes, uint32 &rshdVersion, bool &compressed, uint32 &levelFlags )
{
	rshdOffset = rshdBytes = rshdVersion = levelFlags = 0;
	compressed = false;
	dheader_t header;
	if ( !read || !read( context, 0, sizeof( header ), &header ) )
		return false;
	const dheader_t *h = &header;
	if ( h->ident != IDBSPHEADER || h->version < 19 || h->version > 21 )
		return false;
	for ( int i = 0; i < HEADER_LUMPS; ++i )
	{
		// Version 21 (Left 4 Dead 2) stores each lump header as version, fileofs, filelen.
		const lump_t &lump = h->lumps[i];
		const int fileofs = h->version == 21 ? lump.filelen : lump.fileofs;
		const int filelen = h->version == 21 ? lump.version : lump.filelen;
		const int version = h->version == 21 ? lump.fileofs : lump.version;
		if ( fileofs < 0 || filelen < 0 || (uint64)fileofs + filelen > 0xFFFFFFFFu ) return false;
		lumps[i].fileofs = fileofs;
		lumps[i].filelen = filelen;
		lumps[i].version = version;
		lumps[i].uncompressedSize = (uint32)lump.uncompressedSize;
	}
	if ( lumps[LUMP_MAP_FLAGS].filelen >= sizeof( uint32 ) )
	{
		if ( !read( context, lumps[LUMP_MAP_FLAGS].fileofs, sizeof( uint32 ), &levelFlags ) )
			return false;
	}
	const ShadowMapBspLumpInfo &gl = lumps[LUMP_GAME_LUMP];
	if ( gl.filelen < sizeof( int ) )
		return true;
	int count = 0;
	if ( !read( context, gl.fileofs, sizeof( int ), &count ) || count < 0 || count > 256 )
		return false;
	if ( (uint64)sizeof( int ) + (uint64)count * sizeof( dgamelump_t ) > gl.filelen ) return false;
	for ( int i = 0; i < count; ++i )
	{
		dgamelump_t entry;
		if ( !read( context, gl.fileofs + sizeof( int ) + i * sizeof( dgamelump_t ), sizeof( dgamelump_t ), &entry ) )
			return false;
		if ( entry.id != GAMELUMP_RESTIR_SHADOWMAPS )
			continue;
		if ( entry.fileofs < 0 || entry.filelen < 0 ||
			(uint64)entry.fileofs + entry.filelen > 0xFFFFFFFFu ) return false;
		if ( entry.flags & 0x1 )	// GAMELUMPFLAG_COMPRESSED
		{
			compressed = true;
			return true;
		}
		rshdOffset = entry.fileofs;
		rshdBytes = entry.filelen;
		rshdVersion = entry.version;
		return true;
	}
	return true;
}

// CRC of a lump read in chunks (no whole-lump allocation). Returns false on read failure.
inline bool ShadowMap_CRC32Lump( ShadowMapBspReadFn read, void *context, const ShadowMapBspLumpInfo &lump, uint32 &crc, uint8 *scratch, uint32 scratchBytes )
{
	if ( !read || !scratch || !scratchBytes || scratchBytes > 0x7FFFFFFFu ||
		(uint64)lump.fileofs + lump.filelen > 0xFFFFFFFFu ) return false;
	CRC32_t c;
	CRC32_Init( &c );
	uint32 done = 0;
	while ( done < lump.filelen )
	{
		uint32 n = MIN( scratchBytes, lump.filelen - done );
		if ( !read( context, lump.fileofs + done, n, scratch ) )
			return false;
		CRC32_ProcessBuffer( &c, scratch, (int)n );
		done += n;
	}
	CRC32_Final( &c );
	crc = (uint32)c;
	return true;
}

#endif // SHADOWMAP_BSP_H
