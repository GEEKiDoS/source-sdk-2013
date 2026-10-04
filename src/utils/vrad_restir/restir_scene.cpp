//========= Copyright Valve Corporation, All rights reserved. ============//
// ReSTIR scene geometry and material extraction.
//
// Geometry follows the VRAD trace anchors in trace.cpp:492-653.  The scene
// builder owns only immutable host records; all lighting is evaluated later by
// the Vulkan backend.

#include "restir_scene_internal.h"
#include "vrad_restir.h"
#include "restir_staticprops.h"
#include "bsplib.h"
#include "cmdlib.h"
#include "filesystem_tools.h"
#include "filesystem.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include "tier1/KeyValues.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef SMOOTHING_GROUP_HARD_EDGE
#define SMOOTHING_GROUP_HARD_EDGE 0xff000000
#endif

static CUtlDict<int, unsigned short> s_MaterialNames;
static CUtlDict<int, unsigned short> s_CoverageTextureNames;
static CUtlDict<int, unsigned short> s_AlbedoTextureNames;
static CUtlVector<unsigned char> s_MaterialCoverageLoaded;
static CUtlVector<unsigned char> s_MaterialAlbedoLoaded;
static CUtlVector<Vector> s_AlbedoInverseAverage;			// per scene.textures index (albedo entries only)
static CUtlVector<entity_t *> s_ModelEntities;

struct ReSTIRCachedMaterialEmission
{
	ReSTIRMaterialEmission	emission;
	bool					emissive;
};
static CUtlDict<ReSTIRCachedMaterialEmission, unsigned short> s_MaterialEmissions;

// utils/vrad/vradstaticprops.cpp:649-684, 859-872 — top mip of a VTF decoded to RGBA8888.
static bool LoadVTFRGBA( const char *pBaseTexture, int &width, int &height,
	CUtlVector<unsigned char> &rgba, unsigned int &flags )
{
	if ( !pBaseTexture || !pBaseTexture[0] || !g_pFileSystem )
		return false;

	char path[1024];
	Q_snprintf( path, sizeof( path ), "materials/%s.vtf", pBaseTexture );
	Q_FixSlashes( path, CORRECT_PATH_SEPARATOR );

	FileHandle_t file = g_pFileSystem->Open( path, "rb" );
	if ( !file )
		return false;

	const int size = g_pFileSystem->Size( file );
	CUtlBuffer buffer( 0, size, 0 );
	buffer.EnsureCapacity( size );
	const int bytesRead = g_pFileSystem->Read( buffer.Base(), size, file );
	g_pFileSystem->Close( file );
	if ( bytesRead <= 0 )
		return false;

	buffer.SeekPut( CUtlBuffer::SEEK_HEAD, bytesRead );
	buffer.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );
	IVTFTexture *texture = CreateVTFTexture();
	if ( !texture )
		return false;
	if ( !texture->Unserialize( buffer ) )
	{
		DestroyVTFTexture( texture );
		return false;
	}

	width = texture->Width();
	height = texture->Height();
	flags = texture->Flags();
	if ( width <= 0 || height <= 0 )
	{
		DestroyVTFTexture( texture );
		return false;
	}

	rgba.SetCount( ImageLoader::GetMemRequired( width, height, 1, IMAGE_FORMAT_RGBA8888, false ) );
	const unsigned char *pixels = texture->ImageData( 0, 0, 0, 0, 0, 0 );
	const bool converted = ImageLoader::ConvertImageFormat( pixels, texture->Format(), rgba.Base(),
		IMAGE_FORMAT_RGBA8888, width, height, 0, 0 );
	DestroyVTFTexture( texture );
	return converted;
}

static bool LoadCoverageVTF( const char *pBaseTexture, ReSTIRSceneTexture &coverage )
{
	CUtlVector<unsigned char> rgba;
	unsigned int flags;
	if ( !LoadVTFRGBA( pBaseTexture, coverage.width, coverage.height, rgba, flags ) )
		return false;
	coverage.channels = 1;
	coverage.texels.SetCount( coverage.width * coverage.height );
	for ( int i = 0; i < coverage.texels.Count(); ++i )
		coverage.texels[i] = rgba[i * 4 + 3];
	return true;
}

// Per-texel albedo for bounce light. The texture is kept in gamma space (the GPU
// linearizes with the same 2.2 curve VBSP uses for reflectivity); `inverseAverage`
// is 1 / mean linear RGB so the material's reflectivity still sets the average.
static bool LoadAlbedoVTF( const char *pBaseTexture, ReSTIRSceneTexture &albedo, Vector &inverseAverage )
{
	CUtlVector<unsigned char> rgba;
	unsigned int flags;
	if ( !LoadVTFRGBA( pBaseTexture, albedo.width, albedo.height, rgba, flags ) )
		return false;
	albedo.channels = 4;
	albedo.texels.Swap( rgba );
	double sum[3] = { 0.0, 0.0, 0.0 };
	const int texelCount = albedo.width * albedo.height;
	for ( int i = 0; i < texelCount; ++i )
	{
		for ( int c = 0; c < 3; ++c )
			sum[c] += pow( albedo.texels[i * 4 + c] / 255.0, 2.2 );
	}
	for ( int c = 0; c < 3; ++c )
	{
		const double average = sum[c] / texelCount;
		inverseAverage[c] = average > 1.0e-4 ? (float)( 1.0 / average ) : 0.0f;
	}
	return true;
}

// utils/vrad/vradstaticprops.cpp:688-740 (FindOrLoadIfValid) — cached base-texture coverage.
static int LoadCoverageTexture( ReSTIRScene &scene, const char *pMaterialName, const char *pBaseTexture )
{
	if ( !pBaseTexture || !pBaseTexture[0] )
		return -1;
	const unsigned short cached = s_CoverageTextureNames.Find( pBaseTexture );
	if ( cached != s_CoverageTextureNames.InvalidIndex() )
		return s_CoverageTextureNames[cached];

	const int textureIndex = scene.textures.AddToTail();
	if ( !LoadCoverageVTF( pBaseTexture, scene.textures[textureIndex] ) )
	{
		scene.textures.Remove( textureIndex );
		s_CoverageTextureNames.Insert( pBaseTexture, -1 );
		Warning( "ReSTIR: couldn't load alpha texture for material %s\n", pMaterialName );
		return -1;
	}
	s_CoverageTextureNames.Insert( pBaseTexture, textureIndex );
	return textureIndex;
}

static int LoadAlbedoTexture( ReSTIRScene &scene, const char *pMaterialName, const char *pBaseTexture, Vector &inverseAverage )
{
	inverseAverage.Init();
	if ( !pBaseTexture || !pBaseTexture[0] )
		return -1;
	const unsigned short cached = s_AlbedoTextureNames.Find( pBaseTexture );
	if ( cached != s_AlbedoTextureNames.InvalidIndex() )
	{
		const int textureIndex = s_AlbedoTextureNames[cached];
		if ( textureIndex >= 0 )
			inverseAverage = s_AlbedoInverseAverage[textureIndex];
		return textureIndex;
	}

	const int textureIndex = scene.textures.AddToTail();
	if ( !LoadAlbedoVTF( pBaseTexture, scene.textures[textureIndex], inverseAverage ) )
	{
		scene.textures.Remove( textureIndex );
		s_AlbedoTextureNames.Insert( pBaseTexture, -1 );
		Warning( "ReSTIR: couldn't load base texture for material %s; using its reflectivity\n", pMaterialName );
		return -1;
	}
	s_AlbedoTextureNames.Insert( pBaseTexture, textureIndex );
	s_AlbedoInverseAverage.EnsureCount( textureIndex + 1 );
	s_AlbedoInverseAverage[textureIndex] = inverseAverage;
	return textureIndex;
}

enum ReSTIRMaterialKey
{
	RESTIR_MAT_BASETEXTURE,
	RESTIR_MAT_TRANSLUCENT,
	RESTIR_MAT_ALPHATEST,
	RESTIR_MAT_ADDITIVE,
	RESTIR_MAT_ALPHATESTREFERENCE,
	RESTIR_MAT_COLOR,
	RESTIR_MAT_COLOR2,
	RESTIR_MAT_SELFILLUM,
	RESTIR_MAT_SELFILLUMTINT,
	RESTIR_MAT_SELFILLUMMASK,
	RESTIR_MAT_NOCULL,
	RESTIR_MAT_HDRCOLORSCALE,
	RESTIR_MAT_KEY_COUNT
};

static const char *s_MaterialEmissionKeys[RESTIR_MAT_KEY_COUNT] =
{
	"$basetexture", "$translucent", "$alphatest", "$additive", "$alphatestreference",
	"$color", "$color2", "$selfillum", "$selfillumtint", "$selfillummask", "$nocull", "$hdrcolorscale"
};

struct ReSTIRParsedMaterial
{
	char	shader[MAX_PATH];
	char	values[RESTIR_MAT_KEY_COUNT][MAX_PATH];
	unsigned int keys;
	// Coverage retains VRAD's presence-only alpha test and first non-empty base texture,
	// even when a patch explicitly sets a transparency key to zero.
	char	coverageBaseTexture[MAX_PATH];
	bool	coverageAlphaTested;
};

static void ReadMaterialVMTKeys( KeyValues *pBlock, ReSTIRParsedMaterial &material )
{
	if ( pBlock->FindKey( "$translucent" ) || pBlock->FindKey( "$alphatest" ) )
		material.coverageAlphaTested = true;
	if ( !material.coverageBaseTexture[0] && pBlock->FindKey( "$basetexture" ) )
		Q_strncpy( material.coverageBaseTexture, pBlock->GetString( "$basetexture" ), sizeof( material.coverageBaseTexture ) );
	for ( int key = 0; key < RESTIR_MAT_KEY_COUNT; ++key )
	{
		if ( ( material.keys & ( 1u << key ) ) || !pBlock->FindKey( s_MaterialEmissionKeys[key] ) )
			continue;
		Q_strncpy( material.values[key], pBlock->GetString( s_MaterialEmissionKeys[key] ), sizeof( material.values[key] ) );
		material.keys |= 1u << key;
	}
}

// utils/vrad/vradstaticprops.cpp:700-737 — no material system is initialized; use KeyValues.
// VBSP cubemaps produce patch VMTs. Walk outer overrides before their includes, so the
// outermost replace/insert value wins and the final non-patch root supplies the shader.
static bool ReadParsedMaterialVMT( const char *pMaterialName, ReSTIRParsedMaterial &material )
{
	memset( &material, 0, sizeof( material ) );
	if ( !g_pFullFileSystem )
		return false;
	char vmtPath[MAX_PATH];
	Q_snprintf( vmtPath, sizeof( vmtPath ), "materials/%s.vmt", pMaterialName );
	Q_FixSlashes( vmtPath, CORRECT_PATH_SEPARATOR );

	bool loaded = false;
	for ( int depth = 0; depth < 4; ++depth )
	{
		KeyValues *pVMT = new KeyValues( "vmt" );
		CUtlBuffer buf( 0, 0, CUtlBuffer::TEXT_BUFFER );
		if ( !g_pFullFileSystem->ReadFile( vmtPath, NULL, buf ) || !pVMT->LoadFromBuffer( vmtPath, buf ) )
		{
			pVMT->deleteThis();
			break;
		}
		loaded = true;
		if ( Q_stricmp( pVMT->GetName(), "patch" ) == 0 )
		{
			for ( int block = 0; block < 2; ++block )
			{
				KeyValues *pBlock = pVMT->FindKey( block == 0 ? "replace" : "insert" );
				if ( pBlock )
					ReadMaterialVMTKeys( pBlock, material );
			}
			Q_strncpy( vmtPath, pVMT->GetString( "include", "" ), sizeof( vmtPath ) );
			Q_FixSlashes( vmtPath, CORRECT_PATH_SEPARATOR );
			pVMT->deleteThis();
			if ( !vmtPath[0] )
				break;
			continue;
		}
		Q_strncpy( material.shader, pVMT->GetName(), sizeof( material.shader ) );
		ReadMaterialVMTKeys( pVMT, material );
		pVMT->deleteThis();
		break;
	}
	return loaded;
}

static bool ReadMaterialVMT( const char *pMaterialName, char *pBaseTexture, int baseTextureSize, bool &alphaTested )
{
	ReSTIRParsedMaterial material;
	const bool loaded = ReadParsedMaterialVMT( pMaterialName, material );
	Q_strncpy( pBaseTexture, material.coverageBaseTexture, baseTextureSize );
	alphaTested = material.coverageAlphaTested;
	return loaded;
}

static float MaterialFloat( const ReSTIRParsedMaterial &material, ReSTIRMaterialKey key, float defaultValue )
{
	return material.keys & ( 1u << key ) ? (float)atof( material.values[key] ) : defaultValue;
}

static Vector MaterialColor( const ReSTIRParsedMaterial &material, ReSTIRMaterialKey key )
{
	Vector color( 1.0f, 1.0f, 1.0f );
	if ( !( material.keys & ( 1u << key ) ) )
		return color;
	const char *pValue = material.values[key];
	while ( *pValue == ' ' || *pValue == '\t' )
		++pValue;
	const bool byteColor = *pValue == '{';
	const bool vectorColor = byteColor || *pValue == '[';
	if ( vectorColor )
		++pValue;
	char *pEnd;
	for ( int channel = 0; channel < ( vectorColor ? 3 : 1 ); ++channel )
	{
		const float value = (float)strtod( pValue, &pEnd );
		if ( pEnd == pValue )
			return Vector( 1.0f, 1.0f, 1.0f );
		color[channel] = byteColor ? value / 255.0f : value;
		pValue = pEnd;
	}
	if ( !vectorColor )
		color.y = color.z = color.x;
	return color;
}

// materialsystem/BaseVSShader.cpp:652-672 — overbright colour components stay linear.
static Vector LinearMaterialColor( const Vector &color )
{
	Vector linear;
	for ( int channel = 0; channel < 3; ++channel )
		linear[channel] = color[channel] <= 1.0f ? GammaToLinear( color[channel] ) : color[channel];
	return linear;
}

bool ReSTIR_GetMaterialEmission( ReSTIRScene &scene, const ReSTIROptions &options, const char *pMaterialName,
	ReSTIRMaterialEmission &emission )
{
	emission.intensity.Init();
	emission.texture = -1;
	emission.twoSided = false;
	if ( options.emissiveScale <= 0.0f )
		return false;
	const char *name = pMaterialName ? pMaterialName : "";
	const unsigned short found = s_MaterialEmissions.Find( name );
	if ( found != s_MaterialEmissions.InvalidIndex() )
	{
		emission = s_MaterialEmissions[found].emission;
		return s_MaterialEmissions[found].emissive;
	}
	ReSTIRCachedMaterialEmission cached;
	cached.emission = emission;
	cached.emissive = false;
	const unsigned short cacheIndex = s_MaterialEmissions.Insert( name, cached );
	if ( !name[0] || !Q_strnicmp( name, "tools/", 6 ) || !Q_strnicmp( name, "tools\\", 6 ) )
		return false;

	ReSTIRParsedMaterial material;
	if ( !ReadParsedMaterialVMT( name, material ) || !material.shader[0] )
		return false;
	const bool unlit = Q_stricmp( material.shader, "UnlitGeneric" ) == 0;
	if ( !unlit && MaterialFloat( material, RESTIR_MAT_SELFILLUM, 0.0f ) == 0.0f )
		return false;
	const char *baseTexture = material.values[RESTIR_MAT_BASETEXTURE];
	if ( !Q_strnicmp( baseTexture, "_rt_", 4 ) )
		return false;

	Vector color = LinearMaterialColor( MaterialColor( material, RESTIR_MAT_COLOR ) );
	Vector color2 = LinearMaterialColor( MaterialColor( material, RESTIR_MAT_COLOR2 ) );
	Vector modulation( color.x * color2.x, color.y * color2.y, color.z * color2.z );
	if ( unlit )
	{
		if ( options.hdr )
			modulation *= MaterialFloat( material, RESTIR_MAT_HDRCOLORSCALE, 1.0f );
	}
	else
	{
		// materialsystem/stdshaders/vertexlitgeneric_dx9_helper.cpp:1214 — tint is already linear.
		const Vector tint = MaterialColor( material, RESTIR_MAT_SELFILLUMTINT );
		modulation.x *= tint.x;
		modulation.y *= tint.y;
		modulation.z *= tint.z;
	}
	if ( modulation.x <= 0.0f && modulation.y <= 0.0f && modulation.z <= 0.0f )
		return false;

	int width = 1, height = 1;
	unsigned int baseFlags = 0;
	CUtlVector<unsigned char> base;
	if ( material.keys & ( 1u << RESTIR_MAT_BASETEXTURE ) )
	{
		if ( !LoadVTFRGBA( baseTexture, width, height, base, baseFlags ) )
		{
			Warning( "ReSTIR: couldn't load emissive base texture for material %s\n", name );
			return false;
		}
	}
	const char *maskTexture = material.values[RESTIR_MAT_SELFILLUMMASK];
	// materialsystem/stdshaders/vertexlitgeneric_dx9_helper.cpp:289-300 — no alpha means no SELFILLUM.
	if ( !unlit && !maskTexture[0] && !( baseFlags & ( TEXTUREFLAGS_ONEBITALPHA | TEXTUREFLAGS_EIGHTBITALPHA ) ) )
		return false;
	int maskWidth = 1, maskHeight = 1;
	unsigned int maskFlags = 0;
	CUtlVector<unsigned char> mask;
	const bool hasMask = !unlit && maskTexture[0] &&
		LoadVTFRGBA( maskTexture, maskWidth, maskHeight, mask, maskFlags );
	// Without a base texture, retain the mask's spatial detail instead of reducing it to one texel.
	if ( base.Count() == 0 && hasMask )
	{
		width = maskWidth;
		height = maskHeight;
	}
	const bool translucent = MaterialFloat( material, RESTIR_MAT_TRANSLUCENT, 0.0f ) != 0.0f;
	const bool additive = MaterialFloat( material, RESTIR_MAT_ADDITIVE, 0.0f ) != 0.0f;
	const bool alphaTest = MaterialFloat( material, RESTIR_MAT_ALPHATEST, 0.0f ) != 0.0f;
	float alphaReference = MaterialFloat( material, RESTIR_MAT_ALPHATESTREFERENCE, 0.5f );
	if ( alphaReference <= 0.0f )
		alphaReference = 0.5f;

	CUtlVector<unsigned char> texels;
	texels.SetCount( width * height * 4 );
	bool uniformWhite = true;
	bool emitting = false;
	// materialsystem/stdshaders/vertexlit_and_unlit_generic_ps2x.fxc:365-445 — selfillum RGB mask
	// stays linear; base RGB follows VBSP's 2.2 curve. Unlit transparency scales displayed colour.
	for ( int y = 0; y < height; ++y )
	{
		for ( int x = 0; x < width; ++x )
		{
			const int offset = ( y * width + x ) * 4;
			const float alpha = base.Count() ? base[offset + 3] / 255.0f : 1.0f;
			float alphaFactor = unlit ? ( translucent && !additive ? alpha : 1.0f ) : alpha;
			if ( unlit && alphaTest && alpha < alphaReference )
				alphaFactor = 0.0f;
			int maskOffset = 0;
			if ( hasMask )
			{
				const int maskX = MIN( (int)( ( x + 0.5 ) * maskWidth / width ), maskWidth - 1 );
				const int maskY = MIN( (int)( ( y + 0.5 ) * maskHeight / height ), maskHeight - 1 );
				maskOffset = ( maskY * maskWidth + maskX ) * 4;
			}
			for ( int channel = 0; channel < 3; ++channel )
			{
				const float baseLinear = base.Count() ? powf( base[offset + channel] / 255.0f, 2.2f ) : 1.0f;
				const float factor = baseLinear * ( hasMask ? mask[maskOffset + channel] / 255.0f : alphaFactor );
				uniformWhite = uniformWhite && factor == 1.0f;
				texels[offset + channel] = (unsigned char)( 255.0f * powf( factor, 1.0f / 2.2f ) + 0.5f );
				emitting = emitting || ( texels[offset + channel] != 0 && modulation[channel] > 0.0f );
			}
			texels[offset + 3] = 255;
		}
	}
	if ( !emitting )
		return false;
	emission.intensity = modulation * ( options.emissiveScale * 255.0f / (float)M_PI );
	emission.twoSided = MaterialFloat( material, RESTIR_MAT_NOCULL, 0.0f ) != 0.0f;
	if ( !uniformWhite )
	{
		emission.texture = scene.textures.AddToTail();
		ReSTIRSceneTexture &texture = scene.textures[emission.texture];
		texture.width = width;
		texture.height = height;
		texture.channels = 4;
		texture.texels.Swap( texels );
	}
	s_MaterialEmissions[cacheIndex].emission = emission;
	s_MaterialEmissions[cacheIndex].emissive = true;
	return true;
}

Vector ReSTIR_EmissionTriangleMean( const ReSTIRScene &scene, int texture, const float uv[6] )
{
	if ( texture < 0 )
		return Vector( 1.0f, 1.0f, 1.0f );
	const ReSTIRSceneTexture &image = scene.textures[texture];
	Vector mean( 0.0f, 0.0f, 0.0f );
	// 8x8 fixed strata in a square, mapped uniformly onto the triangle by the square-root warp.
	for ( int y = 0; y < 8; ++y )
	{
		for ( int x = 0; x < 8; ++x )
		{
			const float root = sqrtf( ( x + 0.5f ) / 8.0f );
			const float b0 = 1.0f - root;
			const float b1 = root * ( 1.0f - ( y + 0.5f ) / 8.0f );
			const float b2 = 1.0f - b0 - b1;
			const float u = b0 * uv[0] + b1 * uv[2] + b2 * uv[4];
			const float v = b0 * uv[1] + b1 * uv[3] + b2 * uv[5];
			const int tx = MIN( (int)( ( u - floorf( u ) ) * image.width ), image.width - 1 );
			const int ty = MIN( (int)( ( v - floorf( v ) ) * image.height ), image.height - 1 );
			const int offset = ( ty * image.width + tx ) * image.channels;
			for ( int channel = 0; channel < 3; ++channel )
				mean[channel] += powf( image.texels[offset + channel] / 255.0f, 2.2f );
		}
	}
	return mean * ( 1.0f / 64.0f );
}

// utils/vrad/vradstaticprops.cpp:688-740 — material registry shared with props.
int ReSTIR_GetOrAddMaterial( ReSTIRScene &scene, const char *pMaterialName,
	const Vector &reflectivity, bool textureShadows, bool *pOutAlphaTested )
{
	const char *name = pMaterialName ? pMaterialName : "";
	const unsigned short cached = s_MaterialNames.Find( name );
	int materialIndex;
	if ( cached != s_MaterialNames.InvalidIndex() )
	{
		materialIndex = s_MaterialNames[cached];
	}
	else
	{
		ReSTIRGpuMaterial material;
		memset( &material, 0, sizeof( material ) );
		ReSTIR_SceneSet4( material.reflectivity, reflectivity );
		material.coverageTexture = -1;
		material.albedoTexture = -1;
		materialIndex = scene.materials.AddToTail( material );
		s_MaterialNames.Insert( name, materialIndex );
		s_MaterialCoverageLoaded.AddToTail( false );
		s_MaterialAlbedoLoaded.AddToTail( false );
	}
	if ( textureShadows && !s_MaterialCoverageLoaded[materialIndex] )
	{
		s_MaterialCoverageLoaded[materialIndex] = true;
		char baseTexture[MAX_PATH];
		bool alphaTested = false;
		if ( name[0] && ReadMaterialVMT( name, baseTexture, sizeof( baseTexture ), alphaTested ) && alphaTested )
			scene.materials[materialIndex].coverageTexture = LoadCoverageTexture( scene, name, baseTexture );
	}
	if ( pOutAlphaTested )
		*pOutAlphaTested = scene.materials[materialIndex].coverageTexture >= 0;
	return materialIndex;
}

// Per-texel bounce albedo for a lit brush/displacement material (-restir_texturealbedo).
// Loads $basetexture once per material and records the texdata size the brush
// texture axes are expressed in. Materials without a loadable base texture keep
// their single reflectivity (albedoTexture == -1).
void ReSTIR_LoadMaterialAlbedo( ReSTIRScene &scene, int materialIndex, const char *pMaterialName, int textureWidth, int textureHeight )
{
	ReSTIRGpuMaterial &material = scene.materials[materialIndex];
	material.textureWidth = MAX( textureWidth, 1 );
	material.textureHeight = MAX( textureHeight, 1 );
	if ( s_MaterialAlbedoLoaded[materialIndex] )
		return;
	s_MaterialAlbedoLoaded[materialIndex] = true;
	char baseTexture[MAX_PATH];
	bool alphaTested = false;
	if ( !pMaterialName || !pMaterialName[0] || !ReadMaterialVMT( pMaterialName, baseTexture, sizeof( baseTexture ), alphaTested ) )
		return;
	Vector inverseAverage;
	material.albedoTexture = LoadAlbedoTexture( scene, pMaterialName, baseTexture, inverseAverage );
	if ( material.albedoTexture < 0 )
		return;
	const Vector reflectivity = ReSTIR_SceneV4( material.reflectivity );
	ReSTIR_SceneSet4( material.albedoScale, Vector( reflectivity.x * inverseAverage.x,
		reflectivity.y * inverseAverage.y, reflectivity.z * inverseAverage.z ) );
}

// utils/vrad/vrad.cpp:2223-2236 — selected HDR/LDR face array.
int ReSTIR_SceneFaceCount()
{
	return g_pFaces == dfaces_hdr ? numfaces_hdr : numfaces;
}

// utils/vrad/lightmap.cpp:123-139 — EdgeVertex.
int ReSTIR_SceneFaceVertex( const dface_t *pFace, int edge )
{
	if ( edge < 0 )
		edge += pFace->numedges;
	if ( edge >= pFace->numedges )
		edge %= pFace->numedges;
	const int surfedge = dsurfedges[pFace->firstedge + edge];
	return surfedge < 0 ? dedges[-surfedge].v[1] : dedges[surfedge].v[0];
}


// utils/vrad/vrad.cpp:1811-1856 — face_entity assignment, indexed once per model.
entity_t *ReSTIR_SceneEntityForModel( int model )
{
	return model >= 0 && model < s_ModelEntities.Count() ? s_ModelEntities[model] : NULL;
}

// utils/vrad/vrad.cpp:703-717 — model entity origin copied to face_offset.
void ReSTIR_SceneFaceOrigin( int model, Vector &origin )
{
	origin.Init();
	entity_t *entity = ReSTIR_SceneEntityForModel( model );
	if ( entity )
		GetVectorForKey( entity, "origin", origin );
}

const char *ReSTIR_SceneFaceMaterial( const dface_t *pFace )
{
	if ( pFace->texinfo < 0 || pFace->texinfo >= texinfo.Count() )
		return "";
	const texinfo_t &info = texinfo[pFace->texinfo];
	if ( info.texdata < 0 || info.texdata >= numtexdata )
		return "";
	return TexDataStringTable_GetString( dtexdata[info.texdata].nameStringTableID );
}

// utils/common/polylib.cpp:171-190 — WindingBounds without a temporary winding.
void ReSTIR_SceneFaceBounds( const dface_t *pFace, const Vector &origin, Vector &mins, Vector &maxs )
{
	ClearBounds( mins, maxs );
	for ( int edge = 0; edge < pFace->numedges; ++edge )
		AddPointToBounds( dvertexes[ReSTIR_SceneFaceVertex( pFace, edge )].point + origin, mins, maxs );
}

// utils/common/polylib.cpp:154-166 — WindingArea triangle fan.
float ReSTIR_SceneFaceArea( const dface_t *pFace, const Vector &origin )
{
	if ( pFace->numedges < 3 )
		return 0.0f;

	const Vector p0 = dvertexes[ReSTIR_SceneFaceVertex( pFace, 0 )].point + origin;
	float area = 0.0f;
	for ( int edge = 2; edge < pFace->numedges; ++edge )
	{
		const Vector p1 = dvertexes[ReSTIR_SceneFaceVertex( pFace, edge - 1 )].point + origin;
		const Vector p2 = dvertexes[ReSTIR_SceneFaceVertex( pFace, edge )].point + origin;
		area += 0.5f * VectorLength( CrossProduct( p1 - p0, p2 - p0 ) );
	}
	return area;
}

// utils/common/polylib.cpp:197-209; utils/vrad/vrad.cpp:619-620 — WindingCenter.
Vector ReSTIR_SceneFaceCentroid( const dface_t *pFace, const Vector &origin )
{
	Vector centroid( 0, 0, 0 );
	if ( pFace->numedges <= 0 )
		return centroid;
	for ( int edge = 0; edge < pFace->numedges; ++edge )
		centroid += dvertexes[ReSTIR_SceneFaceVertex( pFace, edge )].point + origin;
	centroid *= 1.0f / pFace->numedges;
	return centroid;
}

// utils/vrad/lightmap.cpp:205, 2125 — BSP face plane, not winding cross product.
Vector ReSTIR_SceneFaceNormal( const dface_t *pFace, const Vector &origin )
{
	(void)origin;
	return dplanes[pFace->planenum].normal;
}

void ReSTIR_SceneSet4( float out[4], const Vector &value, float w )
{
	out[0] = value.x;
	out[1] = value.y;
	out[2] = value.z;
	out[3] = w;
}

Vector ReSTIR_SceneV4( const float value[4] )
{
	return Vector( value[0], value[1], value[2] );
}

float ReSTIR_SceneSafeLength( const Vector &value )
{
	const float length = VectorLength( value );
	return length > 1.0e-20f ? length : 1.0f;
}

void ReSTIR_ScenePointUV( const texinfo_t &texinfoValue, const dtexdata_t &texdata,
	const Vector &point, float &u, float &v )
{
	const Vector s( texinfoValue.textureVecsTexelsPerWorldUnits[0][0],
		texinfoValue.textureVecsTexelsPerWorldUnits[0][1],
		texinfoValue.textureVecsTexelsPerWorldUnits[0][2] );
	const Vector t( texinfoValue.textureVecsTexelsPerWorldUnits[1][0],
		texinfoValue.textureVecsTexelsPerWorldUnits[1][1],
		texinfoValue.textureVecsTexelsPerWorldUnits[1][2] );
	u = ( DotProduct( point, s ) + texinfoValue.textureVecsTexelsPerWorldUnits[0][3] ) /
		( texdata.width > 0 ? texdata.width : 1 );
	v = ( DotProduct( point, t ) + texinfoValue.textureVecsTexelsPerWorldUnits[1][3] ) /
		( texdata.height > 0 ? texdata.height : 1 );
}

void ReSTIR_SceneAddTriangle( ReSTIRScene &scene, const Vector &v0, const Vector &v1, const Vector &v2,
	int material, unsigned int hitId, unsigned int flags, int face, const float uv[6],
	bool forceWinding, const Vector &planeNormal )
{
	Vector a = v0;
	Vector b = v1;
	Vector c = v2;
	const bool swapped = forceWinding && DotProduct( CrossProduct( b - a, c - a ), planeNormal ) < 0.0f;
	if ( swapped )
	{
		Vector temp = b;
		b = c;
		c = temp;
	}

	ReSTIRGpuTriangle triangle;
	memset( &triangle, 0, sizeof( triangle ) );
	ReSTIR_SceneSet4( triangle.v0, a, uv ? uv[0] : 0.0f );
	ReSTIR_SceneSet4( triangle.v1, b, uv ? uv[1] : 0.0f );
	ReSTIR_SceneSet4( triangle.v2, c, uv ? uv[swapped ? 4 : 2] : 0.0f );
	triangle.uv[0] = uv ? uv[swapped ? 5 : 3] : 0.0f;
	triangle.uv[1] = uv ? uv[swapped ? 2 : 4] : 0.0f;
	triangle.uv[2] = uv ? uv[swapped ? 3 : 5] : 0.0f;
	triangle.hitId = hitId;
	triangle.material = material;
	triangle.flags = flags;
	triangle.face = face;
	scene.triangles.AddToTail( triangle );
}

void ReSTIR_SceneAddWindingTriangles( ReSTIRScene &scene, winding_t *pWinding, int texinfoIndex,
	int material, unsigned int hitId, unsigned int flags, int face, const VMatrix &transform,
	bool forceWinding, const Vector &planeNormal )
{
	if ( !pWinding || pWinding->numpoints < 3 )
		return;

	const texinfo_t &info = texinfo[texinfoIndex];
	const dtexdata_t &texdata = dtexdata[info.texdata];
	for ( int point = 2; point < pWinding->numpoints; ++point )
	{
		const Vector local[3] = { pWinding->p[0], pWinding->p[point - 1], pWinding->p[point] };
		Vector world[3];
		float uv[6];
		for ( int corner = 0; corner < 3; ++corner )
		{
			world[corner] = transform.VMul4x3( local[corner] );
			float u, v;
			ReSTIR_ScenePointUV( info, texdata, local[corner], u, v );
			uv[corner * 2 + 0] = u;
			uv[corner * 2 + 1] = v;
		}
		ReSTIR_SceneAddTriangle( scene, world[0], world[1], world[2], material,
			hitId, flags, face, uv, forceWinding, planeNormal );
	}
}


// utils/vrad/radial.cpp:27-35 — LuxelSpaceToWorld (s/t supplied in absolute luxels).
Vector ReSTIR_SceneLuxelToWorld( const Vector &luxelOrigin, const Vector luxelToWorld[2], float s, float t )
{
	return luxelOrigin + luxelToWorld[0] * s + luxelToWorld[1] * t;
}

// utils/vrad/trace.cpp:435-472 — PointLeafnum_r then leaf cluster.
int ReSTIR_SceneClusterFromPoint( const Vector &point )
{
	if ( numnodes <= 0 )
		return -1;
	int node = 0;
	while ( node >= 0 )
	{
		const dnode_t &current = dnodes[node];
		const dplane_t &plane = dplanes[current.planenum];
		const float distance = plane.type < 3 ? point[plane.type] - plane.dist : DotProduct( plane.normal, point ) - plane.dist;
		node = current.children[distance < 0.0f ? 1 : 0];
	}
	const int leaf = -1 - node;
	return leaf >= 0 && leaf < numleafs ? dleafs[leaf].cluster : -1;
}

// utils/vrad/vraddisps.cpp:349-421 — DispBuilderInit.
static void InitDisplacement( int faceIndex, CCoreDispInfo &disp )
{
	const dface_t &face = g_pFaces[faceIndex];
	const ddispinfo_t &dispInfo = g_dispinfo[face.dispinfo];
	CCoreDispSurface *surface = disp.GetSurface();
	surface->SetPointCount( 4 );
	surface->SetHandle( faceIndex );
	surface->SetContents( dispInfo.contents );
	for ( int corner = 0; corner < 4; ++corner )
		surface->SetPoint( corner, dvertexes[ReSTIR_SceneFaceVertex( &face, corner )].point );
	Vector normal;
	surface->GetNormal( normal );
	for ( int corner = 0; corner < 4; ++corner )
		surface->SetPointNormal( corner, normal );
	surface->SetPointStart( dispInfo.startPosition );
	surface->FindSurfPointStartIndex();
	surface->AdjustSurfPointData();

	const texinfo_t &info = texinfo[face.texinfo];
	const Vector u( info.lightmapVecsLuxelsPerWorldUnits[0][0],
		info.lightmapVecsLuxelsPerWorldUnits[0][1], info.lightmapVecsLuxelsPerWorldUnits[0][2] );
	const Vector v( info.lightmapVecsLuxelsPerWorldUnits[1][0],
		info.lightmapVecsLuxelsPerWorldUnits[1][1], info.lightmapVecsLuxelsPerWorldUnits[1][2] );
	const int luxelsPerWorldUnit = static_cast<int>( 1.0f / VectorLength( u ) );
	surface->CalcLuxelCoords( luxelsPerWorldUnit, false, u, v );
	disp.SetNeighborData( dispInfo.m_EdgeNeighbors, dispInfo.m_CornerNeighbors );
	disp.InitDispInfo( dispInfo.power, dispInfo.minTess, dispInfo.smoothingAngle,
		&g_DispVerts[dispInfo.m_iDispVertStart], &g_DispTris[dispInfo.m_iDispTriStart] );
}

// utils/vrad/disp_vrad.cpp:21-46 — corner lookup, including the 0.1-world-unit match.
static int FindDisplacementCorner( CCoreDispInfo *disp, const Vector &point )
{
	int closest = 0;
	float distance = 1.0e24f;
	for ( int corner = 0; corner < 4; ++corner )
	{
		const int vertex = disp->VertIndexToInt( disp->GetCornerPointIndex( corner ) );
		const float candidate = disp->GetVert( vertex ).DistTo( point );
		if ( candidate < distance )
		{
			closest = corner;
			distance = candidate;
		}
	}
	return distance <= 0.1f ? closest : -1;
}

// utils/vrad/disp_vrad.cpp:49-78 — corner neighbors then both edge sub-neighbors.
static int GetDisplacementNeighbors( const CCoreDispInfo *disp, int (&neighbors)[512] )
{
	int count = 0;
	for ( int corner = 0; corner < 4; ++corner )
	{
		const CDispCornerNeighbors *list = disp->GetCornerNeighbors( corner );
		for ( int index = 0; index < list->m_nNeighbors; ++index )
		{
			if ( count < ARRAYSIZE( neighbors ) )
				neighbors[count++] = list->m_Neighbors[index];
		}
	}
	for ( int edge = 0; edge < 4; ++edge )
	{
		const CDispNeighbor *list = disp->GetEdgeNeighbor( edge );
		for ( int sub = 0; sub < 2; ++sub )
		{
			if ( list->m_SubNeighbors[sub].IsValid() && count < ARRAYSIZE( neighbors ) )
				neighbors[count++] = list->m_SubNeighbors[sub].GetNeighborIndex();
		}
	}
	return count;
}

// utils/vrad/disp_vrad.cpp:156-204 — blend T-junction midpoint and both corners.
static void BlendDisplacementTJunctions( CCoreDispInfo **list, int count )
{
	for ( int index = 0; index < count; ++index )
	{
		CCoreDispInfo *disp = list[index];
		for ( int edge = 0; edge < 4; ++edge )
		{
			CDispNeighbor *neighbors = disp->GetEdgeNeighbor( edge );
			const CVertIndex midpoint = disp->GetEdgeMidPoint( edge );
			const int vertex = disp->VertIndexToInt( midpoint );
			if ( !neighbors->m_SubNeighbors[0].IsValid() || !neighbors->m_SubNeighbors[1].IsValid() )
				continue;
			CCoreDispInfo *first = list[neighbors->m_SubNeighbors[0].GetNeighborIndex()];
			CCoreDispInfo *second = list[neighbors->m_SubNeighbors[1].GetNeighborIndex()];
			const int firstCorner = FindDisplacementCorner( first, disp->GetVert( vertex ) );
			const int secondCorner = FindDisplacementCorner( second, disp->GetVert( vertex ) );
			if ( firstCorner == -1 || secondCorner == -1 )
				continue;
			const CVertIndex firstVertex = first->GetCornerPointIndex( firstCorner );
			const CVertIndex secondVertex = second->GetCornerPointIndex( secondCorner );
			Vector normal = disp->GetNormal( vertex ) + first->GetNormal( firstVertex ) +
				second->GetNormal( secondVertex );
			VectorNormalize( normal );
			disp->SetNormal( vertex, normal );
			first->SetNormal( firstVertex, normal );
			second->SetNormal( secondVertex, normal );
		}
	}
}

// utils/vrad/disp_vrad.cpp:81-153 — merge coincident corner normals in encounter order.
static void BlendDisplacementCorners( CCoreDispInfo **list, int count )
{
	CUtlVector<int> cornerVertices;
	for ( int index = 0; index < count; ++index )
	{
		CCoreDispInfo *disp = list[index];
		int neighbors[512];
		const int neighborCount = GetDisplacementNeighbors( disp, neighbors );
		cornerVertices.SetCount( neighborCount );
		for ( int corner = 0; corner < 4; ++corner )
		{
			const int vertex = disp->VertIndexToInt( disp->GetCornerPointIndex( corner ) );
			const Vector &point = disp->GetVert( vertex );
			Vector normal = disp->GetNormal( vertex );
			for ( int neighbor = 0; neighbor < neighborCount; ++neighbor )
			{
				CCoreDispInfo *other = list[neighbors[neighbor]];
				const int otherCorner = FindDisplacementCorner( other, point );
				if ( otherCorner == -1 )
				{
					cornerVertices[neighbor] = -1;
					continue;
				}
				const int otherVertex = other->VertIndexToInt( other->GetCornerPointIndex( otherCorner ) );
				cornerVertices[neighbor] = otherVertex;
				normal += other->GetNormal( otherVertex );
			}
			VectorNormalize( normal );
			disp->SetNormal( vertex, normal );
			for ( int neighbor = 0; neighbor < neighborCount; ++neighbor )
			{
				if ( cornerVertices[neighbor] != -1 )
					list[neighbors[neighbor]]->SetNormal( cornerVertices[neighbor], normal );
			}
		}
	}
}

// utils/vrad/disp_vrad.cpp:207-276 — shared and in-between edge normal interpolation.
static void BlendDisplacementEdges( CCoreDispInfo **list, int count )
{
	for ( int index = 0; index < count; ++index )
	{
		CCoreDispInfo *disp = list[index];
		for ( int edge = 0; edge < 4; ++edge )
		{
			CDispNeighbor *neighbors = disp->GetEdgeNeighbor( edge );
			for ( int sub = 0; sub < 2; ++sub )
			{
				CDispSubNeighbor *neighbor = &neighbors->m_SubNeighbors[sub];
				if ( !neighbor->IsValid() )
					continue;
				CCoreDispInfo *other = list[neighbor->GetNeighborIndex()];
				const int edgeDimension = g_EdgeDims[edge];
				CDispSubEdgeIterator iterator;
				iterator.Start( disp, edge, sub, true );
				iterator.Next();
				CVertIndex previous = iterator.GetVertIndex();
				while ( iterator.Next() )
				{
					if ( !iterator.IsLastVert() )
					{
						Vector normal = disp->GetNormal( iterator.GetVertIndex() ) +
							other->GetNormal( iterator.GetNBVertIndex() );
						VectorNormalize( normal );
						disp->SetNormal( iterator.GetVertIndex(), normal );
						other->SetNormal( iterator.GetNBVertIndex(), normal );
					}
					const int start = previous[!edgeDimension];
					const int end = iterator.GetVertIndex()[!edgeDimension];
					for ( int between = start + 1; between < end; ++between )
					{
						const float fraction = RemapVal( between, start, end, 0, 1 );
						Vector normal;
						VectorLerp( disp->GetNormal( previous ), disp->GetNormal( iterator.GetVertIndex() ),
							fraction, normal );
						VectorNormalize( normal );
						CVertIndex vertex;
						vertex[edgeDimension] = iterator.GetVertIndex()[edgeDimension];
						vertex[!edgeDimension] = between;
						disp->SetNormal( vertex, normal );
					}
					previous = iterator.GetVertIndex();
				}
			}
		}
	}
}

// utils/vrad/vraddisps.cpp:426-469 — build all surfaces before edge smoothing.
static bool BuildDisplacements( ReSTIRSceneBuildContext &context )
{
	context.displacements.SetCount( g_dispinfo.Count() );
	for ( int index = 0; index < context.displacements.Count(); ++index )
	{
		context.displacements[index] = new CCoreDispInfo;
		context.displacements[index]->SetListIndex( index );
	}
	for ( int index = 0; index < context.displacements.Count(); ++index )
		context.displacements[index]->SetDispUtilsHelperInfo( context.displacements.Base(),
			context.displacements.Count() );
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		if ( face.dispinfo >= 0 && face.dispinfo < context.displacements.Count() )
			InitDisplacement( faceIndex, *context.displacements[face.dispinfo] );
	}
	for ( int index = 0; index < context.displacements.Count(); ++index )
	{
		if ( !context.displacements[index]->Create() )
		{
			Warning( "ReSTIR: failed to build displacement %d\n", index );
			return false;
		}
	}
	// utils/vrad/disp_vrad.cpp:317-329 — smoothing pass order is significant.
	BlendDisplacementTJunctions( context.displacements.Base(), context.displacements.Count() );
	BlendDisplacementCorners( context.displacements.Base(), context.displacements.Count() );
	BlendDisplacementEdges( context.displacements.Base(), context.displacements.Count() );
	return true;
}

// utils/vrad/trace.cpp:535-559 — leaf brush union, with an indexed membership set.
static void CollectBrushes_r( int node, CUtlVector<int> &brushes, CUtlVector<unsigned char> &seen )
{
	if ( node < 0 )
	{
		const int leaf = -1 - node;
		for ( int i = 0; i < dleafs[leaf].numleafbrushes; ++i )
		{
			const int brush = dleafbrushes[dleafs[leaf].firstleafbrush + i];
			if ( !seen[brush] )
			{
				seen[brush] = true;
				brushes.AddToTail( brush );
			}
		}
		return;
	}
	CollectBrushes_r( dnodes[node].children[0], brushes, seen );
	CollectBrushes_r( dnodes[node].children[1], brushes, seen );
}

// VRAD trace.cpp:492-532 and 595-653, including per-side alpha coverage.
static void AddBrushModelGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	int model, const VMatrix &transform )
{
	if ( model < 0 || model >= nummodels )
		return;

	CUtlVector<int> brushes;
	CUtlVector<unsigned char> seen;
	seen.SetCount( numbrushes );
	memset( seen.Base(), 0, seen.Count() );
	CollectBrushes_r( dmodels[model].headnode, brushes, seen );
	for ( int brushIndex = 0; brushIndex < brushes.Count(); ++brushIndex )
	{
		const dbrush_t &brush = dbrushes[brushes[brushIndex]];
		if ( !( brush.contents & MASK_OPAQUE ) )
			continue;

		for ( int sideIndex = 0; sideIndex < brush.numsides; ++sideIndex )
		{
			const dbrushside_t &side = dbrushsides[brush.firstside + sideIndex];
			if ( side.texinfo < 0 || side.texinfo >= texinfo.Count() || side.dispinfo != 0 )
				continue;
			const texinfo_t &info = texinfo[side.texinfo];
			if ( info.texdata < 0 || info.texdata >= numtexdata )
				continue;
			if ( info.flags & SURF_SKY )
				continue;

			winding_t *winding = BaseWindingForPlane( dplanes[side.planenum].normal, dplanes[side.planenum].dist );
			for ( int otherIndex = 0; otherIndex < brush.numsides && winding; ++otherIndex )
			{
				if ( otherIndex == sideIndex )
					continue;
				const dbrushside_t &other = dbrushsides[brush.firstside + otherIndex];
				if ( other.bevel )
					continue;
				ChopWindingInPlace( &winding, dplanes[other.planenum ^ 1].normal,
					dplanes[other.planenum ^ 1].dist, 0.0f );
			}
			if ( !winding )
				continue;

			const dtexdata_t &texdata = dtexdata[info.texdata];
			const char *materialName = TexDataStringTable_GetString( texdata.nameStringTableID );
			bool alphaTested = false;
			const int material = ReSTIR_GetOrAddMaterial( scene, materialName, texdata.reflectivity,
				context.options->textureShadows, &alphaTested );
			ReSTIR_SceneAddWindingTriangles( scene, winding, side.texinfo, material,
				RESTIR_TRACE_ID_OPAQUE, RESTIR_TRI_SHADOW | ( alphaTested ? RESTIR_TRI_NONOPAQUE : 0 ),
				-1, transform, false, vec3_origin );
			FreeWinding( winding );
		}
	}
}

// utils/vrad/trace.cpp:612-652 — world-face gather triangles; other faces emit only.
static void AddFaceGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene, int faceIndex )
{
	const dface_t &face = g_pFaces[faceIndex];
	if ( face.texinfo < 0 || face.numedges < 3 )
		return;
	const texinfo_t &info = texinfo[face.texinfo];
	const dtexdata_t &texdata = dtexdata[info.texdata];
	bool alphaTested = false;
	const int material = ReSTIR_GetOrAddMaterial( scene, ReSTIR_SceneFaceMaterial( &face ),
		texdata.reflectivity, context.options->textureShadows, &alphaTested );
	winding_t *winding = AllocWinding( face.numedges );
	winding->numpoints = face.numedges;
	for ( int edge = 0; edge < face.numedges; ++edge )
		winding->p[edge] = dvertexes[ReSTIR_SceneFaceVertex( &face, edge )].point;
	context.faceTriFirst[faceIndex] = scene.triangles.Count();
	VMatrix identity;
	identity.SetupMatrixOrgAngles( context.faceOrigins[faceIndex], QAngle( 0, 0, 0 ) );
	const unsigned int flags = context.faceModels[faceIndex] == 0 && !( info.flags & SURF_NOLIGHT ) ?
		RESTIR_TRI_WORLDFACE : 0;
	ReSTIR_SceneAddWindingTriangles( scene, winding, face.texinfo, material,
		RESTIR_TRACE_ID_OPAQUE, flags | ( alphaTested ? RESTIR_TRI_NONOPAQUE : 0 ),
		faceIndex, identity, true, dplanes[face.planenum].normal );
	context.faceTriCount[faceIndex] = scene.triangles.Count() - context.faceTriFirst[faceIndex];
	FreeWinding( winding );
}

// utils/vrad/trace.cpp:612-652 — sky dface triangle fan and TRACE_ID_SKY.
static void AddSkyFaceGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene, int faceIndex )
{
	const dface_t &face = g_pFaces[faceIndex];
	if ( face.texinfo < 0 || face.numedges < 3 )
		return;
	const texinfo_t &info = texinfo[face.texinfo];
	const dtexdata_t &texdata = dtexdata[info.texdata];
	bool alphaTested = false;
	const int material = ReSTIR_GetOrAddMaterial( scene, ReSTIR_SceneFaceMaterial( &face ),
		texdata.reflectivity, context.options->textureShadows, &alphaTested );
	winding_t *winding = AllocWinding( face.numedges );
	winding->numpoints = face.numedges;
	for ( int edge = 0; edge < face.numedges; ++edge )
		winding->p[edge] = dvertexes[ReSTIR_SceneFaceVertex( &face, edge )].point;
	VMatrix identity;
	identity.Identity();
	context.faceTriFirst[faceIndex] = scene.triangles.Count();
	ReSTIR_SceneAddWindingTriangles( scene, winding, face.texinfo, material,
		RESTIR_TRACE_ID_SKY, RESTIR_TRI_SHADOW | RESTIR_TRI_SKY | RESTIR_TRI_WORLDFACE |
			( alphaTested ? RESTIR_TRI_NONOPAQUE : 0 ), faceIndex, identity, true,
		ReSTIR_SceneFaceNormal( &face, vec3_origin ) );
	context.faceTriCount[faceIndex] = scene.triangles.Count() - context.faceTriFirst[faceIndex];
	FreeWinding( winding );
}

// utils/vrad/vrad_dispcoll.cpp:1064-1080 — indexed displacement ray geometry.
static void AddDisplacementGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene, int faceIndex )
{
	const dface_t &face = g_pFaces[faceIndex];
	const texinfo_t &info = texinfo[face.texinfo];
	const dtexdata_t &texdata = dtexdata[info.texdata];
	CCoreDispInfo &displacement = *context.displacements[face.dispinfo];
	const int material = ReSTIR_GetOrAddMaterial( scene, ReSTIR_SceneFaceMaterial( &face ),
		texdata.reflectivity, false, NULL );
	// Displacement UVs serve lightmap lookup; VRAD displacement shadows are opaque.
	unsigned int flags = 0;
	if ( g_dispinfo[face.dispinfo].contents & MASK_OPAQUE )
		flags |= RESTIR_TRI_SHADOW;
	if ( !( info.flags & SURF_NOLIGHT ) )
		flags |= RESTIR_TRI_WORLDFACE;
	context.faceTriFirst[faceIndex] = scene.triangles.Count();
	const float inverseWidth = 1.0f / ( displacement.GetWidth() - 1 );
	const float inverseHeight = 1.0f / ( displacement.GetHeight() - 1 );
	for ( int tri = 0; tri < displacement.GetTriCount(); ++tri )
	{
		unsigned short indices[3];
		displacement.GetTriIndices( tri, indices[0], indices[2], indices[1] );
		float uv[6];
		for ( int corner = 0; corner < 3; ++corner )
		{
			uv[corner * 2] = ( indices[corner] % displacement.GetWidth() ) * inverseWidth;
			uv[corner * 2 + 1] = ( indices[corner] / displacement.GetWidth() ) * inverseHeight;
		}
		// CCoreDisp triangles are clockwise; reverse vertices and their UVs together.
		ReSTIR_SceneAddTriangle( scene, displacement.GetVert( indices[0] ),
			displacement.GetVert( indices[1] ), displacement.GetVert( indices[2] ),
			material, RESTIR_TRACE_ID_OPAQUE, flags, faceIndex, uv, false, vec3_origin );
	}
	context.faceTriCount[faceIndex] = scene.triangles.Count() - context.faceTriFirst[faceIndex];
}

// utils/vrad/trace.cpp:578-653 — brush-entity shadow casters and world geometry.
bool ReSTIR_SceneBuildGeometry( ReSTIRSceneBuildContext &context, ReSTIRScene &scene )
{
	// trace.cpp:492-532, 536-593 — world brushes first, then marked bmodels.
	VMatrix identity;
	identity.Identity();
	AddBrushModelGeometry( context, scene, 0, identity );
	for ( int entityIndex = 0; entityIndex < num_entities; ++entityIndex )
	{
		entity_t &entity = entities[entityIndex];
		if ( IntForKey( &entity, "vrad_brush_cast_shadows" ) == 0 )
			continue;
		const char *modelName = ValueForKey( &entity, "model" );
		if ( Q_strlen( modelName ) <= 1 )
			continue;
		const int model = atoi( modelName + 1 );
		if ( model <= 0 || model >= nummodels )
			continue;
		Vector origin;
		QAngle angles;
		GetVectorForKey( &entity, "origin", origin );
		GetAnglesForKey( &entity, "angles", angles );
		VMatrix transform;
		transform.SetupMatrixOrgAngles( origin, angles );
		AddBrushModelGeometry( context, scene, model, transform );
	}

	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		const dface_t &face = g_pFaces[faceIndex];
		if ( face.texinfo < 0 || face.dispinfo >= 0 )
			continue;
		if ( context.faceModels[faceIndex] == 0 && ( texinfo[face.texinfo].flags & SURF_SKY ) )
			AddSkyFaceGeometry( context, scene, faceIndex );
		else
			AddFaceGeometry( context, scene, faceIndex );
	}
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		if ( g_pFaces[faceIndex].dispinfo >= 0 )
			AddDisplacementGeometry( context, scene, faceIndex );
	}

	if ( !g_ReSTIRStaticPropMgr.AppendTriangles( scene, context.options->textureShadows ) )
		return false;

	if ( scene.triangles.Count() > 0 )
	{
		Vector mins( FLT_MAX, FLT_MAX, FLT_MAX );
		Vector maxs( -FLT_MAX, -FLT_MAX, -FLT_MAX );
		for ( int i = 0; i < scene.triangles.Count(); ++i )
		{
			AddPointToBounds( ReSTIR_SceneV4( scene.triangles[i].v0 ), mins, maxs );
			AddPointToBounds( ReSTIR_SceneV4( scene.triangles[i].v1 ), mins, maxs );
			AddPointToBounds( ReSTIR_SceneV4( scene.triangles[i].v2 ), mins, maxs );
		}
		scene.worldMins = mins;
		scene.worldMaxs = maxs;
	}
	return true;
}

// utils/vrad/vrad.cpp:667-736,2223-2279 — model/face records and geometry ordering.
bool CReSTIRSceneBuilder::Build( const ReSTIROptions &options, ReSTIRScene &scene )
{
	scene = ReSTIRScene();
	s_MaterialNames.RemoveAll();
	s_CoverageTextureNames.RemoveAll();
	s_AlbedoTextureNames.RemoveAll();
	s_MaterialCoverageLoaded.RemoveAll();
	s_MaterialAlbedoLoaded.RemoveAll();
	s_AlbedoInverseAverage.RemoveAll();
	s_MaterialEmissions.RemoveAll();
	s_ModelEntities.SetCount( nummodels );
	for ( int model = 0; model < nummodels; ++model )
		s_ModelEntities[model] = num_entities > 0 ? &entities[0] : NULL;
	for ( int entityIndex = num_entities - 1; entityIndex >= 0; --entityIndex )
	{
		const char *name = ValueForKey( &entities[entityIndex], "model" );
		if ( name[0] != '*' )
			continue;
		const int model = atoi( name + 1 );
		if ( model < 0 || model >= nummodels )
			continue;
		char expected[32];
		Q_snprintf( expected, sizeof( expected ), "*%d", model );
		if ( !strcmp( name, expected ) )
			s_ModelEntities[model] = &entities[entityIndex];
	}
	for ( int i = 0; i < g_NonShadowCastingMaterialStrings.Count(); ++i )
		free( const_cast<char *>( g_NonShadowCastingMaterialStrings[i] ) );
	g_NonShadowCastingMaterialStrings.RemoveAll();

	ReSTIR_ScenePrepareLightFiles( options );
	ReSTIRSceneBuildContext context;
	context.options = &options;
	context.faceCount = ReSTIR_SceneFaceCount();
	context.faceTriFirst.SetCount( context.faceCount );
	context.faceTriCount.SetCount( context.faceCount );
	context.faceOrigins.SetCount( context.faceCount );
	context.faceModels.SetCount( context.faceCount );
	context.faceNormals.SetCount( context.faceCount );
	context.faceClusters.SetCount( context.faceCount );
	context.faceClusterLists.SetCount( context.faceCount );
	for ( int model = 0; model < nummodels; ++model )
	{
		Vector origin;
		ReSTIR_SceneFaceOrigin( model, origin );
		const dmodel_t &brushModel = dmodels[model];
		for ( int faceIndex = brushModel.firstface;
			faceIndex < brushModel.firstface + brushModel.numfaces; ++faceIndex )
		{
			context.faceModels[faceIndex] = model;
			context.faceOrigins[faceIndex] = origin;
		}
	}
	for ( int faceIndex = 0; faceIndex < context.faceCount; ++faceIndex )
	{
		context.faceTriFirst[faceIndex] = -1;
		context.faceTriCount[faceIndex] = 0;
		context.faceNormals[faceIndex] = ReSTIR_SceneFaceNormal( &g_pFaces[faceIndex], context.faceOrigins[faceIndex] );
		context.faceClusters[faceIndex] = ReSTIR_SceneClusterFromPoint( ReSTIR_SceneFaceCentroid( &g_pFaces[faceIndex], context.faceOrigins[faceIndex] ) );
		context.faceClusterLists[faceIndex] = new CUtlVector<int>;
	}
	for ( int leaf = 0; leaf < numleafs; ++leaf )
	{
		const int cluster = dleafs[leaf].cluster;
		for ( int i = 0; i < dleafs[leaf].numleaffaces; ++i )
		{
			const int faceIndex = dleaffaces[dleafs[leaf].firstleafface + i];
			if ( faceIndex < 0 || faceIndex >= context.faceCount )
				continue;
			if ( context.faceClusterLists[faceIndex]->Find( cluster ) < 0 )
				context.faceClusterLists[faceIndex]->AddToTail( cluster );
		}
	}

	if ( !BuildDisplacements( context ) )
		return false;
	if ( !ReSTIR_SceneBuildGeometry( context, scene ) )
		return false;
	ReSTIR_SceneBuildFaceNeighbors( context, scene );
	if ( !ReSTIR_SceneSaveVertexNormals( context ) )
		return false;
	if ( !ReSTIR_SceneBuildLights( context, scene ) )
		return false;
	return ReSTIR_SceneBuildSamples( context, scene );
}
