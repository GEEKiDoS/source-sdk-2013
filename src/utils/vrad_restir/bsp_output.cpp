//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Encode ordinary ReSTIR lighting and mode-owned shadow receivers,
//          staging rshd and pak payloads in the atomic BSP replacement.
//
//=============================================================================//

#include "bsp_output.h"
#include "vrad_restir.h"
#include "bsplib.h"
#include "cmdlib.h"
#include "filesystem_tools.h"
#include "tier1/strtools.h"
#include "tier1/utldict.h"
#include "tier1/utlbuffer.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "gamebspfile.h"
#include "shadowmap_bsp.h"
#include "hlight_output.h"

#include <windows.h>
#include <limits.h>
#include <string.h>

extern int numworldlightsLDR;
extern int numworldlightsHDR;
extern dworldlight_t dworldlightsLDR[MAX_MAP_WORLDLIGHTS];
extern dworldlight_t dworldlightsHDR[MAX_MAP_WORLDLIGHTS];
extern CUtlVector<dleafambientindex_t> g_LeafAmbientIndexLDR;
extern CUtlVector<dleafambientindex_t> g_LeafAmbientIndexHDR;
extern CUtlVector<dleafambientlighting_t> g_LeafAmbientLightingLDR;
extern CUtlVector<dleafambientlighting_t> g_LeafAmbientLightingHDR;

bool ReSTIR_ReusePairedLeafAmbientLighting()
{
	if ( !g_bHDR || numworldlightsHDR != numworldlightsLDR )
		return false;
	g_LeafAmbientIndexHDR.CopyArray( g_LeafAmbientIndexLDR.Base(), g_LeafAmbientIndexLDR.Count() );
	g_LeafAmbientLightingHDR.CopyArray( g_LeafAmbientLightingLDR.Base(), g_LeafAmbientLightingLDR.Count() );
	// ComputeLeafAmbientLighting marks small native lights as INAMBIENTCUBE.
	// Copy that completed native record, not the pre-gather exportLights.
	memcpy( dworldlightsHDR, dworldlightsLDR, numworldlightsLDR * sizeof( dworldlight_t ) );
	return true;
}
namespace
{
	// Ported from utils/vrad/macro_texture.cpp:17-30. The output module owns
	// this copy because the new baker does not link the legacy VRAD module.
	class CMacroTextureData
	{
	public:
		CMacroTextureData() : m_Width( 0 ), m_Height( 0 ) {}
		int m_Width;
		int m_Height;
		CUtlMemory<unsigned char> m_ImageData;
	};

	class CMacroTextureState
	{
	public:
		CMacroTextureState() : m_WorldMins( 0, 0, 0 ), m_WorldMaxs( 0, 0, 0 ) {}
		~CMacroTextureState() { Shutdown(); }

		void Shutdown()
		{
			for ( int i = 0; i < m_Lookup.Count(); ++i )
			{
				delete m_Lookup[i];
			}
			m_Lookup.RemoveAll();
			m_FaceTextures.Purge();
			m_Global = NULL;
		}

		// Ported from utils/vrad/macro_texture.cpp:43-79. Unlike the legacy
		// implementation, malformed optional macro files warn and are ignored so
		// they cannot leave a partially encoded BSP.
		CMacroTextureData *LoadFile( const char *pFilename )
		{
			if ( !g_pFileSystem )
				return NULL;

			FileHandle_t hFile = g_pFileSystem->Open( pFilename, "rb", TOOLS_READ_PATH_ID );
			if ( hFile == FILESYSTEM_INVALID_HANDLE )
				return NULL;

			int fileSize = g_pFileSystem->Size( hFile );
			if ( fileSize <= 0 )
			{
				g_pFileSystem->Close( hFile );
				return NULL;
			}

			CUtlVector<char> fileData;
			fileData.SetSize( fileSize );
			g_pFileSystem->Read( fileData.Base(), fileSize, hFile );
			g_pFileSystem->Close( hFile );

			CUtlBuffer buffer;
			buffer.Put( fileData.Base(), fileSize );
			IVTFTexture *pTexture = CreateVTFTexture();
			if ( !pTexture || !pTexture->Unserialize( buffer ) )
			{
				if ( pTexture )
					DestroyVTFTexture( pTexture );
				Warning( "ReSTIR: unable to read macro texture %s\n", pFilename );
				return NULL;
			}

			pTexture->ConvertImageFormat( IMAGE_FORMAT_RGBA8888, false );
			CMacroTextureData *pData = new CMacroTextureData;
			pData->m_Width = pTexture->Width();
			pData->m_Height = pTexture->Height();
			if ( pData->m_Width <= 0 || pData->m_Height <= 0 || !pTexture->ImageData() )
			{
				DestroyVTFTexture( pTexture );
				delete pData;
				return NULL;
			}
			int dataSize = pData->m_Width * pData->m_Height * 4;
			pData->m_ImageData.EnsureCapacity( dataSize );
			memcpy( pData->m_ImageData.Base(), pTexture->ImageData(), dataSize );
			DestroyVTFTexture( pTexture );
			return pData;
		}

		// Ported from utils/vrad/macro_texture.cpp:83-138. The scene builder
		// already records world bounds, so this uses ReSTIRScene::worldMins/
		// worldMaxs in place of the legacy worldspawn lookup.
		void Init( const char *pBSPFilename, const ReSTIRScene &scene, int faceCount )
		{
			Shutdown();
			m_WorldMins = scene.worldMins;
			m_WorldMaxs = scene.worldMaxs;
			m_FaceTextures.SetSize( faceCount );
			for ( int i = 0; i < m_FaceTextures.Count(); ++i )
				m_FaceTextures[i] = NULL;

			char mapName[512];
			char vtfFilename[512];
			Q_FileBase( pBSPFilename, mapName, sizeof( mapName ) );
			Q_snprintf( vtfFilename, sizeof( vtfFilename ), "materials/macro/%s/base.vtf", mapName );
			m_Global = LoadFile( vtfFilename );

			for ( int iFace = 0; iFace < m_FaceTextures.Count(); ++iFace )
			{
				if ( iFace >= g_FaceMacroTextureInfos.Count() )
					continue;
				unsigned short stringID = g_FaceMacroTextureInfos[iFace].m_MacroTextureNameID;
				if ( stringID == 0xFFFF || stringID >= (unsigned short)g_TexDataStringTable.Count() )
					continue;
				int stringOffset = g_TexDataStringTable[stringID];
				if ( stringOffset < 0 || stringOffset >= g_TexDataStringData.Count() )
					continue;

				Q_snprintf( vtfFilename, sizeof( vtfFilename ), "%smaterials/%s.vtf", gamedir, &g_TexDataStringData[stringOffset] );
				int lookup = m_Lookup.Find( vtfFilename );
				if ( m_Lookup.IsValidIndex( lookup ) )
				{
					m_FaceTextures[iFace] = m_Lookup[lookup];
				}
				else
				{
					CMacroTextureData *pData = LoadFile( vtfFilename );
					if ( pData )
					{
						m_Lookup.Insert( vtfFilename, pData );
						m_FaceTextures[iFace] = pData;
					}
				}
			}
		}

		// Ported from utils/vrad/macro_texture.cpp:141-163.
		Vector Sample( const CMacroTextureData *pTexture, const Vector &worldPos ) const
		{
			if ( !pTexture || pTexture->m_Width <= 0 || pTexture->m_Height <= 0 )
				return Vector( 1.0f, 1.0f, 1.0f );

			float xRange = m_WorldMaxs.x - m_WorldMins.x;
			float yRange = m_WorldMaxs.y - m_WorldMins.y;
			float u = xRange != 0.0f ? ( worldPos.x - m_WorldMins.x ) / xRange : 0.0f;
			float v = yRange != 0.0f ? ( worldPos.y - m_WorldMins.y ) / yRange : 0.0f;
			int ix = clamp( (int)( u * (float)( pTexture->m_Width - 0.00001f ) ), 0, pTexture->m_Width - 1 );
			int iy = clamp( (int)( v * (float)( pTexture->m_Height - 0.00001f ) ), 0, pTexture->m_Height - 1 );
			iy = pTexture->m_Height - 1 - iy;
			const unsigned char *pColor = &pTexture->m_ImageData[( iy * pTexture->m_Width + ix ) * 4];
			return Vector( pColor[0] / 255.0f, pColor[1] / 255.0f, pColor[2] / 255.0f );
		}

		void Apply( int face, const Vector &worldPos, Vector &color ) const
		{
			if ( m_Global )
				color *= Sample( m_Global, worldPos );
			if ( face >= 0 && face < m_FaceTextures.Count() && m_FaceTextures[face] )
				color *= Sample( m_FaceTextures[face], worldPos );
		}

	private:
		CUtlDict<CMacroTextureData *, int> m_Lookup;
		CUtlVector<CMacroTextureData *> m_FaceTextures;
		CMacroTextureData *m_Global = NULL;
		Vector m_WorldMins;
		Vector m_WorldMaxs;
	};

	static int SelectedFaceCount()
	{
		if ( g_bHDR && numfaces_hdr > 0 )
			return numfaces_hdr;
		return numfaces;
	}


	static void ClearFaceLighting( dface_t &face )
	{
		face.lightofs = -1;
		for ( int i = 0; i < MAXLIGHTMAPS; ++i )
			face.styles[i] = 255;
	}

	static int LuxelCount( const dface_t &face )
	{
		return ( face.m_LightmapTextureSizeInLuxels[0] + 1 ) * ( face.m_LightmapTextureSizeInLuxels[1] + 1 );
	}

	struct NativeFaceStyles
	{
		int count;
		int sourceSlots[MAXLIGHTMAPS];
	};

	// A style slot is kept only when some luxel of it survives ColorRGBExp32 encoding.
	// Pre-assigned slots (PVS-eligible lights that never reach the face) must drop
	// out as in VRAD; an exact-zero test would keep every slot after OIDN, whose
	// output is never exactly zero, and overflow MAX_MAP_LIGHTING.
	static bool HasRadiance( const ReSTIRLightmapResult &result, const ReSTIRGpuFace &face, int slot )
	{
		int numLuxels = face.luxelW * face.luxelH;
		int first = face.firstOutput + ( slot * face.numChannels ) * numLuxels;
		if ( first < 0 || first + face.numChannels * numLuxels > result.radiance.Count() )
			return false;
		for ( int channel = 0; channel < face.numChannels; ++channel )
		{
			for ( int luxel = 0; luxel < numLuxels; ++luxel )
			{
				const Vector &v = result.radiance[first + channel * numLuxels + luxel];
				ColorRGBExp32 encoded;
				VectorToColorRGBExp32( v, encoded );
				if ( encoded.r || encoded.g || encoded.b )
					return true;
			}
		}
		return false;
	}

	static Vector ClampLighting( const Vector &input, const Vector &minLight )
	{
		Vector output = input;
		for ( int i = 0; i < 3; ++i )
		{
			if ( !IsFinite( output[i] ) || output[i] < 0.0f )
				output[i] = 0.0f;
			if ( output[i] < minLight[i] )
				output[i] = minLight[i];
		}
		return output;
	}

	static float MedianUpper( CUtlVector<float> &values )
	{
		for ( int i = 1; i < values.Count(); ++i )
		{
			float value = values[i];
			int j = i - 1;
			while ( j >= 0 && values[j] > value )
			{
				values[j + 1] = values[j];
				--j;
			}
			values[j + 1] = value;
		}
		return values[values.Count() / 2];
	}

	static bool ValidateFaceArray( const dface_t *faces, int faceCount, const CUtlVector<byte> &lightData, const char *label )
	{
		if ( faceCount < 0 || faceCount > MAX_MAP_FACES )
		{
			Warning( "ReSTIR: %s face count %d is invalid\n", label, faceCount );
			return false;
		}
		if ( lightData.Count() < 0 || lightData.Count() > MAX_MAP_LIGHTING )
		{
			Warning( "ReSTIR: %s light data size %d exceeds MAX_MAP_LIGHTING\n", label, lightData.Count() );
			return false;
		}

		for ( int i = 0; i < faceCount; ++i )
		{
			const dface_t &face = faces[i];
			int styleCount = 0;
			bool unusedSeen = false;
			for ( int style = 0; style < MAXLIGHTMAPS; ++style )
			{
				if ( face.styles[style] == 255 )
				{
					unusedSeen = true;
					continue;
				}
				if ( unusedSeen )
				{
					Warning( "ReSTIR: %s face %d has a style after 255\n", label, i );
					return false;
				}
				++styleCount;
			}

			if ( styleCount > 0 && face.styles[0] != 0 )
			{
				Warning( "ReSTIR: %s face %d does not have style 0 in slot 0\n", label, i );
				return false;
			}
			if ( styleCount == 0 )
			{
				if ( face.lightofs != -1 )
				{
					Warning( "ReSTIR: %s face %d has no styles but lightofs %d\n", label, i, face.lightofs );
					return false;
				}
				continue;
			}

			if ( face.lightofs < styleCount * 4 || face.lightofs > lightData.Count() )
			{
				Warning( "ReSTIR: %s face %d has invalid lightofs %d\n", label, i, face.lightofs );
				return false;
			}
			int channels = ( face.texinfo >= 0 && face.texinfo < texinfo.Count() && ( texinfo[face.texinfo].flags & SURF_BUMPLIGHT ) ) ? NUM_BUMP_VECTS + 1 : 1;
			int luxels = LuxelCount( face );
			if ( luxels <= 0 || luxels > INT_MAX / ( styleCount * channels * 4 ) )
			{
				Warning( "ReSTIR: %s face %d has invalid luxel dimensions\n", label, i );
				return false;
			}
			int end = face.lightofs + luxels * styleCount * channels * 4;
			if ( end < face.lightofs || end > lightData.Count() )
			{
				Warning( "ReSTIR: %s face %d light range [%d,%d) is outside %d bytes\n", label, i, face.lightofs, end, lightData.Count() );
				return false;
			}
		}
		return true;
	}

	static bool ValidateAmbient( const CUtlVector<dleafambientindex_t> &indices, const CUtlVector<dleafambientlighting_t> &lighting, const char *label )
	{
		if ( indices.Count() > MAX_MAP_LEAFS )
		{
			Warning( "ReSTIR: %s ambient index count %d exceeds MAX_MAP_LEAFS\n", label, indices.Count() );
			return false;
		}
		if ( lighting.Count() < 0 )
			return false;
		for ( int i = 0; i < indices.Count(); ++i )
		{
			const dleafambientindex_t &index = indices[i];
			if ( index.ambientSampleCount == 0 )
			{
				if ( indices.Count() && index.firstAmbientSample >= (unsigned short)indices.Count() )
				{
					Warning( "ReSTIR: %s leaf %d fallback index %u is invalid\n", label, i, index.firstAmbientSample );
					return false;
				}
			}
			else if ( (int)index.firstAmbientSample + (int)index.ambientSampleCount > lighting.Count() )
			{
				Warning( "ReSTIR: %s leaf %d ambient range is invalid\n", label, i );
				return false;
			}
		}
		return true;
	}
}

namespace
{
	struct HighresColorContext
	{
		const ReSTIRScene *scene;
		const ReSTIRLightmapResult *result;
		CMacroTextureState *macro;
	};
	static Vector HighresColor(void *opaque, int faceIndex, int luxel, int plane, const Vector &input)
	{
		HighresColorContext &context = *static_cast<HighresColorContext *>(opaque);
		const ReSTIRScene &scene = *context.scene;
		const ReSTIRGpuFace &face = scene.faces[faceIndex];
		const int global = face.firstLuxel + luxel;
		const int validity = face.numSamples > 0 ? context.result->luxelValid[global] : 0;
		Vector rgb = validity ? input : Vector(0,0,0);
		for (int c = 0; c < 3; ++c) rgb[c] = MAX(rgb[c],scene.faceMinLight[faceIndex][c]);
		if (plane == 0 && validity == 1)
		{
			const float *p = scene.luxels[global].position;
			context.macro->Apply(face.dface,Vector(p[0],p[1],p[2]),rgb);
		}
		return rgb;
	}
	static bool ShadowStorageError()
	{
		Warning("ReSTIR: inconsistent selected-light export metadata\n");
		return false;
	}
}

bool CReSTIRBSPOutput::EncodeLightmaps( const ReSTIROptions &options, const ReSTIRScene &scene, const ReSTIRLightmapResult &result )
{
	// The independently sampled float output is captured before native style pruning.
	if ( !g_pFaces || !pdlightdata || !pNumworldlights || !dworldlights )
	{
		Warning( "ReSTIR: BSP lighting globals are not initialized\n" );
		return false;
	}
	if ( scene.faces.Count() != scene.faceMinLight.Count() )
	{
		Warning( "ReSTIR: scene face/minlight arrays are inconsistent\n" );
		return false;
	}
	if ( scene.dfaceToFace.Count() < SelectedFaceCount() )
	{
		Warning( "ReSTIR: scene dface mapping is smaller than the selected face array\n" );
		return false;
	}

	CMacroTextureState macroTextures;
	macroTextures.Init( options.mapPath.String(), scene, SelectedFaceCount() );
	HighresColorContext highres = { &scene, &result, &macroTextures };
	if (!ReSTIR_CaptureHighres(options,scene,result,HighresColor,&highres))
		return false;

	CUtlVector<NativeFaceStyles> nativeStyles;
	nativeStyles.SetCount( scene.faces.Count() );
	int64 totalBytes = 0;
	for ( int dfaceIndex = 0; dfaceIndex < SelectedFaceCount(); ++dfaceIndex )
	{
		// Ported from utils/vrad/lightmap.cpp:3089-3090.
		ClearFaceLighting( g_pFaces[dfaceIndex] );
		int sceneFaceIndex = scene.dfaceToFace[dfaceIndex];
		if ( sceneFaceIndex < 0 )
			continue;
		if ( sceneFaceIndex >= scene.faces.Count() )
		{
			Warning( "ReSTIR: dface %d maps to invalid scene face %d\n", dfaceIndex, sceneFaceIndex );
			return false;
		}
		const ReSTIRGpuFace &sceneFace = scene.faces[sceneFaceIndex];
		if ( sceneFace.dface != dfaceIndex || sceneFace.numStyles <= 0 || sceneFace.numStyles > RESTIR_MAX_FACE_STYLES ||
			sceneFace.styles[0] != 0 || ( sceneFace.numChannels != 1 && sceneFace.numChannels != 4 ) )
		{
			Warning( "ReSTIR: scene face %d has an invalid style/channel contract\n", sceneFaceIndex );
			return false;
		}

		// Ported from utils/vrad/lightmap.cpp:3366-3422.
		const int64 luxelCount = (int64)sceneFace.luxelW * sceneFace.luxelH;
		if ( sceneFace.luxelW <= 0 || sceneFace.luxelH <= 0 || luxelCount <= 0 || luxelCount > MAX_MAP_LIGHTING )
		{
			Warning( "ReSTIR: scene face %d has invalid luxel dimensions\n", sceneFaceIndex );
			return false;
		}
		const int luxels = (int)luxelCount;
		int64 outputValues = (int64)sceneFace.numStyles * sceneFace.numChannels * luxels;
		if ( sceneFace.firstOutput < 0 || outputValues > INT_MAX || (int64)sceneFace.firstOutput + outputValues > result.radiance.Count() )
		{
			Warning( "ReSTIR: scene face %d output range is outside the baked result\n", sceneFaceIndex );
			return false;
		}
		if ( sceneFace.firstLuxel < 0 || (int64)sceneFace.firstLuxel + luxels > scene.luxels.Count() )
		{
			Warning( "ReSTIR: scene face %d luxel range is outside the scene\n", sceneFaceIndex );
			return false;
		}

		// Selected-only styles remain in the transport source, not the receiver.
		// Enforce the receiver contract before RGBExp pruning can hide overflow.
		int receiverSlots[MAXLIGHTMAPS];
		int receiverCount = 0;
		for ( int slot = 0; slot < sceneFace.numStyles; ++slot )
		{
			if ( !scene.IsReceiverStyle( sceneFace.styles[slot] ) )
				continue;
			if ( receiverCount == MAXLIGHTMAPS )
			{
				Warning( "ReSTIR: scene face %d has more than %d receiver styles\n", sceneFaceIndex, MAXLIGHTMAPS );
				return false;
			}
			receiverSlots[receiverCount++] = slot;
		}
		NativeFaceStyles &styles = nativeStyles[sceneFaceIndex];
		styles.count = 0;
		for ( int receiverSlot = 0; receiverSlot < receiverCount; ++receiverSlot )
		{
			const int sourceSlot = receiverSlots[receiverSlot];
			if ( sourceSlot == 0 || HasRadiance( result, sceneFace, sourceSlot ) )
				styles.sourceSlots[styles.count++] = sourceSlot;
		}
		const int active = styles.count;
		totalBytes += (int64)active * 4 + (int64)active * sceneFace.numChannels * LuxelCount(g_pFaces[dfaceIndex]) * 4;
		if ( totalBytes > MAX_MAP_LIGHTING )
		{
			Warning( "ReSTIR: encoded light data exceeds MAX_MAP_LIGHTING\n" );
			return false;
		}
	}

	pdlightdata->SetSize( (int)totalBytes );
	int writeOffset = 0;
	for ( int dfaceIndex = 0; dfaceIndex < SelectedFaceCount(); ++dfaceIndex )
	{
		int sceneFaceIndex = scene.dfaceToFace[dfaceIndex];
		if ( sceneFaceIndex < 0 )
			continue;
		const ReSTIRGpuFace &sceneFace = scene.faces[sceneFaceIndex];
		const NativeFaceStyles &styles = nativeStyles[sceneFaceIndex];
		int active = styles.count;
		int luxels = LuxelCount(g_pFaces[dfaceIndex]);
		g_pFaces[dfaceIndex].lightofs = writeOffset + active * 4;
		writeOffset += active * 4 + active * sceneFace.numChannels * luxels * 4;
		int outStyle = 0;
		for ( ; outStyle < active; ++outStyle )
			g_pFaces[dfaceIndex].styles[outStyle] = (byte)sceneFace.styles[styles.sourceSlots[outStyle]];
		while ( outStyle < MAXLIGHTMAPS )
			g_pFaces[dfaceIndex].styles[outStyle++] = 255;
	}

	// Ported from utils/vrad/radial.cpp:642-882. Data is packed in VRAD's
	// (style, bump channel, luxel) order; average colors precede luxels in
	// reverse style order through dface_AvgLightColor's offset convention.
	for ( int sceneFaceIndex = 0; sceneFaceIndex < scene.faces.Count(); ++sceneFaceIndex )
	{
		const ReSTIRGpuFace &sceneFace = scene.faces[sceneFaceIndex];
		if ( sceneFace.dface < 0 || sceneFace.dface >= SelectedFaceCount() )
			return false;
		const dface_t &face = g_pFaces[sceneFace.dface];
		const NativeFaceStyles &styles = nativeStyles[sceneFaceIndex];
		int active = styles.count;
		int luxels = LuxelCount(face);
		const int highLuxels = sceneFace.luxelW * sceneFace.luxelH;
		const int density = options.shadowMaps ? options.highresDensity : 1;
		const int nativeWidth = face.m_LightmapTextureSizeInLuxels[0] + 1;
		int channels = sceneFace.numChannels;
		Vector minLight( 0, 0, 0 );
		if ( sceneFaceIndex < scene.faceMinLight.Count() )
			minLight = scene.faceMinLight[sceneFaceIndex];

		for ( int outStyle = 0; outStyle < active; ++outStyle )
		{
			const int sourceSlot = styles.sourceSlots[outStyle];

			CUtlVector<float> red;
			CUtlVector<float> green;
			CUtlVector<float> blue;
			for ( int channel = 0; channel < channels; ++channel )
			{
				for ( int luxel = 0; luxel < luxels; ++luxel )
				{
					const int highLuxel = (luxel / nativeWidth) * density * sceneFace.luxelW + (luxel % nativeWidth) * density;
					int sourceIndex = sceneFace.firstOutput + ( sourceSlot * channels + channel ) * highLuxels + highLuxel;
					int destIndex = face.lightofs + ( outStyle * channels + channel ) * luxels * 4 + luxel * 4;
					if ( sourceIndex < 0 || sourceIndex >= result.radiance.Count() || destIndex < 0 || destIndex + 4 > pdlightdata->Count() )
					{
						Warning( "ReSTIR: face %d output index is outside the result/light buffer\n", sceneFace.dface );
						return false;
					}

					ColorRGBExp32 &encoded = *(ColorRGBExp32 *)( pdlightdata->Base() + destIndex );
					int globalLuxel = sceneFace.firstLuxel + highLuxel;
					int validity = 0;
					if ( sceneFace.numSamples > 0 && globalLuxel >= 0 && globalLuxel < result.luxelValid.Count() && globalLuxel < scene.luxels.Count() )
						validity = result.luxelValid[globalLuxel];
					Vector light;
					if ( validity == 0 )
					{
						// radial.cpp:797-804: zero-sample face -> (255,0,0) regardless of bRed2Black.
						// radial.cpp:522-526: luxel without samples -> (0,0,0) (bRed2Black defaults to true,
						// vrad.cpp:60; -rederrors is not offered here); displacement luxels are left at
						// zero by CVRadDispMgr::SampleRadial (vraddisps.cpp:1058-1084). All pass the
						// minlight clamp and the encoder, and are excluded from the median
						// (baseSampleOk == false).
						light = ClampLighting( Vector( sceneFace.numSamples > 0 ? 0.0f : 255.0f, 0, 0 ), minLight );
						VectorToColorRGBExp32( light, encoded );
						continue;
					}

					light = ClampLighting( result.radiance[sourceIndex], minLight );
					// radial.cpp:822-832: macro textures and the median only see baseSampleOk luxels.
					if ( channel == 0 && validity == 1 )
					{
						Vector worldPosition( scene.luxels[globalLuxel].position[0], scene.luxels[globalLuxel].position[1], scene.luxels[globalLuxel].position[2] );
						macroTextures.Apply( sceneFace.dface, worldPosition, light );
						if ( !IsFinite( light.x ) || !IsFinite( light.y ) || !IsFinite( light.z ) )
							light.Init( 0, 0, 0 );
						for ( int c = 0; c < 3; ++c )
							if ( light[c] < 0.0f ) light[c] = 0.0f;
						red.AddToTail( light.x );
						green.AddToTail( light.y );
						blue.AddToTail( light.z );
					}
					VectorToColorRGBExp32( light, encoded );
				}
			}

			ColorRGBExp32 *average = (ColorRGBExp32 *)( pdlightdata->Base() + face.lightofs - ( outStyle + 1 ) * 4 );
			if ( red.Count() == 0 )
			{
				VectorToColorRGBExp32( Vector( 0, 0, 0 ), *average );
			}
			else
			{
				Vector median( MedianUpper( red ), MedianUpper( green ), MedianUpper( blue ) );
				VectorToColorRGBExp32( median, *average );
			}
		}

		bool hasSamples = false;
		for ( int sample = 0; sample < sceneFace.numSamples; ++sample )
		{
			hasSamples = true;
			break;
		}
		if ( !hasSamples )
			Msg( "no samples %d\n", sceneFace.dface );
	}

	// Ported from utils/vrad/lightmap.cpp:1626-1658.
	if ( scene.exportLights.Count() > MAX_MAP_WORLDLIGHTS )
	{
		Error( "too many lights %d / %d\n", scene.exportLights.Count(), MAX_MAP_WORLDLIGHTS );
		return false;
	}
	if ( scene.exportLightToGpuLight.Count() != scene.exportLights.Count() )
		return ShadowStorageError();
	int retained = 0, selected = 0;
	for ( int i = 0; i < scene.exportLights.Count(); ++i )
	{
		const int gpu = scene.exportLightToGpuLight[i];
		if ( gpu < 0 || gpu >= scene.lights.Count() )
			return ShadowStorageError();
		if ( options.shadowMaps && ( scene.lights[gpu].lightFlags & RESTIR_LIGHT_RUNTIME_DIRECT ) )
		{
			++selected;
			continue;
		}
		// No marker flags or engine hook: selected analytic lights exist only
		// in rshd; every unselected worldlight keeps its ordinary fields.
		dworldlights[retained++] = scene.exportLights[i];
	}
	if ( selected != ReSTIR_SelectedLightCount() )
		return ShadowStorageError();
	*pNumworldlights = retained;
	return ReSTIR_FinishSelectedLighting(options,selected);
}

bool CReSTIRBSPOutput::Validate( const ReSTIROptions &options )
{
	if ( !ReSTIR_WriteShadowMapSidecar( options ) )
		return false;
	if ( !pdlightdata || !g_pFaces || !pNumworldlights || !dworldlights )
	{
		Warning( "ReSTIR: cannot validate uninitialized BSP output globals\n" );
		return false;
	}
	if ( *pNumworldlights < 0 || *pNumworldlights > MAX_MAP_WORLDLIGHTS )
	{
		Warning( "ReSTIR: worldlight count %d exceeds MAX_MAP_WORLDLIGHTS\n", *pNumworldlights );
		return false;
	}
	if ( !ValidateFaceArray( g_pFaces, SelectedFaceCount(), *pdlightdata, g_bHDR ? "HDR" : "LDR" ) )
		return false;
	if ( dlightdataLDR.Count() > 0 && !ValidateFaceArray( dfaces, numfaces, dlightdataLDR, "LDR" ) )
		return false;
	if ( dlightdataHDR.Count() > 0 && numfaces_hdr > 0 && !ValidateFaceArray( dfaces_hdr, numfaces_hdr, dlightdataHDR, "HDR" ) )
		return false;
	if ( !ValidateAmbient( g_LeafAmbientIndexLDR, g_LeafAmbientLightingLDR, "LDR" ) ||
		 !ValidateAmbient( g_LeafAmbientIndexHDR, g_LeafAmbientLightingHDR, "HDR" ) )
		return false;

	int gameLumpBytes = 0;
	int gameLumpCount = 0;
	g_GameLumps.ComputeGameLumpSizeAndCount( gameLumpBytes, gameLumpCount );
	if ( gameLumpBytes < 0 || gameLumpCount < 0 )
	{
		Warning( "ReSTIR: invalid game-lump size/count\n" );
		return false;
	}
	for ( GameLumpHandle_t handle = g_GameLumps.FirstGameLump(); handle != g_GameLumps.InvalidGameLump(); handle = g_GameLumps.NextGameLump( handle ) )
	{
		int size = g_GameLumps.GameLumpSize( handle );
		if ( size < 0 || ( size > 0 && !g_GameLumps.GetGameLump( handle ) ) )
		{
			Warning( "ReSTIR: invalid game-lump payload\n" );
			return false;
		}
	}
	return true;
}

bool CReSTIRBSPOutput::Write( const ReSTIROptions &options )
{
	if ( !Validate( options ) )
		return false;

	const char *sourcePath = options.transactionPath.Length() ? options.transactionPath.String() : options.mapPath.String();
	if ( !sourcePath || !sourcePath[0] )
	{
		Warning( "ReSTIR: no BSP path was supplied\n" );
		return false;
	}
	char temporaryPath[MAX_PATH * 2];
	// options.mapPath already carries ".bsp" -> "<map>.bsp.restir.tmp" (plan 7.6).
	Q_snprintf( temporaryPath, sizeof( temporaryPath ), "%s.restir.tmp", sourcePath );
	DeleteFileA( temporaryPath );

	// WriteBSPFile consumes the current BSP globals, including GetPakFile(),
	// g_LevelFlags, and LUMP_FACES_HDR when numfaces_hdr is nonzero (the
	// requirements are visible in utils/common/bsplib.cpp:2631-2745).
	WriteBSPFile( temporaryPath );
	if ( !g_pFileSystem || !g_pFileSystem->FileExists( temporaryPath ) )
	{
		DeleteFileA( temporaryPath );
		Warning( "ReSTIR: WriteBSPFile did not create %s\n", temporaryPath );
		return false;
	}

	if ( !MoveFileExA( temporaryPath, sourcePath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
	{
		DWORD error = GetLastError();
		DeleteFileA( temporaryPath );
		Warning( "ReSTIR: atomic replace failed for %s (error %lu)\n", sourcePath, (unsigned long)error );
		return false;
	}

	int gameLumpBytes = 0;
	int gameLumpCount = 0;
	g_GameLumps.ComputeGameLumpSizeAndCount( gameLumpBytes, gameLumpCount );
	Msg( "ReSTIR: wrote %s (lighting=%d bytes, worldlights=%d, game-lumps=%d/%d bytes)\n",
		sourcePath, pdlightdata ? pdlightdata->Count() : 0, pNumworldlights ? *pNumworldlights : 0, gameLumpCount, gameLumpBytes );
	return true;
}
