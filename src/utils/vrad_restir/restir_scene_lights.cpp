//========= Copyright Valve Corporation, All rights reserved. ============//
// Direct lights are rebuilt from texlights, entities and emissive materials, never the input
// lump. The temporary subdivision records exist only to export VRAD leaf emitters; GPU surface
// lights sample a separate front-facing emitter triangle stream (ReSTIRScene::emitterTriangles).

#include "restir_scene_internal.h"
#include "vrad_restir.h"
#include "restir_staticprops.h"
#include "map_utils.h"
#include "cmdlib.h"
#include "filesystem_tools.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

static const float s_flLightScale = 1.0f;
static const float s_flDirectScale = 100.0f * 100.0f;
static const float s_flDLightThreshold = 0.1f;
static const float s_flMinChop = 4.0f;
static const float s_flMaxChop = 4.0f;
static const float s_flDispChop = 4.0f;

struct ReSTIRTexLight
{
	char name[256];
	Vector value;
	CUtlString source;
};

struct ReSTIRSurfacePatch
{
	winding_t *winding;
	Vector origin;
	Vector normal;
	Vector mins;
	Vector maxs;
	Vector faceMins;
	Vector faceMaxs;
	Vector baseLight;
	float area;
	float baseArea;
	float scale[2];
	float luxScale;
	float chop;
	int face;
	int child[2];
	int indices[3];
	int gpuLight;
	bool sky;
};

static CUtlVector<ReSTIRTexLight> s_TexLights;
static CUtlString s_LevelName;

// utils/vrad/lightmap.cpp:1056-1118 — lightscale is VRAD's default 1.
static bool LightForString( const char *pLight, Vector &intensity )
{
	double r = 0.0;
	double g = 0.0;
	double b = 0.0;
	double scaler = 0.0;
	double rHDR, gHDR, bHDR, scalerHDR;
	intensity.Init();
	int argumentCount = sscanf( pLight, "%lf %lf %lf %lf %lf %lf %lf %lf",
		&r, &g, &b, &scaler, &rHDR, &gHDR, &bHDR, &scalerHDR );
	if ( argumentCount == 8 )
	{
		if ( g_bHDR )
		{
			r = rHDR;
			g = gHDR;
			b = bHDR;
			scaler = scalerHDR;
		}
		argumentCount = 4;
	}
	if ( r < 0.0 || g < 0.0 || b < 0.0 || scaler < 0.0 )
	{
		intensity.Init();
		return false;
	}
	intensity[0] = pow( r / 255.0, 2.2 ) * 255;
	switch ( argumentCount )
	{
	case 1:
		intensity[1] = intensity[2] = intensity[0];
		break;
	case 3:
	case 4:
		intensity[1] = pow( g / 255.0, 2.2 ) * 255;
		intensity[2] = pow( b / 255.0, 2.2 ) * 255;
		if ( argumentCount == 4 )
		{
			VectorScale( intensity, scaler / 255.0, intensity );
		}
		break;
	default:
		printf( "unknown light specifier type - %s\n", pLight );
		return false;
	}
	VectorScale( intensity, s_flLightScale, intensity );
	return true;
}

// utils/vrad/vrad.cpp:190-292. Bounded scans replace unsafe %s only.
static bool ReadLightFile( const char *pFilename )
{
	FileHandle_t file = g_pFileSystem->Open( pFilename, "r" );
	if ( !file )
	{
		Warning( "Warning: Couldn't open texlight file %s.\n", pFilename );
		return true;
	}
	int fileTexlights = 0;
	char buffer[1024];
	Msg( "[Reading texlights from '%s']\n", pFilename );
	while ( CmdLib_FGets( buffer, sizeof( buffer ), file ) )
	{
		char *scan = buffer;
		if ( !Q_strnicmp( "hdr:", scan, 4 ) )
		{
			scan += 4;
			if ( !g_bHDR )
			{
				continue;
			}
		}
		if ( !Q_strnicmp( "ldr:", scan, 4 ) )
		{
			scan += 4;
			if ( g_bHDR )
			{
				continue;
			}
		}
		scan += strspn( scan, " \t" );
		char directive[1024];
		if ( sscanf( scan, "noshadow %1023s", directive ) == 1 )
		{
			char *dot = strchr( directive, '.' );
			if ( dot )
			{
				*dot = 0;
			}
			g_NonShadowCastingMaterialStrings.AddToTail( strdup( directive ) );
		}
		else if ( sscanf( scan, "forcetextureshadow %1023s", directive ) == 1 )
		{
			ForceTextureShadowsOnModel( directive );
		}
		else
		{
			if ( s_TexLights.Count() == 128 )
			{
				Warning( "Too many texlights, max = %d\n", 128 );
				g_pFileSystem->Close( file );
				return false;
			}
			char name[256];
			if ( sscanf( scan, "%255s ", name ) != 1 )
			{
				if ( strlen( scan ) > 4 )
				{
					Msg( "ignoring bad texlight '%s' in %s", scan, pFilename );
				}
				continue;
			}
			Vector value;
			// VRAD deliberately retains LightForString's output on parse failure.
			const size_t offset = strlen( name ) + 1;
			LightForString( scan + MIN( offset, strlen( scan ) ), value );
			int lightIndex;
			for ( lightIndex = 0; lightIndex < s_TexLights.Count(); ++lightIndex )
			{
				ReSTIRTexLight &texLight = s_TexLights[lightIndex];
				if ( strcmp( texLight.name, name ) == 0 )
				{
					if ( strcmp( texLight.source.String(), pFilename ) == 0 )
					{
						Msg( "ERROR\a: Duplication of '%s' in file '%s'!\n", texLight.name, texLight.source.String() );
					}
					else if ( texLight.value[0] != value[0] || texLight.value[1] != value[1] || texLight.value[2] != value[2] )
					{
						Warning( "Warning: Overriding '%s' from '%s' with '%s'!\n", texLight.name, texLight.source.String(), pFilename );
					}
					else
					{
						Warning( "Warning: Redundant '%s' def in '%s' AND '%s'!\n", texLight.name, texLight.source.String(), pFilename );
					}
					break;
				}
			}
			if ( lightIndex == s_TexLights.Count() )
			{
				s_TexLights.AddToTail();
			}
			Q_strncpy( s_TexLights[lightIndex].name, name, sizeof( s_TexLights[lightIndex].name ) );
			s_TexLights[lightIndex].value = value;
			s_TexLights[lightIndex].source = pFilename;
			++fileTexlights;
		}
	}
	qprintf( "[%i texlights parsed from '%s']\n\n", fileTexlights, pFilename );
	g_pFileSystem->Close( file );
	return true;
}

// utils/vrad/vrad.cpp:300-350 — strip maps/<level>/ and three cubemap suffixes.
static void LightForTexture( const char *pName, Vector &result )
{
	result.Init();
	char baseFilename[MAX_PATH];
	if ( Q_strncmp( "maps/", pName, 5 ) == 0 )
	{
		if ( Q_strncmp( s_LevelName.String(), pName + 5, s_LevelName.Length() ) == 0 )
		{
			const char *base = pName + 5 + s_LevelName.Length();
			if ( *base == '/' )
			{
				++base;
				Q_strncpy( baseFilename, base, sizeof( baseFilename ) );
				bool foundSeparators = true;
				for ( int i = 0; i < 3; ++i )
				{
					char *underscore = Q_strrchr( baseFilename, '_' );
					if ( underscore && *underscore )
					{
						*underscore = '\0';
					}
					else
					{
						foundSeparators = false;
					}
				}
				if ( foundSeparators )
				{
					pName = baseFilename;
				}
			}
		}
	}
	for ( int i = 0; i < s_TexLights.Count(); ++i )
	{
		if ( !Q_stricmp( pName, s_TexLights[i].name ) )
		{
			result = s_TexLights[i].value;
			return;
		}
	}
}

static bool s_bLightFilesValid = true;

// utils/vrad/vrad.cpp:2144,2166-2187. Options already contains the .bsp extension.
void ReSTIR_ScenePrepareLightFiles( const ReSTIROptions &options )
{
	s_TexLights.RemoveAll();
	char levelName[MAX_PATH];
	Q_FileBase( options.mapPath.String(), levelName, sizeof( levelName ) );
	s_LevelName = levelName;
	char globalLights[MAX_PATH];
	Q_strncpy( globalLights, "lights.rad", sizeof( globalLights ) );
	if ( !g_pFileSystem->FileExists( globalLights ) )
	{
		Msg( "Could not find lights.rad in %s.\nTrying VRAD BIN directory instead...\n", globalLights );
#ifdef _WIN32
		GetModuleFileName( NULL, globalLights, sizeof( globalLights ) );
		Q_ExtractFilePath( globalLights, globalLights, sizeof( globalLights ) );
		Q_strncat( globalLights, "lights.rad", sizeof( globalLights ) );
#endif
	}
	char levelLights[MAX_PATH];
	Q_StripExtension( options.mapPath.String(), levelLights, sizeof( levelLights ) );
	Q_DefaultExtension( levelLights, ".rad", sizeof( levelLights ) );
	if ( !g_pFileSystem->FileExists( levelLights ) )
	{
		levelLights[0] = 0;
	}
	s_bLightFilesValid = ReadLightFile( globalLights );
	if ( s_bLightFilesValid && options.lightsFile.Length() )
	{
		s_bLightFilesValid = ReadLightFile( options.lightsFile.String() );
	}
	if ( s_bLightFilesValid && levelLights[0] )
	{
		s_bLightFilesValid = ReadLightFile( levelLights );
	}
}

// utils/vrad/lightmap.cpp:941-954 — target names are case sensitive.
static entity_t *FindTargetEntity( const char *pTarget )
{
	for ( int i = 0; i < num_entities; ++i )
	{
		if ( !strcmp( ValueForKey( &entities[i], "targetname" ), pTarget ) )
		{
			return &entities[i];
		}
	}
	return NULL;
}

// utils/vrad/lightmap.cpp:1047-1054.
static bool LightForKey( entity_t *pEntity, const char *pKey, Vector &intensity )
{
	return LightForString( ValueForKey( pEntity, const_cast<char *>( pKey ) ), intensity );
}

// utils/vrad/lightmap.cpp:968-988. Appending followed by one reversal emulates head insertion.
static ReSTIRGpuLight AllocLight( const Vector &origin )
{
	ReSTIRGpuLight light;
	memset( &light, 0, sizeof( light ) );
	ReSTIR_SceneSet4( light.origin, origin );
	light.firstTri = -1;
	light.emissionTexture = -1;
	return light;
}

struct ReSTIREmitterWeight
{
	int triangle;
	double area;
	double power;
};

static float EmitterTriangleArea( const ReSTIRGpuEmitterTriangle &triangle, Vector &areaNormal )
{
	CrossProduct( ReSTIR_SceneV4( triangle.v1 ) - ReSTIR_SceneV4( triangle.v0 ),
		ReSTIR_SceneV4( triangle.v2 ) - ReSTIR_SceneV4( triangle.v0 ), areaNormal );
	areaNormal *= 0.5f;
	return areaNormal.Length();
}

int ReSTIR_AddSurfaceEmitter( ReSTIRScene &scene, const ReSTIRGpuEmitterTriangle *pTriangles, int count,
	const Vector &intensity, int texture, int lightFlags )
{
	CUtlVector<ReSTIREmitterWeight> weights;
	weights.EnsureCapacity( count );
	double totalArea = 0.0, totalPower = 0.0;
	Vector centroid( 0, 0, 0 ), normal( 0, 0, 0 );
	for ( int i = 0; i < count; ++i )
	{
		const ReSTIRGpuEmitterTriangle &triangle = pTriangles[i];
		Vector areaNormal;
		const float area = EmitterTriangleArea( triangle, areaNormal );
		if ( area <= 0.0f )
		{
			continue;
		}
		const float uv[6] = { triangle.v0[3], triangle.v1[3], triangle.v2[3],
			triangle.uv[0], triangle.uv[1], triangle.uv[2] };
		const Vector mean = ReSTIR_EmissionTriangleMean( scene, texture, uv );
		ReSTIREmitterWeight weight;
		weight.triangle = i;
		weight.area = area;
		weight.power = area * ( 0.2126 * intensity.x * mean.x +
			0.7152 * intensity.y * mean.y + 0.0722 * intensity.z * mean.z );
		weights.AddToTail( weight );
		totalArea += weight.area;
		totalPower += weight.power;
		centroid += ( ReSTIR_SceneV4( triangle.v0 ) + ReSTIR_SceneV4( triangle.v1 ) +
			ReSTIR_SceneV4( triangle.v2 ) ) * ( area / 3.0f );
		normal += areaNormal;
	}
	if ( totalArea <= 0.0 || totalPower <= 0.0 )
	{
		return -1;
	}
	centroid *= (float)( 1.0 / totalArea );
	VectorNormalize( normal );
	ReSTIRGpuLight light = AllocLight( centroid );
	light.type = emit_surface;
	light.firstTri = scene.emitterTriangles.Count();
	light.numTris = weights.Count();
	light.emissionTexture = texture;
	light.lightFlags = lightFlags;
	ReSTIR_SceneSet4( light.normal, normal );
	ReSTIR_SceneSet4( light.intensity, intensity, (float)totalPower );
	scene.emitterTriangles.EnsureCapacity( light.firstTri + light.numTris );
	double cumulative = 0.0;
	float radiusSquared = 0.0f;
	for ( int i = 0; i < weights.Count(); ++i )
	{
		const ReSTIREmitterWeight &weight = weights[i];
		ReSTIRGpuEmitterTriangle triangle = pTriangles[weight.triangle];
		cumulative += 0.9 * weight.power / totalPower + 0.1 * weight.area / totalArea;
		triangle.uv[3] = i == weights.Count() - 1 ? 1.0f : (float)cumulative;
		scene.emitterTriangles.AddToTail( triangle );
		radiusSquared = MAX( radiusSquared, ( ReSTIR_SceneV4( triangle.v0 ) - centroid ).LengthSqr() );
		radiusSquared = MAX( radiusSquared, ( ReSTIR_SceneV4( triangle.v1 ) - centroid ).LengthSqr() );
		radiusSquared = MAX( radiusSquared, ( ReSTIR_SceneV4( triangle.v2 ) - centroid ).LengthSqr() );
	}
	light.attenuation[0] = sqrtf( radiusSquared );
	return scene.lights.AddToTail( light );
}

static ReSTIRGpuEmitterTriangle CopyEmitterTriangle( const ReSTIRGpuTriangle &source )
{
	ReSTIRGpuEmitterTriangle triangle;
	memcpy( triangle.v0, source.v0, sizeof( triangle.v0 ) );
	memcpy( triangle.v1, source.v1, sizeof( triangle.v1 ) );
	memcpy( triangle.v2, source.v2, sizeof( triangle.v2 ) );
	memcpy( triangle.uv, source.uv, sizeof( triangle.uv ) );
	triangle.uv[3] = 0.0f;
	return triangle;
}

static void ReverseEmitterTriangle( ReSTIRGpuEmitterTriangle &triangle )
{
	for ( int axis = 0; axis < 3; ++axis )
	{
		V_swap( triangle.v1[axis], triangle.v2[axis] );
	}
	V_swap( triangle.v2[3], triangle.uv[1] );
	V_swap( triangle.uv[0], triangle.uv[2] );
}

// utils/vrad/lightmap.cpp:1557-1582 — keep the texlight's VRAD intensity and patch origin;
// only its GPU sampling geometry moves out of the visibility triangle stream.
static void AppendTexlightTriangles( const ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	int face, ReSTIRGpuLight &light )
{
	light.firstTri = scene.emitterTriangles.Count();
	double totalArea = 0.0;
	for ( int i = 0; i < context.faceTriCount[face]; ++i )
	{
		ReSTIRGpuEmitterTriangle triangle = CopyEmitterTriangle( scene.triangles[context.faceTriFirst[face] + i] );
		Vector areaNormal;
		triangle.uv[3] = EmitterTriangleArea( triangle, areaNormal );
		if ( triangle.uv[3] <= 0.0f )
		{
			continue;
		}
		totalArea += triangle.uv[3];
		scene.emitterTriangles.AddToTail( triangle );
	}
	light.numTris = scene.emitterTriangles.Count() - light.firstTri;
	double cumulative = 0.0;
	for ( int i = 0; i < light.numTris; ++i )
	{
		ReSTIRGpuEmitterTriangle &triangle = scene.emitterTriangles[light.firstTri + i];
		cumulative += triangle.uv[3] / totalArea;
		triangle.uv[3] = i == light.numTris - 1 ? 1.0f : (float)cumulative;
	}
}

// utils/vrad/lightmap.cpp:1124-1169.
static void ParseLightGeneric( entity_t *pEntity, ReSTIRGpuLight &light )
{
	light.style = (int)FloatForKey( pEntity, "style" );
	Vector intensity;
	if ( g_bHDR && LightForKey( pEntity, "_lightHDR", intensity ) )
	{
	}
	else
	{
		LightForKey( pEntity, "_light", intensity );
	}
	const Vector origin = ReSTIR_SceneV4( light.origin );
	const char *target = ValueForKey( pEntity, "target" );
	if ( target[0] )
	{
		entity_t *pTarget = FindTargetEntity( target );
		if ( !pTarget )
		{
			Warning( "WARNING: light at (%i %i %i) has missing target\n", (int)origin[0], (int)origin[1], (int)origin[2] );
		}
		else
		{
			Vector destination;
			GetVectorForKey( pTarget, "origin", destination );
			Vector normal = destination - origin;
			VectorNormalize( normal );
			ReSTIR_SceneSet4( light.normal, normal );
		}
	}
	else
	{
		Vector angles, normal;
		GetVectorForKey( pEntity, "angles", angles );
		SetupLightNormalFromProps( QAngle( angles.x, angles.y, angles.z ),
			FloatForKey( pEntity, "angle" ), FloatForKey( pEntity, "pitch" ), normal );
		ReSTIR_SceneSet4( light.normal, normal );
	}
	if ( g_bHDR )
	{
		VectorScale( intensity, FloatForKeyWithDefault( pEntity, "_lightscaleHDR", 1.0 ), intensity );
	}
	ReSTIR_SceneSet4( light.intensity, intensity );
}

// utils/vrad/lightmap.cpp:1171-1259. Rescale even when the monotonic solve fails.
static void SetLightFalloffParams( entity_t *pEntity, ReSTIRGpuLight &light )
{
	float d50 = FloatForKey( pEntity, "_fifty_percent_distance" );
	light.fade[0] = 0;
	light.fade[1] = -1;
	light.fade[2] = 1.0e22;
	if ( d50 )
	{
		float d0 = FloatForKey( pEntity, "_zero_percent_distance" );
		if ( d0 < d50 )
		{
			Warning( "light has _fifty_percent_distance of %f but _zero_percent_distance of %f\n", d50, d0 );
			d0 = 2.0 * d50;
		}
		float a = 0, b = 1, c = 0;
		if ( !SolveInverseQuadraticMonotonic( 0, 1.0, d50, 2.0, d0, 256.0, a, b, c ) )
		{
			Warning( "can't solve quadratic for light %f %f\n", d50, d0 );
		}
		float v50 = c + d50 * ( b + d50 * a );
		float scale = 2.0 / v50;
		a *= scale;
		b *= scale;
		c *= scale;
		light.attenuation[2] = a;
		light.attenuation[1] = b;
		light.attenuation[0] = c;
		if ( IntForKey( pEntity, "_hardfalloff" ) )
		{
			light.fade[1] = d0;
			light.fade[0] = 0.75 * d0 + 0.25 * d50;
		}
		else
		{
			if ( fabs( a ) > 0.0 )
			{
				float maximumDistance = b / ( -2.0 * a );
				if ( maximumDistance > 0.0 )
				{
					light.fade[2] = maximumDistance;
					light.fade[0] = maximumDistance;
					light.fade[1] = 10.0 * maximumDistance;
				}
			}
		}
	}
	else
	{
		light.attenuation[0] = FloatForKey( pEntity, "_constant_attn" );
		light.attenuation[1] = FloatForKey( pEntity, "_linear_attn" );
		light.attenuation[2] = FloatForKey( pEntity, "_quadratic_attn" );
		light.origin[3] = FloatForKey( pEntity, "_distance" );
		for ( int i = 0; i < 3; ++i )
		{
			if ( light.attenuation[i] < EQUAL_EPSILON )
			{
				light.attenuation[i] = 0;
			}
		}
		if ( light.attenuation[0] < EQUAL_EPSILON && light.attenuation[1] < EQUAL_EPSILON && light.attenuation[2] < EQUAL_EPSILON )
		{
			light.attenuation[0] = 1;
		}
		float ratio = light.attenuation[0] + 100 * light.attenuation[1] + 100 * 100 * light.attenuation[2];
		if ( ratio > 0 )
		{
			Vector intensity = ReSTIR_SceneV4( light.intensity );
			VectorScale( intensity, ratio, intensity );
			ReSTIR_SceneSet4( light.intensity, intensity );
		}
	}
}

// utils/vrad/lightmap.cpp:1261-1311.
static void ParseLightSpot( entity_t *pEntity, ReSTIRGpuLight &light )
{
	ParseLightGeneric( pEntity, light );
	light.type = emit_spotlight;
	light.normal[3] = FloatForKey( pEntity, "_inner_cone" );
	if ( !light.normal[3] )
	{
		light.normal[3] = 10;
	}
	light.fade[3] = FloatForKey( pEntity, "_cone" );
	if ( !light.fade[3] )
	{
		light.fade[3] = light.normal[3];
	}
	if ( light.fade[3] < light.normal[3] )
	{
		light.fade[3] = light.normal[3];
	}
	if ( light.normal[3] == 180 && light.fade[3] == 180 )
	{
		light.normal[3] = light.fade[3] = 0;
		light.type = emit_point;
		light.attenuation[3] = 0;
	}
	else
	{
		Vector origin = ReSTIR_SceneV4( light.origin );
		if ( light.normal[3] > 90 )
		{
			Warning( "WARNING: light_spot at (%i %i %i) has inner angle larger than 90 degrees! Clamping to 90...\n",
				(int)origin[0], (int)origin[1], (int)origin[2] );
			light.normal[3] = 90;
		}
		if ( light.fade[3] > 90 )
		{
			Warning( "WARNING: light_spot at (%i %i %i) has outer angle larger than 90 degrees! Clamping to 90...\n",
				(int)origin[0], (int)origin[1], (int)origin[2] );
			light.fade[3] = 90;
		}
		light.fade[3] = (float)cos( light.fade[3] / 180 * M_PI );
		light.normal[3] = (float)cos( light.normal[3] / 180 * M_PI );
		light.attenuation[3] = FloatForKey( pEntity, "_exponent" );
	}
	SetLightFalloffParams( pEntity, light );
}

// utils/vrad/lightmap.cpp:1522-1533.
static void ParseLightPoint( entity_t *pEntity, ReSTIRGpuLight &light )
{
	ParseLightGeneric( pEntity, light );
	light.type = emit_point;
	SetLightFalloffParams( pEntity, light );
}

// utils/vrad/lightmap.cpp:1626-1658. Capacity failures propagate instead of Error().
static bool ExportLight( ReSTIRScene &scene, const ReSTIRGpuLight &light, int gpuIndex, int cluster )
{
	if ( scene.exportLights.Count() == MAX_MAP_WORLDLIGHTS )
	{
		Warning( "too many lights %d / %d\n", scene.exportLights.Count() + 1, MAX_MAP_WORLDLIGHTS );
		return false;
	}
	dworldlight_t worldLight;
	memset( &worldLight, 0, sizeof( worldLight ) );
	worldLight.cluster = cluster;
	worldLight.type = (emittype_t)light.type;
	worldLight.style = light.style;
	worldLight.origin = ReSTIR_SceneV4( light.origin );
	VectorScale( ReSTIR_SceneV4( light.intensity ), 1.0 / 255.0, worldLight.intensity );
	worldLight.normal = ReSTIR_SceneV4( light.normal );
	worldLight.stopdot = light.normal[3];
	worldLight.stopdot2 = light.fade[3];
	worldLight.exponent = light.attenuation[3];
	worldLight.radius = light.origin[3];
	worldLight.constant_attn = light.attenuation[0];
	worldLight.linear_attn = light.attenuation[1];
	worldLight.quadratic_attn = light.attenuation[2];
	worldLight.flags = 0;
	scene.exportLights.AddToTail( worldLight );
	scene.exportLightToGpuLight.AddToTail( gpuIndex );
	return true;
}

// utils/vrad/vrad.cpp:399-423 — reflectivityScale is the default 1.
static void BaseLightForFace( const dface_t &face, Vector &light, float &area, Vector &reflectivity )
{
	const dtexdata_t &data = dtexdata[texinfo[face.texinfo].texdata];
	LightForTexture( TexDataStringTable_GetString( data.nameStringTableID ), light );
	area = data.height * data.width;
	reflectivity = data.reflectivity;
	for ( int i = 0; i < 3; ++i )
	{
		if ( reflectivity[i] > 0.99 )
		{
			reflectivity[i] = 0.99;
		}
	}
}

// utils/vrad/vrad.cpp:366-392.
static winding_t *WindingFromFace( const dface_t &face, const Vector &origin )
{
	winding_t *pWinding = AllocWinding( face.numedges );
	pWinding->numpoints = face.numedges;
	for ( int i = 0; i < face.numedges; ++i )
	{
		pWinding->p[i] = dvertexes[ReSTIR_SceneFaceVertex( &face, i )].point + origin;
	}
	RemoveColinearPoints( pWinding );
	return pWinding;
}

// utils/vrad/vrad.cpp:502-664 — omit radiosity links/plane allocation, not emitter data.
static int MakePatchForFace( const ReSTIRSceneBuildContext &context, int faceIndex,
	CUtlVector<ReSTIRSurfacePatch> &patches )
{
	const dface_t &face = g_pFaces[faceIndex];
	Vector baseLight, reflectivity;
	float baseArea;
	BaseLightForFace( face, baseLight, baseArea, reflectivity );
	// Non-emitting radiosity parents have no role in the exported light list.
	if ( VectorCompare( baseLight, vec3_origin ) )
	{
		return -1;
	}
	winding_t *pWinding = WindingFromFace( face, context.faceOrigins[faceIndex] );
	float area = WindingArea( pWinding );
	if ( area <= 0 )
	{
		FreeWinding( pWinding );
		return -1;
	}
	texinfo[face.texinfo].flags |= SURF_LIGHT;
	if ( baseArea < 1.0e-6 || VectorAvg( baseLight ) < s_flDLightThreshold )
	{
		FreeWinding( pWinding );
		return -1;
	}
	ReSTIRSurfacePatch patch;
	memset( &patch, 0, sizeof( patch ) );
	patch.child[0] = patch.child[1] = -1;
	patch.gpuLight = -1;
	patch.face = faceIndex;
	patch.winding = pWinding;
	patch.area = area;
	const texinfo_t &info = texinfo[face.texinfo];
	float chopScale[2] = { 0, 0 };
	for ( int i = 0; i < 2; ++i )
	{
		for ( int j = 0; j < 3; ++j )
		{
			patch.scale[i] += info.textureVecsTexelsPerWorldUnits[i][j] * info.textureVecsTexelsPerWorldUnits[i][j];
			chopScale[i] += info.lightmapVecsLuxelsPerWorldUnits[i][j] * info.lightmapVecsLuxelsPerWorldUnits[i][j];
		}
		patch.scale[i] = sqrt( patch.scale[i] );
		chopScale[i] = sqrt( chopScale[i] );
	}
	patch.sky = ( info.flags & SURF_SKY ) != 0;
	patch.luxScale = ( chopScale[0] + chopScale[1] ) / 2;
	patch.chop = s_flMaxChop;
	WindingCenter( pWinding, patch.origin );
	patch.normal = dplanes[face.planenum].normal;
	WindingBounds( pWinding, patch.faceMins, patch.faceMaxs );
	patch.mins = patch.faceMins;
	patch.maxs = patch.faceMaxs;
	patch.baseLight = baseLight;
	patch.baseArea = baseArea;
	return patches.AddToTail( patch );
}

// utils/vrad/vrad.cpp:750-762.
static bool PreventSubdivision( const ReSTIRSurfacePatch &patch )
{
	int flags = texinfo[g_pFaces[patch.face].texinfo].flags;
	if ( flags & SURF_NOCHOP )
	{
		return true;
	}
	if ( ( flags & SURF_NOLIGHT ) && !( flags & SURF_LIGHT ) )
	{
		return true;
	}
	return false;
}

// utils/vrad/vrad.cpp:768-829.
static int CreateChildPatch( const ReSTIRSceneBuildContext &context,
	CUtlVector<ReSTIRSurfacePatch> &patches, int parentIndex,
	winding_t *pWinding, float area, const Vector &center )
{
	ReSTIRSurfacePatch child = patches[parentIndex];
	child.child[0] = child.child[1] = -1;
	child.winding = pWinding;
	child.area = area;
	child.origin = center;
	child.normal = ReSTIR_ScenePhongNormal( context, child.face, center, context.faceNormals[child.face] );
	WindingBounds( pWinding, child.mins, child.maxs );
	if ( !VectorCompare( child.baseLight, vec3_origin ) )
	{
		return patches.AddToTail( child );
	}
	Vector total = ( child.maxs - child.mins ) * child.luxScale;
	if ( child.chop > s_flMinChop && total[0] < child.chop && total[1] < child.chop && total[2] < child.chop )
	{
		for ( int i = 0; i < 3; ++i )
		{
			if ( ( child.faceMaxs[i] == child.maxs[i] || child.faceMins[i] == child.mins[i] ) && total[i] > s_flMinChop )
			{
				child.chop = MAX( s_flMinChop, child.chop / 2 );
				break;
			}
		}
	}
	return patches.AddToTail( child );
}

// utils/vrad/vrad.cpp:835-918 — including equality-at-chop and triangle splitting.
static void SubdividePatch( const ReSTIRSceneBuildContext &context,
	CUtlVector<ReSTIRSurfacePatch> &patches, int patchIndex )
{
	ReSTIRSurfacePatch &patch = patches[patchIndex];
	if ( patch.sky )
	{
		return;
	}
	Vector total = ( patch.maxs - patch.mins ) * patch.luxScale;
	float widest = -1;
	int widestAxis = -1;
	bool subdivide = false;
	for ( int i = 0; i < 3; ++i )
	{
		if ( total[i] > widest )
		{
			widestAxis = i;
			widest = total[i];
		}
		if ( total[i] >= patch.chop && total[i] >= s_flMinChop )
		{
			subdivide = true;
		}
	}
	if ( !subdivide && widestAxis != -1 )
	{
		if ( total[widestAxis] > total[( widestAxis + 1 ) % 3] * 2 && total[widestAxis] > total[( widestAxis + 2 ) % 3] * 2 )
		{
			if ( patch.chop > s_flMinChop )
			{
				subdivide = true;
				patch.chop = MAX( s_flMinChop, patch.chop / 2 );
			}
		}
	}
	if ( !subdivide )
	{
		return;
	}
	Vector split( 0, 0, 0 );
	split[widestAxis] = 1;
	float distance = ( patch.mins[widestAxis] + patch.maxs[widestAxis] ) * 0.5f;
	winding_t *front, *back;
	ClipWindingEpsilon( patch.winding, split, distance, ON_EPSILON, &front, &back );
	Vector center1, center2;
	float area1 = WindingAreaAndBalancePoint( front, center1 );
	float area2 = WindingAreaAndBalancePoint( back, center2 );
	if ( area1 == 0 || area2 == 0 )
	{
		Msg( "zero area child patch\n" );
		if ( front )
		{
			FreeWinding( front );
		}
		if ( back )
		{
			FreeWinding( back );
		}
		return;
	}
	int child1 = CreateChildPatch( context, patches, patchIndex, front, area1, center1 );
	int child2 = CreateChildPatch( context, patches, patchIndex, back, area2, center2 );
	patches[patchIndex].child[0] = child1;
	patches[patchIndex].child[1] = child2;
	SubdividePatch( context, patches, child1 );
	SubdividePatch( context, patches, child2 );
}

// utils/vrad/vrad_dispcoll.cpp:385-414,799-895.
static int CreateDispParentPatch( int faceIndex, CCoreDispInfo &disp,
	CUtlVector<ReSTIRSurfacePatch> &patches )
{
	int width = disp.GetWidth();
	int corners[4] = { 0, width * ( width - 1 ), width * width - 1, width - 1 };
	ReSTIRSurfacePatch patch;
	memset( &patch, 0, sizeof( patch ) );
	Vector reflectivity;
	BaseLightForFace( g_pFaces[faceIndex], patch.baseLight, patch.baseArea, reflectivity );
	if ( patch.baseArea < 1.0e-6 || VectorAvg( patch.baseLight ) < s_flDLightThreshold )
	{
		return -1;
	}
	patch.face = faceIndex;
	patch.child[0] = patch.child[1] = -1;
	patch.gpuLight = -1;
	patch.winding = AllocWinding( 4 );
	patch.winding->numpoints = 4;
	for ( int i = 0; i < 4; ++i )
	{
		patch.winding->p[i] = disp.GetVert( corners[i] );
		patch.origin += patch.winding->p[i];
	}
	patch.origin *= 1.0f / 4.0f;
	Vector edge0 = patch.winding->p[1] - patch.winding->p[0];
	Vector edge1 = patch.winding->p[3] - patch.winding->p[0];
	patch.normal = edge1.Cross( edge0 );
	patch.area = VectorNormalize( patch.normal );
	patch.scale[0] = patch.scale[1] = 1.0f;
	patch.chop = s_flDispChop;
	patch.mins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	patch.maxs.Init( FLT_MIN, FLT_MIN, FLT_MIN );
	for ( int i = 0; i < 4; ++i )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			patch.mins[axis] = MIN( patch.mins[axis], patch.winding->p[i][axis] );
			patch.maxs[axis] = MAX( patch.maxs[axis], patch.winding->p[i][axis] );
		}
	}
	patch.faceMins = patch.mins;
	patch.faceMaxs = patch.maxs;
	return patches.AddToTail( patch );
}

// utils/vrad/vrad_dispcoll.cpp:904-1062 — only unused links/plane allocation omitted.
static int InitDispPatch( CUtlVector<ReSTIRSurfacePatch> &patches, int parentIndex,
	int childIndex, const Vector points[3], const int indices[3] )
{
	ReSTIRSurfacePatch patch = patches[parentIndex];
	patch.child[0] = patch.child[1] = -1;
	patch.winding = AllocWinding( 3 );
	patch.winding->numpoints = 3;
	patch.origin.Init();
	for ( int i = 0; i < 3; ++i )
	{
		patch.winding->p[i] = points[i];
		patch.indices[i] = indices[i];
		patch.origin += points[i];
	}
	patch.origin *= 1.0f / 3.0f;
	Vector edge0 = points[1] - points[0];
	Vector edge1 = points[2] - points[0];
	patch.normal = edge1.Cross( edge0 );
	patch.area = VectorNormalize( patch.normal ) * 0.5f;
	patch.scale[0] = patch.scale[1] = 1.0f;
	patch.chop = s_flDispChop;
	patch.mins.Init( FLT_MAX, FLT_MAX, FLT_MAX );
	patch.maxs.Init( FLT_MIN, FLT_MIN, FLT_MIN );
	for ( int i = 0; i < 3; ++i )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			patch.mins[axis] = MIN( patch.mins[axis], points[i][axis] );
			patch.maxs[axis] = MAX( patch.maxs[axis], points[i][axis] );
		}
	}
	int index = patches.AddToTail( patch );
	patches[parentIndex].child[childIndex] = index;
	return index;
}

// utils/vrad/vrad_dispcoll.cpp:649-764 — subdivisions below the authored grid.
static void CreateDispChildPatchesSub( CUtlVector<ReSTIRSurfacePatch> &patches,
	int parentIndex, float sampleSize )
{
	const ReSTIRSurfacePatch &parent = patches[parentIndex];
	Vector points[3] = { parent.winding->p[0], parent.winding->p[1], parent.winding->p[2] };
	Vector edges[3] = { points[1] - points[0], points[2] - points[1], points[0] - points[2] };
	float minEdgeLength = sampleSize * s_flDispChop;
	float edgeLength = 0.0f;
	int longEdge = -1;
	for ( int i = 0; i < 3; ++i )
	{
		if ( edgeLength < edges[i].Length() )
		{
			edgeLength = edges[i].Length();
			longEdge = i;
		}
	}
	if ( edgeLength < minEdgeLength )
	{
		return;
	}
	float minArea = ( s_flDispChop * sampleSize ) * ( s_flDispChop * sampleSize ) * 0.5f;
	Vector normal = edges[1].Cross( edges[0] );
	float testArea = VectorNormalize( normal ) * 0.5f;
	if ( testArea < minArea )
	{
		return;
	}
	Vector childPoints[2][3];
	switch ( longEdge )
	{
	case 0:
		childPoints[0][0] = points[0];
		childPoints[0][1] = ( points[0] + points[1] ) * 0.5f;
		childPoints[0][2] = points[2];
		childPoints[1][0] = ( points[0] + points[1] ) * 0.5f;
		childPoints[1][1] = points[1];
		childPoints[1][2] = points[2];
		break;
	case 1:
		childPoints[0][0] = points[0];
		childPoints[0][1] = points[1];
		childPoints[0][2] = ( points[1] + points[2] ) * 0.5f;
		childPoints[1][0] = ( points[1] + points[2] ) * 0.5f;
		childPoints[1][1] = points[2];
		childPoints[1][2] = points[0];
		break;
	case 2:
		childPoints[0][0] = points[0];
		childPoints[0][1] = points[1];
		childPoints[0][2] = ( points[0] + points[2] ) * 0.5f;
		childPoints[1][0] = ( points[0] + points[2] ) * 0.5f;
		childPoints[1][1] = points[1];
		childPoints[1][2] = points[2];
		break;
	}
	int indices[3] = { -1, -1, -1 };
	int child1 = InitDispPatch( patches, parentIndex, 0, childPoints[0], indices );
	int child2 = InitDispPatch( patches, parentIndex, 1, childPoints[1], indices );
	CreateDispChildPatchesSub( patches, child1, sampleSize );
	CreateDispChildPatchesSub( patches, child2, sampleSize );
}

// utils/vrad/vrad_dispcoll.cpp:421-642 — root diagonal then indexed grid bisections.
static void CreateDispChildPatches( CUtlVector<ReSTIRSurfacePatch> &patches,
	int parentIndex, CCoreDispInfo &disp, int level, float sampleSize )
{
	const ReSTIRSurfacePatch &parent = patches[parentIndex];
	int pointCount = parent.winding->numpoints;
	Assert( pointCount == 3 || pointCount == 4 );
	Vector edges[4];
	edges[0] = parent.winding->p[1] - parent.winding->p[0];
	if ( pointCount == 4 )
	{
		edges[1] = parent.winding->p[2] - parent.winding->p[1];
		edges[2] = parent.winding->p[3] - parent.winding->p[2];
		edges[3] = parent.winding->p[3] - parent.winding->p[0];
	}
	else
	{
		edges[1] = parent.winding->p[2] - parent.winding->p[0];
		edges[2] = parent.winding->p[2] - parent.winding->p[1];
	}
	float minEdgeLength = sampleSize * s_flDispChop;
	float edgeLength = 0.0f;
	for ( int i = 0; i < pointCount; ++i )
	{
		if ( edgeLength < edges[i].Length() )
		{
			edgeLength = edges[i].Length();
		}
	}
	if ( edgeLength < minEdgeLength )
	{
		return;
	}
	Vector normal = pointCount == 4 ? edges[3].Cross( edges[0] ) : edges[1].Cross( edges[0] );
	float testArea = VectorNormalize( normal );
	float minArea = ( s_flDispChop * sampleSize ) * ( s_flDispChop * sampleSize );
	if ( pointCount == 3 )
	{
		testArea *= 0.5f;
		minArea *= 0.5f;
	}
	if ( testArea < minArea )
	{
		return;
	}
	if ( pointCount == 3 && level >= disp.GetPower() * 2 )
	{
		CreateDispChildPatchesSub( patches, parentIndex, sampleSize );
		return;
	}
	int childIndices[2][3];
	if ( pointCount == 4 )
	{
		int width = disp.GetWidth();
		childIndices[0][0] = width * width - 1;
		childIndices[0][1] = 0;
		childIndices[0][2] = width * ( width - 1 );
		childIndices[1][0] = 0;
		childIndices[1][1] = width * width - 1;
		childIndices[1][2] = width - 1;
	}
	else
	{
		int newIndex = ( parent.indices[1] + parent.indices[0] ) / 2;
		childIndices[0][0] = parent.indices[2];
		childIndices[0][1] = parent.indices[0];
		childIndices[0][2] = newIndex;
		childIndices[1][0] = parent.indices[1];
		childIndices[1][1] = parent.indices[2];
		childIndices[1][2] = newIndex;
	}
	Vector childPoints[2][3];
	for ( int i = 0; i < 2; ++i )
	{
		for ( int j = 0; j < 3; ++j )
		{
			childPoints[i][j] = disp.GetVert( childIndices[i][j] );
		}
	}
	int child1 = InitDispPatch( patches, parentIndex, 0, childPoints[0], childIndices[0] );
	int child2 = InitDispPatch( patches, parentIndex, 1, childPoints[1], childIndices[1] );
	int childLevel = pointCount == 4 ? 0 : level + 1;
	CreateDispChildPatches( patches, child1, disp, childLevel, sampleSize );
	CreateDispChildPatches( patches, child2, disp, childLevel, sampleSize );
}

// utils/vrad/vrad.cpp:996-1012 — keep winding-point fallback for solid origins.
static int ClusterForPatch( const ReSTIRSurfacePatch &patch )
{
	int cluster = ReSTIR_SceneClusterFromPoint( patch.origin );
	if ( cluster == -1 )
	{
		for ( int i = 0; i < patch.winding->numpoints; ++i )
		{
			int pointCluster = ReSTIR_SceneClusterFromPoint( patch.winding->p[i] );
			if ( pointCluster != -1 )
			{
				cluster = pointCluster;
				break;
			}
		}
	}
	return cluster;
}

// utils/vrad/lightmap.cpp:1557-1582. One GPU face emitter; one exported leaf light.
static bool BuildSurfaceLights( ReSTIRSceneBuildContext &context, ReSTIRScene &scene, CUtlVector<bool> &texlightFaces )
{
	CUtlVector<ReSTIRSurfacePatch> patches;
	for ( int model = 0; model < nummodels; ++model )
	{
		for ( int i = 0; i < dmodels[model].numfaces; ++i )
		{
			int face = dmodels[model].firstface + i;
			if ( g_pFaces[face].dispinfo == -1 && g_pFaces[face].texinfo >= 0 )
			{
				MakePatchForFace( context, face, patches );
			}
		}
	}
	for ( int i = 0; i < g_dispinfo.Count(); ++i )
	{
		int face = g_dispinfo[i].m_iMapFace;
		CreateDispParentPatch( face, *context.displacements[i], patches );
	}
	const int rootCount = patches.Count();
	for ( int i = 0; i < rootCount; ++i )
	{
		ReSTIRSurfacePatch &patch = patches[i];
		if ( patch.baseArea < 1.0e-6 || VectorAvg( patch.baseLight ) < s_flDLightThreshold )
		{
			continue;
		}
		int face = patch.face;
		ReSTIRGpuLight light = AllocLight( patch.origin );
		light.type = emit_surface;
		AppendTexlightTriangles( context, scene, face, light );
		ReSTIR_SceneSet4( light.normal, patch.normal );
		Vector intensity;
		VectorScale( patch.baseLight, s_flLightScale * patch.scale[0] * patch.scale[1] / patch.baseArea, intensity );
		VectorScale( intensity, s_flDirectScale, intensity );
		ReSTIR_SceneSet4( light.intensity, intensity );
		patch.gpuLight = scene.lights.AddToTail( light );
		texlightFaces[face] = true;
	}
	for ( int i = 0; i < rootCount; ++i )
	{
		if ( patches[i].gpuLight < 0 || PreventSubdivision( patches[i] ) )
		{
			continue;
		}
		int face = patches[i].face;
		if ( g_pFaces[face].dispinfo == -1 )
		{
			SubdividePatch( context, patches, i );
		}
		else
		{
			CCoreDispInfo &disp = *context.displacements[g_pFaces[face].dispinfo];
			const texinfo_t &info = texinfo[g_pFaces[face].texinfo];
			Vector luxelAxis( info.lightmapVecsLuxelsPerWorldUnits[0][0],
				info.lightmapVecsLuxelsPerWorldUnits[0][1], info.lightmapVecsLuxelsPerWorldUnits[0][2] );
			CreateDispChildPatches( patches, i, disp, 0, 1.0f / luxelAxis.Length() );
		}
	}
	bool success = true;
	for ( int i = 0; i < patches.Count(); ++i )
	{
		ReSTIRSurfacePatch &patch = patches[i];
		if ( !success || patch.child[0] != -1 || patch.baseArea < 1.0e-6 || patch.gpuLight < 0 )
		{
			continue;
		}
		if ( VectorAvg( patch.baseLight ) >= s_flDLightThreshold )
		{
			ReSTIRGpuLight light = AllocLight( patch.origin );
			light.type = emit_surface;
			ReSTIR_SceneSet4( light.normal, patch.normal );
			Vector intensity;
			VectorScale( patch.baseLight, s_flLightScale * patch.area * patch.scale[0] * patch.scale[1] / patch.baseArea, intensity );
			VectorScale( intensity, s_flDirectScale, intensity );
			ReSTIR_SceneSet4( light.intensity, intensity );
			success = ExportLight( scene, light, patch.gpuLight, ClusterForPatch( patch ) );
		}
	}
	for ( int i = 0; i < patches.Count(); ++i )
	{
		FreeWinding( patches[i].winding );
	}
	return success;
}

// Brush geometry mirrors AddFaceGeometry (restir_scene.cpp:801-825), and displacement
// winding mirrors utils/vrad/vrad_dispcoll.cpp:1064-1080. Emission always uses material UVs,
// not the displacement-grid UVs used by the visibility and lightmap gather geometry.
static bool BuildMaterialSurfaceLights( ReSTIRSceneBuildContext &context, ReSTIRScene &scene,
	const CUtlVector<bool> &texlightFaces )
{
	int brushEmitters = 0, dispEmitters = 0;
	CUtlVector<ReSTIRGpuEmitterTriangle> triangles;
	for ( int model = 0; model < nummodels; ++model )
	{
		for ( int i = 0; i < dmodels[model].numfaces; ++i )
		{
			const int faceIndex = dmodels[model].firstface + i;
			const dface_t &face = g_pFaces[faceIndex];
			if ( face.texinfo < 0 || texlightFaces[faceIndex] )
			{
				continue;
			}
			const texinfo_t &info = texinfo[face.texinfo];
			if ( info.flags & ( SURF_NODRAW | SURF_SKY | SURF_SKY2D | SURF_SKIP | SURF_HINT | SURF_TRIGGER ) )
			{
				continue;
			}
			ReSTIRMaterialEmission emission;
			if ( !ReSTIR_GetMaterialEmission( scene, *context.options, ReSTIR_SceneFaceMaterial( &face ), emission ) )
			{
				continue;
			}
			triangles.RemoveAll();
			if ( face.dispinfo == -1 )
			{
				triangles.EnsureCapacity( context.faceTriCount[faceIndex] * ( emission.twoSided ? 2 : 1 ) );
				for ( int tri = 0; tri < context.faceTriCount[faceIndex]; ++tri )
				{
					triangles.AddToTail( CopyEmitterTriangle( scene.triangles[context.faceTriFirst[faceIndex] + tri] ) );
				}
			}
			else
			{
				CCoreDispInfo &displacement = *context.displacements[face.dispinfo];
				const dtexdata_t &data = dtexdata[info.texdata];
				triangles.EnsureCapacity( displacement.GetTriCount() * ( emission.twoSided ? 2 : 1 ) );
				for ( int tri = 0; tri < displacement.GetTriCount(); ++tri )
				{
					unsigned short indices[3];
					displacement.GetTriIndices( tri, indices[0], indices[2], indices[1] );
					ReSTIRGpuEmitterTriangle triangle;
					ReSTIR_SceneSet4( triangle.v0, displacement.GetVert( indices[0] ) );
					ReSTIR_SceneSet4( triangle.v1, displacement.GetVert( indices[1] ) );
					ReSTIR_SceneSet4( triangle.v2, displacement.GetVert( indices[2] ) );
					float uv[6];
					for ( int corner = 0; corner < 3; ++corner )
					{
						Vector flat;
						displacement.GetFlatVert( indices[corner], flat );
						ReSTIR_ScenePointUV( info, data, flat, uv[corner * 2], uv[corner * 2 + 1] );
					}
					triangle.v0[3] = uv[0];
					triangle.v1[3] = uv[1];
					triangle.v2[3] = uv[2];
					triangle.uv[0] = uv[3];
					triangle.uv[1] = uv[4];
					triangle.uv[2] = uv[5];
					triangle.uv[3] = 0.0f;
					triangles.AddToTail( triangle );
				}
			}
			if ( emission.twoSided )
			{
				const int frontCount = triangles.Count();
				for ( int tri = 0; tri < frontCount; ++tri )
				{
					ReSTIRGpuEmitterTriangle reversed = triangles[tri];
					ReverseEmitterTriangle( reversed );
					triangles.AddToTail( reversed );
				}
			}
			if ( ReSTIR_AddSurfaceEmitter( scene, triangles.Base(), triangles.Count(),
				emission.intensity, emission.texture, RESTIR_LIGHT_MATERIAL ) >= 0 )
			{
				if ( face.dispinfo == -1 )
					++brushEmitters;
				else
					++dispEmitters;
			}
		}
	}
	const int beforeProps = scene.lights.Count();
	if ( !g_ReSTIRStaticPropMgr.AppendEmitters( scene, *context.options ) )
	{
		return false;
	}
	const int propEmitters = scene.lights.Count() - beforeProps;
	Msg( "%d material emitters (%d brush, %d displacement, %d static prop), %d emitter triangles\n",
		brushEmitters + dispEmitters + propEmitters, brushEmitters, dispEmitters, propEmitters,
		scene.emitterTriangles.Count() );
	return true;
}

// utils/vrad/lightmap.cpp:1465-1473 — distinguish absent from empty keys.
static const char *ValueForKeyOrNull( entity_t *pEntity, const char *pKey )
{
	for ( epair_t *pair = pEntity->epairs; pair; pair = pair->next )
	{
		if ( !strcmp( pair->key, pKey ) )
		{
			return pair->value;
		}
	}
	return NULL;
}

// utils/vrad/lightmap.cpp:1475-1520. Ambient has calloc defaults, not sun fields.
static bool ParseLightEnvironment( entity_t *pEntity, ReSTIRScene &scene, float &sunSpreadAngle )
{
	Vector origin;
	GetVectorForKey( pEntity, "origin", origin );
	ReSTIRGpuLight light = AllocLight( origin );
	ParseLightGeneric( pEntity, light );
	const char *angle = ValueForKeyOrNull( pEntity, "SunSpreadAngle" );
	if ( angle )
	{
		sunSpreadAngle = atof( angle );
		printf( "sun extent from map=%f\n", sin( ( M_PI / 180.0 ) * sunSpreadAngle ) );
		if ( scene.skyLight >= 0 )
		{
			scene.lights[scene.skyLight].sunSpreadAngle = sunSpreadAngle;
		}
	}
	if ( scene.skyLight < 0 )
	{
		light.type = emit_skylight;
		light.sunSpreadAngle = sunSpreadAngle;
		scene.skyLight = scene.lights.AddToTail( light );
		if ( !ExportLight( scene, light, scene.skyLight, ReSTIR_SceneClusterFromPoint( origin ) ) )
		{
			return false;
		}
		ReSTIRGpuLight ambient = AllocLight( origin );
		ambient.type = emit_skyambient;
		Vector intensity;
		if ( g_bHDR && LightForKey( pEntity, "_ambientHDR", intensity ) )
		{
		}
		else if ( !LightForKey( pEntity, "_ambient", intensity ) )
		{
			VectorScale( ReSTIR_SceneV4( light.intensity ), 0.5, intensity );
		}
		if ( g_bHDR )
		{
			VectorScale( intensity, FloatForKeyWithDefault( pEntity, "_AmbientScaleHDR", 1.0 ), intensity );
		}
		ReSTIR_SceneSet4( ambient.intensity, intensity );
		scene.skyAmbientLight = scene.lights.AddToTail( ambient );
		if ( !ExportLight( scene, ambient, scene.skyAmbientLight, ReSTIR_SceneClusterFromPoint( origin ) ) )
		{
			return false;
		}
	}
	return true;
}

// utils/vrad/lightmap.cpp:1587-1618 — class names are case sensitive.
static bool BuildEntityLights( ReSTIRScene &scene )
{
	float sunSpreadAngle = 0.0f;
	for ( int i = 0; i < num_entities; ++i )
	{
		entity_t *pEntity = &entities[i];
		const char *name = ValueForKey( pEntity, "classname" );
		if ( strncmp( name, "light", 5 ) || !strcmp( name, "light_dynamic" ) )
		{
			continue;
		}
		if ( !strcmp( name, "light_environment" ) )
		{
			if ( !ParseLightEnvironment( pEntity, scene, sunSpreadAngle ) )
			{
				return false;
			}
			continue;
		}
		Vector origin;
		GetVectorForKey( pEntity, "origin", origin );
		ReSTIRGpuLight light = AllocLight( origin );
		if ( !strcmp( name, "light_spot" ) )
		{
			ParseLightSpot( pEntity, light );
		}
		else if ( !strcmp( name, "light" ) )
		{
			ParseLightPoint( pEntity, light );
		}
		else
		{
			qprintf( "unsupported light entity: \"%s\"\n", name );
			continue;
		}
		int index = scene.lights.AddToTail( light );
		if ( !ExportLight( scene, light, index, ReSTIR_SceneClusterFromPoint( origin ) ) )
		{
			return false;
		}
	}
	return true;
}

// utils/vrad/lightmap.cpp:982-985,991-995,1633 — reverse leaf/entity allocation order.
// Face aggregates follow the first encounter of their leaves in that active list.
static void FinishActiveLightOrder( ReSTIRScene &scene )
{
	for ( int i = 0; i < scene.exportLights.Count() / 2; ++i )
	{
		int other = scene.exportLights.Count() - i - 1;
		V_swap( scene.exportLights[i], scene.exportLights[other] );
		V_swap( scene.exportLightToGpuLight[i], scene.exportLightToGpuLight[other] );
	}
	CUtlVector<int> oldToNew;
	oldToNew.SetCount( scene.lights.Count() );
	for ( int i = 0; i < oldToNew.Count(); ++i )
	{
		oldToNew[i] = -1;
	}
	int nextLight = 0;
	for ( int i = 0; i < scene.exportLightToGpuLight.Count(); ++i )
	{
		int oldIndex = scene.exportLightToGpuLight[i];
		if ( oldToNew[oldIndex] < 0 )
		{
			oldToNew[oldIndex] = nextLight++;
		}
		scene.exportLightToGpuLight[i] = oldToNew[oldIndex];
	}
	Assert( nextLight == scene.lights.Count() );
	if ( scene.skyLight >= 0 )
	{
		scene.skyLight = oldToNew[scene.skyLight];
		scene.skyAmbientLight = oldToNew[scene.skyAmbientLight];
	}
	// Apply the permutation in place; export mappings already use final indices.
	for ( int i = 0; i < oldToNew.Count(); ++i )
	{
		while ( oldToNew[i] != i )
		{
			int destination = oldToNew[i];
			V_swap( scene.lights[i], scene.lights[destination] );
			V_swap( oldToNew[i], oldToNew[destination] );
		}
	}
}

// utils/vrad/lightmap.cpp:1541-1658; sceneStyles is the shared GPU indexing contract.
bool ReSTIR_SceneBuildLights( ReSTIRSceneBuildContext &context, ReSTIRScene &scene )
{
	if ( !s_bLightFilesValid )
	{
		return false;
	}
	CUtlVector<bool> texlightFaces;
	texlightFaces.SetCount( context.faceCount );
	for ( int i = 0; i < texlightFaces.Count(); ++i )
	{
		texlightFaces[i] = false;
	}
	if ( !BuildSurfaceLights( context, scene, texlightFaces ) || !BuildEntityLights( scene ) )
	{
		return false;
	}
	FinishActiveLightOrder( scene );
	if ( !BuildMaterialSurfaceLights( context, scene, texlightFaces ) )
	{
		return false;
	}
	scene.sceneStyles.RemoveAll();
	scene.sceneStyles.AddToTail( 0 );
	for ( int i = 0; i < scene.lights.Count(); ++i )
	{
		int style = scene.lights[i].style;
		if ( style == 0 || scene.sceneStyles.Find( style ) >= 0 )
		{
			continue;
		}
		int slot = 1;
		while ( slot < scene.sceneStyles.Count() && scene.sceneStyles[slot] < style )
		{
			++slot;
		}
		scene.sceneStyles.InsertBefore( slot, style );
	}
	for ( int i = 0; i < scene.lights.Count(); ++i )
	{
		scene.lights[i].styleSlot = scene.sceneStyles.Find( scene.lights[i].style );
	}
	// Header offsets and light indices share one buffer (restir_types.h styleLights contract).
	// Iterate lights in ascending order within each style; sky has separate sampling paths.
	scene.styleLights.RemoveAll();
	const int numStyles = scene.sceneStyles.Count();
	scene.styleLights.SetCount( numStyles + 1 );
	scene.styleLights.EnsureCapacity( numStyles + 1 + scene.lights.Count() );
	for ( int slot = 0; slot < numStyles; ++slot )
	{
		scene.styleLights[slot] = scene.styleLights.Count();
		for ( int i = 0; i < scene.lights.Count(); ++i )
		{
			const ReSTIRGpuLight &light = scene.lights[i];
			if ( light.styleSlot == slot && light.type != emit_skylight && light.type != emit_skyambient )
			{
				scene.styleLights.AddToTail( i );
			}
		}
	}
	scene.styleLights[numStyles] = scene.styleLights.Count();
	return true;
}
