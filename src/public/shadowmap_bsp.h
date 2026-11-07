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
#define GAMELUMP_RESTIR_SHADOWMAPS_VERSION			3

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

struct ShadowMapModeDisk					// 64 bytes
{
	uint32	lightingBytes;				// byte size of this mode's LUMP_LIGHTING(_HDR) payload at bake time
	uint32	lightingCRC32;				// CRC32 of that payload
	uint32	facesCRC32;					// CRC32 of this mode's LUMP_FACES(_HDR) payload
	uint32	worldlightsCRC32;			// CRC32 of this mode's WRITTEN (selected lights removed) LUMP_WORLDLIGHTS(_HDR) payload
	uint32	lightsOffset;				// ShadowMapLightDisk[lightCount]; sun (if any) first, then locals in bake order
	uint32	lightCount;					// > 0 for every mode in runtimeModeMask
	int32	sunLightIndex;				// index into this mode's list of the selected emit_skylight, -1 none
	uint32	reserved;					// zero
	uint32	receiverFacesOffset, receiverFaceCount;		// dface-sorted ShadowMapReceiverFaceDisk records
	uint32	receiverTrianglesOffset, receiverTriangleCount;
	uint32	sunVisibilityOffset, sunVisibilityCount;		// one row-major R8_UNORM plane per face; count excludes zero padding
	uint32	receiverDataCRC32, reserved2;				// CRC(face bytes, triangle bytes, unpadded scalar bytes); reserved2 zero
};

struct ShadowMapLumpDisk					// 144 bytes
{
	uint32	byteSize;					// total payload bytes including this header
	uint32	runtimeModeMask;			// SHADOWMAP_MODE_BIT(mode) for every converted mode (both bits for a -both bake)
	uint32	reserved[2];				// zero
	ShadowMapModeDisk mode[SHADOWMAP_MODE_COUNT];	// index 0 LDR, 1 HDR; absent modes all zero except sunLightIndex = -1
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
COMPILE_TIME_ASSERT( sizeof( ShadowMapModeDisk ) == 64 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapLumpDisk ) == 144 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapReceiverFaceDisk ) == 96 );
COMPILE_TIME_ASSERT( sizeof( ShadowMapReceiverTriangleDisk ) == 60 );

//-----------------------------------------------------------------------------
// Validation. Callers state in `checkMask` which BSP cross-checks they could
// populate (the baker: all; client/server: the lumps they read from the BSP file).
//-----------------------------------------------------------------------------
#define SHADOWMAP_CHECK_LEVEL_FLAGS		0x01	// levelFlags (dflagslump_t) vs runtimeModeMask
#define SHADOWMAP_CHECK_LIGHTING		0x02	// lightingBytes / lightingCRC32
#define SHADOWMAP_CHECK_FACES			0x04	// facesCRC32
#define SHADOWMAP_CHECK_WORLDLIGHTS		0x08	// worldlightsCRC32 of the written lump
#define SHADOWMAP_CHECK_ALL				0x0F


typedef bool ( *ShadowMapBspReadFn )( void *context, uint32 offset, uint32 bytes, void *out );

struct ShadowMapBspLumpInfo
{
	uint32 fileofs, filelen, version, uncompressedSize;
};
struct ShadowMapValidateMode
{
	bool	available;			// this mode's lumps exist in the BSP
	uint32	lightingBytes;
	uint32	lightingCRC32;
	uint32	facesCRC32;
	uint32	worldlightsCRC32;	// CRC of the lump as present in the BSP (selected lights already removed)
	const dface_t *faces;
	uint32 faceCount, facesOffset;

	ShadowMapValidateMode() { memset( this, 0, sizeof( *this ) ); }
};

struct ShadowMapValidateInput
{
	const void				*lump;			// 'rshd' payload
	uint32					lumpBytes;
	uint32					lumpVersion;	// game-lump dict version
	uint32					levelFlags;		// dflagslump_t::m_LevelFlags (0 when the lump is absent)
	uint32					checkMask;		// SHADOWMAP_CHECK_*
	ShadowMapValidateMode	mode[SHADOWMAP_MODE_COUNT];

	// Direct spans for the baker; file offsets and read callback for runtime.
	const texinfo_t *texinfos;
	const dmodel_t *models;
	uint32 texinfoCount, modelCount;
	uint32 texinfoOffset, modelsOffset;
	ShadowMapBspReadFn read;
	void *readContext;

	ShadowMapValidateInput() : lump( NULL ), lumpBytes( 0 ), lumpVersion( 0 ), levelFlags( 0 ), checkMask( SHADOWMAP_CHECK_ALL ),
		texinfos( NULL ), models( NULL ), texinfoCount( 0 ), modelCount( 0 ),
		texinfoOffset( 0 ), modelsOffset( 0 ), read( NULL ), readContext( NULL ) {}
};

struct ShadowMapModeView
{
	const ShadowMapModeDisk		*record;
	const ShadowMapLightDisk	*lights;
	uint32						lightCount;
	int32						sunLightIndex;
	bool						runtime;		// runtimeModeMask bit
	const ShadowMapReceiverFaceDisk *receiverFaces;
	uint32 receiverFaceCount;
	const ShadowMapReceiverTriangleDisk *receiverTriangles;
	uint32 receiverTriangleCount;
	const uint8 *sunVisibility;
	uint32 sunVisibilityCount;
};

struct ShadowMapLumpView
{
	const ShadowMapLumpDisk		*header;
	ShadowMapModeView			mode[SHADOWMAP_MODE_COUNT];
};

inline bool ShadowMap_IsFiniteFloat( float f )				{ return f == f && f <= FLT_MAX && f >= -FLT_MAX; }
inline bool ShadowMap_ValidSunAngularRadius( float degrees )	{ return ShadowMap_IsFiniteFloat( degrees ) && degrees >= 0.0f && degrees < SHADOWMAP_MAX_SUN_ANGULAR_RADIUS; }
inline bool ShadowMap_ValidLocalSourceRadius( float units )	{ return ShadowMap_IsFiniteFloat( units ) && units >= 0.0f; }
inline uint32 ShadowMap_LevelFlagDirect( int mode )				{ return mode == SHADOWMAP_MODE_HDR ? LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR : LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR; }

inline bool ShadowMap_RangeOk( uint32 lumpBytes, uint32 offset, uint32 count, uint32 stride )
{
	if ( count == 0 )
		return offset == 0;
	if ( ( offset & 3 ) != 0 || offset < sizeof( ShadowMapLumpDisk ) )
		return false;
	return (uint64)offset + (uint64)count * (uint64)stride <= (uint64)lumpBytes;
}

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
// Canonical CRC excludes only the zero padding after the scalar section.
inline uint32 ShadowMap_ReceiverDataCRC32( const ShadowMapReceiverFaceDisk *faces, uint32 faceCount,
	const ShadowMapReceiverTriangleDisk *triangles, uint32 triangleCount, const uint8 *visibility, uint32 visibilityCount )
{
	CRC32_t crc;
	CRC32_Init( &crc );
	if ( faceCount ) CRC32_ProcessBuffer( &crc, faces, (int)( faceCount * sizeof( *faces ) ) );
	if ( triangleCount ) CRC32_ProcessBuffer( &crc, triangles, (int)( triangleCount * sizeof( *triangles ) ) );
	if ( visibilityCount ) CRC32_ProcessBuffer( &crc, visibility, (int)visibilityCount );
	CRC32_Final( &crc );
	return (uint32)crc;
}

template <typename T>
inline bool ShadowMap_ReadRecord( const ShadowMapValidateInput &in, const T *records, uint32 fileOffset, uint32 count, uint32 index, T &out )
{
	if ( index >= count ) return false;
	if ( records ) { out = records[index]; return true; }
	const uint64 offset = (uint64)fileOffset + (uint64)index * sizeof( T );
	return in.read && offset + sizeof( T ) <= 0xFFFFFFFFu &&
		in.read( in.readContext, (uint32)offset, sizeof( T ), &out );
}

inline bool ShadowMap_ValidateReceiverFaces( const ShadowMapValidateInput &in, int mode, const ShadowMapModeView &view )
{
	const ShadowMapValidateMode &vm = in.mode[mode];
	if ( ( in.checkMask & SHADOWMAP_CHECK_FACES ) &&
		( vm.faceCount > MAX_MAP_FACES || !in.modelCount || in.modelCount > MAX_MAP_MODELS ||
			!in.texinfoCount || in.texinfoCount > MAX_MAP_TEXINFO ) ) return false;
	if ( in.checkMask & SHADOWMAP_CHECK_FACES )
	{
		// BSP model ranges must be valid and uniquely own their faces.
		uint32 starts[MAX_MAP_MODELS], ends[MAX_MAP_MODELS];
		for ( uint32 m = 0; m < in.modelCount; ++m )
		{
			dmodel_t model;
			if ( !ShadowMap_ReadRecord( in, in.models, in.modelsOffset, in.modelCount, m, model ) ||
				model.firstface < 0 || model.numfaces < 0 ||
				(uint64)model.firstface + model.numfaces > vm.faceCount ) return false;
			starts[m] = model.firstface;
			ends[m] = model.firstface + model.numfaces;
			if ( starts[m] != ends[m] )
				for ( uint32 previous = 0; previous < m; ++previous )
					if ( starts[previous] != ends[previous] && starts[m] < ends[previous] && starts[previous] < ends[m] ) return false;
		}
	}
	uint64 scalarPrefix = 0, trianglePrefix = 0, previousLightingEnd = 0;
	uint32 previousFace = 0;
	for ( uint32 i = 0; i < view.receiverFaceCount; ++i )
	{
		const ShadowMapReceiverFaceDisk &f = view.receiverFaces[i];
		const uint64 luxels = (uint64)f.luxelW * f.luxelH;
		if ( f.reserved || ( f.flags & ~( SHADOWMAP_RECEIVER_BUMPED | SHADOWMAP_RECEIVER_DISPLACEMENT ) ) ||
			f.dfaceIndex >= MAX_MAP_FACES || f.modelIndex >= MAX_MAP_MODELS || ( i && f.dfaceIndex <= previousFace ) ||
			!f.luxelW || !f.luxelH || luxels > MAX_MAP_LIGHTING || f.numStyles == 0 || f.numStyles > MAXLIGHTMAPS ||
			f.numChannels != ( ( f.flags & SHADOWMAP_RECEIVER_BUMPED ) ? 4u : 1u ) ||
			f.firstSunVisibility != scalarPrefix || f.firstTriangle != trianglePrefix || !f.triangleCount ||
			scalarPrefix + luxels > view.sunVisibilityCount || trianglePrefix + f.triangleCount > view.receiverTriangleCount ||
			( f.lightingOffset & 3 ) || f.lightingOffset < f.numStyles * 4 ||
			(uint64)f.lightingOffset + luxels * f.numChannels * f.numStyles * 4 > view.record->lightingBytes )
			return false;
		if ( f.lightingOffset - f.numStyles * 4 < previousLightingEnd ) return false;
		previousLightingEnd = (uint64)f.lightingOffset + luxels * f.numChannels * f.numStyles * 4;
		for ( int axis = 0; axis < 2; ++axis )
			for ( int c = 0; c < 4; ++c )
				if ( !ShadowMap_IsFiniteFloat( f.worldToLuxel[axis][c] ) ) return false;
		for ( int c = 0; c < 4; ++c )
			if ( !ShadowMap_IsFiniteFloat( f.plane[c] ) ) return false;
		const double normalLength = (double)f.plane[0]*f.plane[0] + (double)f.plane[1]*f.plane[1] + (double)f.plane[2]*f.plane[2];
		if ( normalLength < 0.99 || normalLength > 1.01 ) return false;
		for ( uint32 t = 0; t < f.triangleCount; ++t )
		{
			const ShadowMapReceiverTriangleDisk &tri = view.receiverTriangles[f.firstTriangle + t];
			for ( int v = 0; v < 3; ++v )
			{
				for ( int c = 0; c < 3; ++c )
					if ( !ShadowMap_IsFiniteFloat( tri.position[v][c] ) ) return false;
				for ( int c = 0; c < 2; ++c )
					if ( !ShadowMap_IsFiniteFloat( tri.luxel[v][c] ) ) return false;
				if ( f.flags & SHADOWMAP_RECEIVER_DISPLACEMENT )
				{
					if ( tri.luxel[v][0] < 0 || tri.luxel[v][0] > f.luxelW - 1 ||
						tri.luxel[v][1] < 0 || tri.luxel[v][1] > f.luxelH - 1 ) return false;
				}
				else
				{
					// Validate affine labels, not ideal coplanarity: shipped BSP
					// windings can deviate from their nominal plane.
					for ( int axis = 0; axis < 2; ++axis )
					{
						double value = f.worldToLuxel[axis][3];
						double magnitude = fabs( value );
						for ( int c = 0; c < 3; ++c )
						{
							const double term = (double)tri.position[v][c] * f.worldToLuxel[axis][c];
							value += term;
							magnitude += fabs( term );
						}
						const double expected = tri.luxel[v][axis];
						if ( fabs( value - expected ) > 16.0 * FLT_EPSILON * ( magnitude + fabs( expected ) + 1.0 ) ) return false;
					}
				}
			}
		}
		if ( in.checkMask & SHADOWMAP_CHECK_FACES )
		{
			dface_t bspFace;
			texinfo_t info;
			dmodel_t model;
			if ( !ShadowMap_ReadRecord( in, vm.faces, vm.facesOffset, vm.faceCount, f.dfaceIndex, bspFace ) ||
				bspFace.texinfo < 0 || !ShadowMap_ReadRecord( in, in.texinfos, in.texinfoOffset, in.texinfoCount, bspFace.texinfo, info ) ||
				!ShadowMap_ReadRecord( in, in.models, in.modelsOffset, in.modelCount, f.modelIndex, model ) ||
				bspFace.lightofs < 0 || f.lightingOffset != (uint32)bspFace.lightofs ||
				bspFace.m_LightmapTextureSizeInLuxels[0] < 0 || bspFace.m_LightmapTextureSizeInLuxels[1] < 0 ||
				f.luxelW != (uint32)bspFace.m_LightmapTextureSizeInLuxels[0] + 1 ||
				f.luxelH != (uint32)bspFace.m_LightmapTextureSizeInLuxels[1] + 1 ||
				!!( f.flags & SHADOWMAP_RECEIVER_BUMPED ) != !!( info.flags & SURF_BUMPLIGHT ) ||
				!!( f.flags & SHADOWMAP_RECEIVER_DISPLACEMENT ) != ( bspFace.dispinfo >= 0 ) ||
				model.firstface < 0 || model.numfaces < 0 || (uint64)model.firstface + model.numfaces > vm.faceCount ||
				f.dfaceIndex < (uint32)model.firstface || f.dfaceIndex >= (uint64)model.firstface + model.numfaces )
				return false;
			uint32 styles = 0;
			while ( styles < MAXLIGHTMAPS && bspFace.styles[styles] != 255 ) ++styles;
			if ( styles != f.numStyles || bspFace.styles[0] != 0 ) return false;
			for ( uint32 s = styles; s < MAXLIGHTMAPS; ++s ) if ( bspFace.styles[s] != 255 ) return false;
		}
		previousFace = f.dfaceIndex;
		scalarPrefix += luxels;
		trianglePrefix += f.triangleCount;
	}
	if ( scalarPrefix != view.sunVisibilityCount || trianglePrefix != view.receiverTriangleCount ) return false;
	if ( in.checkMask & SHADOWMAP_CHECK_FACES )
	{
		// Every encoded face needs its guard, including black and zero-sample faces.
		uint32 receiver = 0;
		for ( uint32 i = 0; i < vm.faceCount; ++i )
		{
			dface_t f;
			if ( !ShadowMap_ReadRecord( in, vm.faces, vm.facesOffset, vm.faceCount, i, f ) ) return false;
			if ( f.lightofs < 0 ) continue;
			if ( receiver >= view.receiverFaceCount || view.receiverFaces[receiver++].dfaceIndex != i ) return false;
		}
		if ( receiver != view.receiverFaceCount ) return false;
	}
	return true;
}

// Returns false with SHADOWMAP_ERR_INVALID_METADATA (plus a ": detail" suffix) on any failure.
inline bool ShadowMap_ValidateLump( const ShadowMapValidateInput &in, ShadowMapLumpView &out, char *error, int errorBytes )
{
	memset( &out, 0, sizeof( out ) );
	const char *detail = NULL;
	const uint8 *base = (const uint8 *)in.lump;
	const ShadowMapLumpDisk *header = (const ShadowMapLumpDisk *)base;

	do
	{
		if ( !base || in.lumpBytes < sizeof( ShadowMapLumpDisk ) || in.lumpBytes > 0x7FFFFFFFu || ( (uintp)base & 3 ) )
																				{ detail = "lump too small or unaligned"; break; }
		if ( in.lumpVersion != GAMELUMP_RESTIR_SHADOWMAPS_VERSION )				{ detail = "unknown rshd version"; break; }
		if ( header->byteSize != in.lumpBytes || ( in.lumpBytes & 3 ) != 0 )		{ detail = "byteSize mismatch"; break; }
		if ( header->reserved[0] || header->reserved[1] )						{ detail = "header reserved"; break; }
		if ( header->runtimeModeMask == 0 || ( header->runtimeModeMask & ~3u ) )	{ detail = "runtime mode mask"; break; }
		out.header = header;
		bool failed = false;
		uint64 cursor = sizeof( ShadowMapLumpDisk );
		for ( int m = 0; m < SHADOWMAP_MODE_COUNT && !failed; ++m )
		{
			const ShadowMapModeDisk &rec = header->mode[m];
			ShadowMapModeView &view = out.mode[m];
			view.record = &rec;
			view.runtime = ( header->runtimeModeMask & SHADOWMAP_MODE_BIT( m ) ) != 0;
			view.sunLightIndex = rec.sunLightIndex;
			if ( rec.reserved || rec.reserved2 )									{ detail = "mode reserved"; failed = true; break; }
			if ( !view.runtime )
			{
				const uint32 *words = (const uint32 *)&rec;
				for ( int w = 0; w < (int)( sizeof( rec ) / 4 ); ++w )
				{
					if ( w == offsetof( ShadowMapModeDisk, sunLightIndex ) / 4 ) continue;
					if ( words[w] != 0 ) { detail = "absent mode has data"; failed = true; break; }
				}
				if ( failed ) break;
				if ( rec.sunLightIndex != -1 )									{ detail = "absent mode sun index"; failed = true; break; }
				if ( ( in.checkMask & SHADOWMAP_CHECK_LEVEL_FLAGS ) && ( in.levelFlags & ShadowMap_LevelFlagDirect( m ) ) )
																				{ detail = "orphan level flag"; failed = true; break; }
				continue;
			}
			const ShadowMapValidateMode &vm = in.mode[m];
			if ( in.checkMask && !vm.available )								{ detail = "converted mode lumps missing"; failed = true; break; }
			if ( !rec.lightCount || rec.lightCount > MAX_MAP_WORLDLIGHTS || rec.lightingBytes > MAX_MAP_LIGHTING ||
				!ShadowMap_RangeOk( in.lumpBytes, rec.lightsOffset, rec.lightCount, sizeof( ShadowMapLightDisk ) ) ||
				rec.lightsOffset != cursor )
																				{ detail = "light section range or canonical order"; failed = true; break; }
			cursor += (uint64)rec.lightCount * sizeof( ShadowMapLightDisk );
			view.lights = (const ShadowMapLightDisk *)( base + rec.lightsOffset );
			view.lightCount = rec.lightCount;
			if ( ( in.checkMask & SHADOWMAP_CHECK_LEVEL_FLAGS ) && !( in.levelFlags & ShadowMap_LevelFlagDirect( m ) ) )
																				{ detail = "missing level flag"; failed = true; break; }
			if ( ( in.checkMask & SHADOWMAP_CHECK_LIGHTING ) && ( rec.lightingBytes != vm.lightingBytes || rec.lightingCRC32 != vm.lightingCRC32 ) )
																				{ detail = "lighting CRC"; failed = true; break; }
			if ( ( in.checkMask & SHADOWMAP_CHECK_FACES ) && rec.facesCRC32 != vm.facesCRC32 )
																				{ detail = "faces CRC"; failed = true; break; }
			if ( ( in.checkMask & SHADOWMAP_CHECK_WORLDLIGHTS ) && rec.worldlightsCRC32 != vm.worldlightsCRC32 )
																				{ detail = "worldlights CRC"; failed = true; break; }
			if ( rec.sunLightIndex < -1 || rec.sunLightIndex >= (int32)rec.lightCount )
																				{ detail = "sun index"; failed = true; break; }
			if ( rec.sunLightIndex > 0 )										{ detail = "sun must be first"; failed = true; break; }
			for ( uint32 i = 0; i < rec.lightCount; ++i )
			{
				const ShadowMapLightDisk &l = view.lights[i];
				const bool isSun = (int32)i == rec.sunLightIndex;
				if ( l.reserved )												{ detail = "light reserved"; failed = true; break; }
				if ( !ShadowMap_FiniteVector( l.light.origin ) || !ShadowMap_FiniteVector( l.light.intensity ) || !ShadowMap_FiniteVector( l.light.normal ) ||
					 !ShadowMap_IsFiniteFloat( l.light.constant_attn ) || !ShadowMap_IsFiniteFloat( l.light.linear_attn ) || !ShadowMap_IsFiniteFloat( l.light.quadratic_attn ) ||
					 !ShadowMap_IsFiniteFloat( l.light.stopdot ) || !ShadowMap_IsFiniteFloat( l.light.stopdot2 ) || !ShadowMap_IsFiniteFloat( l.light.exponent ) || !ShadowMap_IsFiniteFloat( l.light.radius ) )
																				{ detail = "light record not finite"; failed = true; break; }
				// VRAD convention: endFade < 0 (baker default -1) means "no quintic fade"; startFade >= 0.
				if ( !ShadowMap_IsFiniteFloat( l.startFade ) || !ShadowMap_IsFiniteFloat( l.endFade ) || !ShadowMap_IsFiniteFloat( l.capDist ) ||
					 l.startFade < 0.0f || l.capDist < 0.0f )
																				{ detail = "light fade"; failed = true; break; }
				if ( isSun )
				{
					if ( l.light.type != emit_skylight )						{ detail = "sun type"; failed = true; break; }
					if ( !ShadowMap_ValidSunAngularRadius( l.shadowSunAngularRadius ) || l.shadowSourceRadius != 0.0f )
																				{ detail = "sun emitter size"; failed = true; break; }
					if ( l.startFade != 0.0f || l.endFade != 0.0f || l.capDist != 0.0f )	{ detail = "sun fade"; failed = true; break; }
				}
				else
				{
					if ( l.light.type != emit_point && l.light.type != emit_spotlight )	{ detail = "local type"; failed = true; break; }
					if ( !ShadowMap_ValidLocalSourceRadius( l.shadowSourceRadius ) || l.shadowSunAngularRadius != 0.0f )
																				{ detail = "local emitter size"; failed = true; break; }
					if ( l.capDist <= 0.0f )									{ detail = "local cap distance"; failed = true; break; }
				}
			}
			if ( failed ) break;
			if ( rec.sunLightIndex < 0 )
			{
				if ( rec.receiverFacesOffset || rec.receiverFaceCount || rec.receiverTrianglesOffset || rec.receiverTriangleCount ||
					rec.sunVisibilityOffset || rec.sunVisibilityCount || rec.receiverDataCRC32 )
																				{ detail = "orphan receiver sections"; failed = true; break; }
				continue;
			}
			if ( !rec.receiverFaceCount || rec.receiverFaceCount > MAX_MAP_FACES || !rec.receiverTriangleCount ||
				!rec.sunVisibilityCount || rec.sunVisibilityCount > MAX_MAP_LIGHTING ||
				!ShadowMap_RangeOk( in.lumpBytes, rec.receiverFacesOffset, rec.receiverFaceCount, sizeof( ShadowMapReceiverFaceDisk ) ) ||
				!ShadowMap_RangeOk( in.lumpBytes, rec.receiverTrianglesOffset, rec.receiverTriangleCount, sizeof( ShadowMapReceiverTriangleDisk ) ) ||
				!ShadowMap_RangeOk( in.lumpBytes, rec.sunVisibilityOffset, rec.sunVisibilityCount, 1 ) )
																				{ detail = "missing or invalid receiver sections"; failed = true; break; }
			if ( rec.receiverFacesOffset != cursor )								{ detail = "receiver face overlap or order"; failed = true; break; }
			cursor += (uint64)rec.receiverFaceCount * sizeof( ShadowMapReceiverFaceDisk );
			if ( rec.receiverTrianglesOffset != cursor )							{ detail = "receiver triangle overlap or order"; failed = true; break; }
			cursor += (uint64)rec.receiverTriangleCount * sizeof( ShadowMapReceiverTriangleDisk );
			if ( rec.sunVisibilityOffset != cursor )								{ detail = "receiver scalar overlap or order"; failed = true; break; }
			cursor += rec.sunVisibilityCount;
			const uint64 paddedEnd = ( cursor + 3 ) & ~(uint64)3;
			if ( paddedEnd > in.lumpBytes )										{ detail = "scalar padding range"; failed = true; break; }
			for ( ; cursor < paddedEnd; ++cursor )
				if ( base[cursor] != 0 ) { detail = "scalar padding"; failed = true; break; }
			if ( failed ) break;
			view.receiverFaces = (const ShadowMapReceiverFaceDisk *)( base + rec.receiverFacesOffset );
			view.receiverFaceCount = rec.receiverFaceCount;
			view.receiverTriangles = (const ShadowMapReceiverTriangleDisk *)( base + rec.receiverTrianglesOffset );
			view.receiverTriangleCount = rec.receiverTriangleCount;
			view.sunVisibility = base + rec.sunVisibilityOffset;
			view.sunVisibilityCount = rec.sunVisibilityCount;
			if ( rec.receiverDataCRC32 != ShadowMap_ReceiverDataCRC32( view.receiverFaces, view.receiverFaceCount,
				view.receiverTriangles, view.receiverTriangleCount, view.sunVisibility, view.sunVisibilityCount ) )
																				{ detail = "receiver CRC"; failed = true; break; }
			if ( !ShadowMap_ValidateReceiverFaces( in, m, view ) )					{ detail = "receiver face or geometry range"; failed = true; break; }
		}
		if ( failed ) break;
		if ( cursor != in.lumpBytes )											{ detail = "trailing or overlapping sections"; break; }
		return true;
	} while ( false );

	if ( error && errorBytes > 0 )
	{
		if ( detail )
			V_snprintf( error, errorBytes, "%s: %s", SHADOWMAP_ERR_INVALID_METADATA, detail );
		else
			V_strncpy( error, SHADOWMAP_ERR_INVALID_METADATA, errorBytes );
	}
	memset( &out, 0, sizeof( out ) );
	return false;
}


//-----------------------------------------------------------------------------
// Reading the sidecar and its cross-check lumps straight from a BSP file
// (client CRC checks and the server, which has no game-lump API). The file
// is the engine's own map file (maps/<name>.bsp); external .lmp overrides are
// not supported for converted maps (the CRCs would mismatch -> rejection).
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
		if ( h->lumps[i].fileofs < 0 || h->lumps[i].filelen < 0 ||
			(uint64)h->lumps[i].fileofs + h->lumps[i].filelen > 0xFFFFFFFFu ) return false;
		lumps[i].fileofs = h->lumps[i].fileofs;
		lumps[i].filelen = h->lumps[i].filelen;
		lumps[i].version = h->lumps[i].version;
		lumps[i].uncompressedSize = (uint32)h->lumps[i].uncompressedSize;
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

// Fills the validator input for both modes from a BSP file: lighting 8/53, faces 7/58, worldlights 15/54.
// Lighting decides availability; a map with every light selected has no remaining worldlights.
inline bool ShadowMap_BuildValidateInputFromBsp( ShadowMapBspReadFn read, void *context, const ShadowMapBspLumpInfo lumps[HEADER_LUMPS],
	uint32 levelFlags, ShadowMapValidateInput &in, uint8 *scratch, uint32 scratchBytes )
{
	in.levelFlags = levelFlags;
	in.checkMask = SHADOWMAP_CHECK_ALL;
	in.read = read;
	in.readContext = context;
	in.texinfos = NULL;
	in.models = NULL;
	const ShadowMapBspLumpInfo &textures = lumps[LUMP_TEXINFO], &models = lumps[LUMP_MODELS];
	if ( textures.uncompressedSize || models.uncompressedSize || textures.filelen % sizeof( texinfo_t ) ||
		models.filelen % sizeof( dmodel_t ) || textures.filelen / sizeof( texinfo_t ) > MAX_MAP_TEXINFO ||
		models.filelen / sizeof( dmodel_t ) > MAX_MAP_MODELS ) return false;
	in.texinfoCount = textures.filelen / sizeof( texinfo_t );
	in.modelCount = models.filelen / sizeof( dmodel_t );
	in.texinfoOffset = textures.fileofs;
	in.modelsOffset = models.fileofs;
	for ( int m = 0; m < SHADOWMAP_MODE_COUNT; ++m )
	{
		const ShadowMapBspLumpInfo &lighting = lumps[m == SHADOWMAP_MODE_HDR ? LUMP_LIGHTING_HDR : LUMP_LIGHTING];
		const ShadowMapBspLumpInfo &faces = lumps[m == SHADOWMAP_MODE_HDR ? LUMP_FACES_HDR : LUMP_FACES];
		const ShadowMapBspLumpInfo &worldlights = lumps[m == SHADOWMAP_MODE_HDR ? LUMP_WORLDLIGHTS_HDR : LUMP_WORLDLIGHTS];
		ShadowMapValidateMode &vm = in.mode[m];
		vm = ShadowMapValidateMode();
		if ( lighting.uncompressedSize || faces.uncompressedSize || worldlights.uncompressedSize )
			continue;	// compressed lumps: not supported for converted maps -> mode unavailable -> rejection
		if ( lighting.filelen > MAX_MAP_LIGHTING || faces.filelen % sizeof( dface_t ) ||
			faces.filelen / sizeof( dface_t ) > MAX_MAP_FACES || worldlights.filelen % sizeof( dworldlight_t ) ||
			worldlights.filelen / sizeof( dworldlight_t ) > MAX_MAP_WORLDLIGHTS ) return false;
		vm.faceCount = faces.filelen / sizeof( dface_t );
		vm.facesOffset = faces.fileofs;
		// A legitimate map whose every light was selected writes an EMPTY worldlights lump; lighting decides availability.
		vm.available = lighting.filelen != 0;
		if ( !vm.available )
			continue;
		vm.lightingBytes = lighting.filelen;
		if ( !ShadowMap_CRC32Lump( read, context, lighting, vm.lightingCRC32, scratch, scratchBytes ) ||
			 !ShadowMap_CRC32Lump( read, context, faces, vm.facesCRC32, scratch, scratchBytes ) ||
			 !ShadowMap_CRC32Lump( read, context, worldlights, vm.worldlightsCRC32, scratch, scratchBytes ) )
			return false;
	}
	return true;
}

#endif // SHADOWMAP_BSP_H
