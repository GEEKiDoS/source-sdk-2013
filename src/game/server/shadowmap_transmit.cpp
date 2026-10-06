//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Read-only map admission and conservative public-caster transmission.
//          No client DLL or engine hooks are needed by a dedicated server.
//
//=============================================================================//

#include "cbase.h"
#include "shadowmap_transmit.h"
#include "hlight_bsp.h"
#include "shadowmap_scene.h"
#include "entitylist.h"
#include "world.h"
#include "collisionproperty.h"
#include "tier1/utlvector.h"
#include "tier1/lzmaDecoder.h"
#include "filesystem.h"
#include "tier2/tier2.h"
#include <limits.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

namespace
{
	bool g_bFeatureMap = false;
	bool g_bReady = false;
	int g_nEligibilityTick = -1;
	CUtlVector< ShadowMapInfluenceVolume_t > g_Volumes;
	CUtlVector< uint8 > g_SpatialEligibility;
	CUtlVector< uint8 > g_CallbackMembership;

	struct ServerBspReadContext
	{
		FileHandle_t file;
		uint32 fileBytes;
	};

	bool ReadServerBsp( void *context, uint32 offset, uint32 bytes, void *out )
	{
		ServerBspReadContext *pRead = (ServerBspReadContext *)context;
		if ( offset > INT_MAX || bytes > INT_MAX || (uint64)offset + bytes > pRead->fileBytes )
			return false;
		g_pFullFileSystem->Seek( pRead->file, offset, FILESYSTEM_SEEK_HEAD );
		return g_pFullFileSystem->Read( out, bytes, pRead->file ) == (int)bytes;
	}

	// Hash decoded native lump bytes, retaining only fixed-size input/output
	// chunks. CLZMAStream owns its bounded dictionary, not a copy of the BSP.
	bool CRC32ServerDecodedLump( ServerBspReadContext &context, const ShadowMapBspLumpInfo &source,
		uint32 decodedBytes, uint32 &crc, uint8 *scratch, uint32 scratchBytes )
	{
		if ( (uint64)source.fileofs + source.filelen > context.fileBytes )
			return false;
		if ( !source.uncompressedSize )
			return ShadowMap_CRC32Lump( ReadServerBsp, &context, source, crc, scratch, scratchBytes );

		uint8 compressed[16384];
		uint32 inputBytes = MIN( source.filelen, (uint32)sizeof( compressed ) );
		if ( inputBytes < sizeof( lzma_header_t ) ||
			!ReadServerBsp( &context, source.fileofs, inputBytes, compressed ) )
			return false;
		lzma_header_t header;
		V_memcpy( &header, compressed, sizeof( header ) );
		uint32 dictionaryBytes;
		V_memcpy( &dictionaryBytes, header.properties + 1, sizeof( dictionaryBytes ) );
		// Check all capacities/properties before the decoder allocates anything.
		// Match the checked Source LZMA convention used by shader_vcs_dx12.
		if ( header.id != LZMA_ID || LittleLong( header.actualSize ) != decodedBytes ||
			!decodedBytes || LittleLong( header.lzmaSize ) != source.filelen - sizeof( header ) ||
			!header.lzmaSize || header.properties[0] >= 225 ||
			LittleLong( dictionaryBytes ) > 64u * 1024u * 1024u )
			return false;

		CLZMAStream stream;
		CRC32_t c;
		CRC32_Init( &c );
		uint32 fileRead = inputBytes, inputUsed = 0, outputBytes = 0;
		for ( ;; )
		{
			unsigned int consumed = 0, produced = 0;
			if ( !stream.Read( compressed + inputUsed, inputBytes - inputUsed,
				scratch, scratchBytes, consumed, produced ) ||
				consumed > inputBytes - inputUsed || produced > scratchBytes ||
				produced > decodedBytes - outputBytes || ( !consumed && !produced ) )
				return false;
			inputUsed += consumed;
			outputBytes += produced;
			CRC32_ProcessBuffer( &c, scratch, (int)produced );
			if ( inputUsed == inputBytes && fileRead == source.filelen && outputBytes == decodedBytes )
				break;
			if ( inputUsed == inputBytes )
			{
				inputBytes = MIN( source.filelen - fileRead, (uint32)sizeof( compressed ) );
				if ( inputBytes && !ReadServerBsp( &context, source.fileofs + fileRead, inputBytes, compressed ) )
					return false;
				fileRead += inputBytes;
				inputUsed = 0;
			}
		}
		unsigned int remaining = 0;
		if ( !stream.GetExpectedBytesRemaining( remaining ) || remaining )
			return false;
		CRC32_Final( &c );
		crc = (uint32)c;
		return true;
	}

	bool ValidateServerManifestSources( ServerBspReadContext &context, const ShadowMapBspLumpInfo lumps[HEADER_LUMPS],
		const hlight::ManifestView &manifest, char *error, int errorBytes )
	{
		uint8 scratch[16384];
		uint32 crc[HEADER_LUMPS];
		bool crcReady[HEADER_LUMPS] = { false };
		for ( int m = 0; m < SHADOWMAP_MODE_COUNT; ++m )
		{
			if ( !manifest.mode[m].runtime )
				continue;
			const hlight::ManifestModeDisk &mode = *manifest.mode[m].record;
			const ShadowMapBspLumpInfo &faces = lumps[mode.faceLump];
			const ShadowMapBspLumpInfo &lighting = lumps[mode.lightingLump];
			const ShadowMapBspLumpInfo &worldlights = lumps[mode.worldlightsLump];
			const uint32 faceBytes = faces.uncompressedSize ? faces.uncompressedSize : faces.filelen;
			const uint32 lightingBytes = lighting.uncompressedSize ? lighting.uncompressedSize : lighting.filelen;
			const uint32 worldlightBytes = worldlights.uncompressedSize ? worldlights.uncompressedSize : worldlights.filelen;
			if ( faces.version != LUMP_FACES_VERSION || lighting.version != LUMP_LIGHTING_VERSION ||
				!faceBytes || faceBytes % sizeof( dface_t ) || faceBytes / sizeof( dface_t ) > MAX_MAP_FACES ||
				lightingBytes != mode.lightingBytes || lightingBytes > MAX_MAP_LIGHTING || ( lightingBytes & 3 ) ||
				worldlightBytes % sizeof( dworldlight_t ) || worldlightBytes / sizeof( dworldlight_t ) > MAX_MAP_WORLDLIGHTS )
			{
				V_snprintf( error, errorBytes, "rshd v4 mode %d source version/size", m );
				return false;
			}
			const uint32 ids[] = { mode.faceLump, mode.lightingLump, mode.worldlightsLump };
			const uint32 expected[] = { mode.facesCRC32, mode.lightingCRC32, mode.worldlightsCRC32 };
			const uint32 decodedBytes[] = { faceBytes, lightingBytes, worldlightBytes };
			for ( int i = 0; i < ARRAYSIZE( ids ); ++i )
			{
				const uint32 id = ids[i];
				const ShadowMapBspLumpInfo &source = lumps[id];
				// This checks named base-BSP sources, not presumed external override
				// filenames. Only native effective-load observations can certify those.
				// An explicit HDR mode can name LDR faces: read/decode/hash that source
				// once, without validating inverse receiver geometry.
				if ( !crcReady[id] )
				{
					if ( !CRC32ServerDecodedLump( context, source, decodedBytes[i], crc[id], scratch, sizeof( scratch ) ) )
					{
						V_snprintf( error, errorBytes, "rshd v4 mode %d lump %u source bounds/read/decoding", m, id );
						return false;
					}
					crcReady[id] = true;
				}
				if ( crc[id] != expected[i] )
				{
					V_snprintf( error, errorBytes, "rshd v4 mode %d lump %u source CRC", m, id );
					return false;
				}
			}
		}
		return true;
	}

	bool ReadServerManifestPayload( ServerBspReadContext &context, const ShadowMapBspLumpInfo lumps[HEADER_LUMPS],
		uint32 offset, uint32 bytes, uint32 levelFlags, CUtlVector< uint8 > &payload,
		hlight::ManifestView &view, char *error, int errorBytes )
	{
		const uint64 maxManifestBytes = sizeof( hlight::ManifestDisk ) +
			(uint64)SHADOWMAP_MODE_COUNT * MAX_MAP_WORLDLIGHTS * sizeof( ShadowMapLightDisk );
		if ( bytes < sizeof( hlight::ManifestDisk ) || bytes > maxManifestBytes || bytes > INT_MAX ||
			(uint64)offset + bytes > context.fileBytes )
		{
			V_snprintf( error, errorBytes, "rshd v4 file range/size" );
			return false;
		}
		payload.SetCount( bytes );
		if ( !ReadServerBsp( &context, offset, bytes, payload.Base() ) )
		{
			V_snprintf( error, errorBytes, "rshd v4 data read" );
			return false;
		}
		return hlight::ValidateManifest( payload.Base(), bytes, levelFlags, view, error, errorBytes ) &&
			ValidateServerManifestSources( context, lumps, view, error, errorBytes );
	}

	bool ValidBounds( const Vector &mins, const Vector &maxs )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( !ShadowMap_IsFiniteFloat( mins[axis] ) || !ShadowMap_IsFiniteFloat( maxs[axis] ) || mins[axis] > maxs[axis] )
				return false;
		}
		return true;
	}

	bool ValidVolume( const ShadowMapInfluenceVolume_t &volume )
	{
		if ( !ValidBounds( volume.mins, volume.maxs ) || volume.planeCount < 0 || volume.planeCount > 6 )
			return false;
		for ( int p = 0; p < volume.planeCount; ++p )
		{
			if ( !volume.planes[p].m_Normal.IsValid() || !ShadowMap_IsFiniteFloat( volume.planes[p].m_Dist ) )
				return false;
		}
		return true;
	}

	void AddUniqueVolume( const ShadowMapInfluenceVolume_t &volume )
	{
		// Identical HDR/LDR projections need only one spatial test per entity.
		for ( int v = 0; v < g_Volumes.Count(); ++v )
		{
			const ShadowMapInfluenceVolume_t &other = g_Volumes[v];
			if ( other.mins != volume.mins || other.maxs != volume.maxs || other.planeCount != volume.planeCount ||
				other.zNear != volume.zNear || other.zFar != volume.zFar || other.cube != volume.cube ||
				other.tanRenderedHalfFov != volume.tanRenderedHalfFov )
				continue;
			bool same = true;
			for ( int p = 0; p < volume.planeCount; ++p )
			{
				if ( other.planes[p].m_Normal != volume.planes[p].m_Normal || other.planes[p].m_Dist != volume.planes[p].m_Dist )
				{
					same = false;
					break;
				}
			}
			if ( same )
				return;
		}
		g_Volumes.AddToTail( volume );
	}

	bool RecipientPermitsChain( CBaseEntity *pEntity, CCheckTransmitInfo *pInfo )
	{
		// Never stop at an already-transmitted ancestor: recipient semantics still
		// apply. Bound the walk so a corrupt/cyclic hierarchy fails closed.
		for ( int depth = 0; pEntity && depth < MAX_EDICTS; ++depth )
		{
			edict_t *pEdict = pEntity->edict();
			if ( !pEdict || pEdict->IsFree() || !pEdict->GetNetworkable() ||
				pEntity->IsEFlagSet( EFL_SERVER_ONLY ) || ( pEdict->m_fStateFlags & FL_EDICT_DONTSEND ) )
				return false;

			// FULLCHECK is zero: resolve it through the virtual, not a bit mask.
			// Also call for PVSCHECK/ALWAYS ancestors, since owner-only entities
			// may deny this recipient without cached DONTSEND flags.
			const int flags = pEntity->ShouldTransmit( pInfo );
			if ( ( flags & FL_EDICT_DONTSEND ) ||
				!( flags & ( FL_EDICT_PVSCHECK | FL_EDICT_ALWAYS ) ) ||
				( pEdict->m_fStateFlags & FL_EDICT_DONTSEND ) )
				return false;

			CServerNetworkProperty *pParent = pEntity->NetworkProp()->GetNetworkParent();
			if ( !pParent )
				return true;
			pEntity = pParent->GetBaseEntity();
			if ( !pEntity )
				return false;
		}
		return false;
	}

	void TransmitEligibleEntity( CBaseEntity *pEntity, CCheckTransmitInfo *pInfo )
	{
		if ( !ShadowMapTransmit_IsSpatiallyEligible( pEntity ) || pEntity->IsEFlagSet( EFL_IN_SKYBOX ) )
			return;
		if ( pInfo->m_pTransmitEdict->Get( pEntity->entindex() ) )
			return;
		if ( RecipientPermitsChain( pEntity, pInfo ) )
			pEntity->SetTransmit( pInfo, true );
	}
}

void ShadowMapTransmit_LevelShutdown()
{
	g_bReady = false;
	g_bFeatureMap = false;
	g_nEligibilityTick = -1;
	g_Volumes.RemoveAll();
	g_SpatialEligibility.RemoveAll();
	g_CallbackMembership.RemoveAll();
}

void ShadowMapTransmit_LevelInit()
{
	ShadowMapTransmit_LevelShutdown();

	char mapPath[MAX_PATH];
	V_snprintf( mapPath, sizeof( mapPath ), "maps/%s.bsp", STRING( gpGlobals->mapname ) );
	ServerBspReadContext context;
	context.file = g_pFullFileSystem->Open( mapPath, "rb", "GAME" );
	if ( !context.file )
	{
		Warning( "%s: cannot read %s\n", SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE, mapPath );
		return;
	}
	context.fileBytes = g_pFullFileSystem->Size( context.file );
	ShadowMapBspLumpInfo lumps[HEADER_LUMPS];
	uint32 offset = 0, bytes = 0, version = 0, levelFlags = 0;
	bool compressed = false;
	const bool directoryRead = ShadowMap_ReadBspDirectory( ReadServerBsp, &context, lumps,
		offset, bytes, version, compressed, levelFlags );
	// A manifest or ownership flag identifies a feature candidate even when its
	// payload is corrupt; no unsupported metadata can advertise readiness.
	g_bFeatureMap = bytes != 0 || offset != 0 || version != 0 || compressed || ( levelFlags &
		( LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_NONHDR | LVLFLAGS_RUNTIME_SHADOWMAP_DIRECT_HDR ) ) != 0;
	if ( !directoryRead || compressed || ( g_bFeatureMap && !bytes ) )
	{
		g_pFullFileSystem->Close( context.file );
		Warning( "%s: BSP rshd directory/compression or orphan feature flags\n", SHADOWMAP_ERR_INVALID_METADATA );
		return;
	}
	if ( !g_bFeatureMap )
	{
		bool orphanAsset = false;
		if ( !hlight::FindPakAsset( ReadServerBsp, &context, lumps[LUMP_PAKFILE], orphanAsset ) || orphanAsset )
		{
			g_bFeatureMap = true;
			Warning( "%s: unreadable pak directory or orphan high-resolution asset\n", SHADOWMAP_ERR_INVALID_METADATA );
		}
		g_pFullFileSystem->Close( context.file );
		return;
	}
	if ( version != hlight::kManifestVersion )
	{
		g_pFullFileSystem->Close( context.file );
		Warning( "%s: unsupported rshd version %u; rebake legacy enhanced maps as manifest v4\n",
			SHADOWMAP_ERR_INVALID_METADATA, version );
		return;
	}

	CUtlVector< uint8 > payload;
	char error[256] = { 0 };
	hlight::ManifestView view = {};
	const bool valid = ReadServerManifestPayload( context, lumps, offset, bytes, levelFlags,
		payload, view, error, sizeof( error ) );
	g_pFullFileSystem->Close( context.file );
	if ( !valid )
	{
		Warning( "%s: %s\n", SHADOWMAP_ERR_INVALID_METADATA, error[0] ? error : "rshd v4 data read" );
		return;
	}
	g_bFeatureMap = view.header->runtimeModeMask != 0;

	CWorld *pWorld = GetWorldEntity();
	Vector worldMins, worldMaxs;
	if ( !pWorld )
	{
		Warning( "%s: no world entity\n", SHADOWMAP_ERR_TRANSMIT_UNAVAILABLE );
		return;
	}
	pWorld->GetWorldBounds( worldMins, worldMaxs );
	if ( !ValidBounds( worldMins, worldMaxs ) )
	{
		Warning( "%s: world bounds\n", SHADOWMAP_ERR_INVALID_METADATA );
		return;
	}

	bool hasSun = false;
	for ( int m = 0; m < SHADOWMAP_MODE_COUNT; ++m )
	{
		const hlight::ManifestModeView &mode = view.mode[m];
		if ( !mode.runtime )
			continue;
		for ( uint32 i = 0; i < mode.lightCount; ++i )
		{
			const ShadowMapLightDisk &light = mode.lights[i];
			if ( (int32)i == mode.sunLightIndex )
			{
				hasSun = true;
				continue;
			}
			ShadowMapInfluenceVolume_t volume;
			memset( &volume, 0, sizeof( volume ) );
			ShadowMapScene_LocalInfluence( light.light, worldMins, worldMaxs, volume );
			if ( !ValidVolume( volume ) || !ShadowMap_IsFiniteFloat( volume.zFar ) ||
				!ShadowMap_IsFiniteFloat( volume.zNear ) || volume.zFar <= volume.zNear )
			{
				Warning( "%s: local influence volume\n", SHADOWMAP_ERR_INVALID_METADATA );
				g_Volumes.RemoveAll();
				return;
			}
			AddUniqueVolume( volume );
		}
	}
	if ( hasSun )
	{
		ShadowMapInfluenceVolume_t volume;
		memset( &volume, 0, sizeof( volume ) );
		ShadowMapScene_SunDomain( worldMins, worldMaxs, volume );
		if ( !ValidVolume( volume ) )
		{
			Warning( "%s: sun influence volume\n", SHADOWMAP_ERR_INVALID_METADATA );
			g_Volumes.RemoveAll();
			return;
		}
		AddUniqueVolume( volume );
	}

	// RGB-only modes legitimately have no selected lights. They still advertise
	// verified feature-server compatibility, without inventing caster volumes.
	// The engine's transmit bitvector is MAX_EDICTS wide. Reserve that entire
	// index space once, including future entities, not just today's population.
	g_SpatialEligibility.SetCount( MAX_EDICTS );
	g_CallbackMembership.SetCount( MAX_EDICTS );
	memset( g_SpatialEligibility.Base(), 0, g_SpatialEligibility.Count() );
	memset( g_CallbackMembership.Base(), 0, g_CallbackMembership.Count() );
	g_bReady = true;
}

bool ShadowMapTransmit_Ready()
{
	return g_bReady;
}

bool ShadowMapTransmit_FeatureMap()
{
	return g_bFeatureMap;
}

void ShadowMapTransmit_BeginTick()
{
	if ( !g_bFeatureMap || !g_bReady || g_nEligibilityTick == gpGlobals->tickcount )
		return;
	g_nEligibilityTick = gpGlobals->tickcount;
	memset( g_SpatialEligibility.Base(), 0, g_SpatialEligibility.Count() );
	if ( !g_Volumes.Count() )
		return;
	for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity; pEntity = gEntList.NextEnt( pEntity ) )
	{
		edict_t *pEdict = pEntity->edict();
		const int index = pEntity->entindex();
		if ( !pEdict || pEdict->IsFree() || !pEdict->GetNetworkable() || index < 0 || index >= g_SpatialEligibility.Count() ||
			pEntity->IsEFlagSet( EFL_SERVER_ONLY ) || ( pEdict->m_fStateFlags & FL_EDICT_DONTSEND ) )
			continue;
		Vector mins, maxs;
		pEntity->CollisionProp()->WorldSpaceSurroundingBounds( &mins, &maxs );
		for ( int v = 0; v < g_Volumes.Count(); ++v )
		{
			if ( ShadowMapScene_VolumeIntersectsBox( g_Volumes[v], mins, maxs ) )
			{
				g_SpatialEligibility[index] = 1;
				break;
			}
		}
	}
}

bool ShadowMapTransmit_IsSpatiallyEligible( const CBaseEntity *pEntity )
{
	if ( !g_bFeatureMap || !g_bReady || !pEntity )
		return false;
	const int index = pEntity->entindex();
	return index >= 0 && index < g_SpatialEligibility.Count() && g_SpatialEligibility[index] != 0;
}

void ShadowMapTransmit_CheckTransmit( CCheckTransmitInfo *pInfo, const unsigned short *pEdictIndices, int nEdicts )
{
	if ( !g_bFeatureMap || !g_bReady || !pInfo || !pInfo->m_pTransmitEdict || !pInfo->m_pClientEnt )
		return;
	ShadowMapTransmit_BeginTick();
	memset( g_CallbackMembership.Base(), 0, g_CallbackMembership.Count() );
	for ( int i = 0; i < nEdicts; ++i )
	{
		const int index = pEdictIndices[i];
		if ( index >= g_CallbackMembership.Count() )
			continue;
		g_CallbackMembership[index] = 1;
		TransmitEligibleEntity( CBaseEntity::Instance( engine->PEntityOfEntIndex( index ) ), pInfo );
	}

	// The callback normally contains only the engine's current candidates.
	// Never-main-visible casters can be absent altogether, not merely out of PVS.
	if ( nEdicts != engine->GetEntityCount() )
	{
		for ( CBaseEntity *pEntity = gEntList.FirstEnt(); pEntity; pEntity = gEntList.NextEnt( pEntity ) )
		{
			const int index = pEntity->entindex();
			if ( index >= 0 && index < g_CallbackMembership.Count() && !g_CallbackMembership[index] )
				TransmitEligibleEntity( pEntity, pInfo );
		}
	}
}
