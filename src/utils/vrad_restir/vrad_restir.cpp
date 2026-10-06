//========= Copyright Valve Corporation, All rights reserved. ============//
// VRAD ReSTIR host orchestration and command-line surface.

#include "vrad_restir.h"
#include "restir_scene.h"
#include "restir_vulkan.h"
#include "restir_denoiser.h"
#include "restir_staticprops.h"
#include "ambient_cube.h"
#include "bsp_output.h"
#include "prop_lighting.h"
#include "restir_lightmap_rescale.h"
#include "cmdlib.h"
#include "bsplib.h"
#include "loadcmdline.h"
#include "tools_minidump.h"
#include "tier0/icommandline.h"
#include "tier0/fasttimer.h"
#include "tier1/strtools.h"
#include "mathlib/mathlib.h"

#include <errno.h>
#include <float.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>

ReSTIROptions g_ReSTIROptions;
static CUtlString s_ShadowMapDiagnosticsPath;
static bool s_BakeBothModes = false;
dface_t *g_pFaces = NULL;

static CVRadRestirDLL g_VRadRestirDLL;
EXPOSE_SINGLE_INTERFACE( CVRadRestirDLL, IVRadDLL, VRAD_INTERFACE_VERSION );

static bool ParseIntValue( const char *pOption, const char *pValue, int minValue, int maxValue, int &outValue )
{
	if ( !pValue || !pValue[0] )
	{
		Warning( "Error: expected a value after '%s'\n", pOption );
		return false;
	}

	char *pEnd = NULL;
	errno = 0;
	long value = strtol( pValue, &pEnd, 10 );
	if ( errno == ERANGE || pEnd == pValue || *pEnd != 0 || value < minValue || value > maxValue )
	{
		Warning( "Error: invalid value '%s' after '%s' (expected %d..%d)\n", pValue, pOption, minValue, maxValue );
		return false;
	}

	outValue = (int)value;
	return true;
}

static bool ParseFloatValue( const char *pOption, const char *pValue, float minValue, float maxValue, float &outValue )
{
	if ( !pValue || !pValue[0] )
	{
		Warning( "Error: expected a value after '%s'\n", pOption );
		return false;
	}

	char *pEnd = NULL;
	errno = 0;
	double value = strtod( pValue, &pEnd );
	if ( errno == ERANGE || pEnd == pValue || *pEnd != 0 || value < minValue || value > maxValue || value != value || value > FLT_MAX || value < -FLT_MAX )
	{
		Warning( "Error: invalid value '%s' after '%s' (expected %.3f..%.3f)\n", pValue, pOption, minValue, maxValue );
		return false;
	}

	outValue = (float)value;
	return true;
}

static bool IsOption( const char *pArg, const char *pName )
{
	return pArg && pName && !Q_stricmp( pArg, pName );
}

static bool IsIgnoredOption( const char *pArg )
{
	static const char *const s_pIgnored[] =
	{
		"-extra", "-noextra", "-low", "-fastambient", "-nodetaillight",
		"-nossprops", "-StaticPropNormals", "-OnlyStaticProps", "-centersamples", "-verbose", "-v",
		"-novconfig", "-StopOnExit", "-steam", "-allowdebug", "-FullMinidumps", "-insert_search_path",
		"-debugextra", "-rederrors", "-dump", "-dumpnormals", "-dumptrace", "-LargeDispSampleRadius",
		"-dumppropmaps", "-noskyboxrecurse", "-loghash", "-onlydetail", "-dlightmap", "-luxeldensity",
		"-maxchop", "-scale", "-ambient", "-dlight", "-sky", "-notexscale", "-coring", NULL
	};

	for ( int i = 0; s_pIgnored[i]; ++i )
	{
		if ( IsOption( pArg, s_pIgnored[i] ) )
			return true;
	}
	return false;
}

static bool IsMPIOptionWithValue( const char *pArg )
{
	return IsOption( pArg, "-mpi_pw" ) || IsOption( pArg, "-mpi_worker" ) ||
		IsOption( pArg, "-mpi_Port" ) || IsOption( pArg, "-mpi_WorkerCount" ) ||
		IsOption( pArg, "-mpi_FileTransmitRate" ) || IsOption( pArg, "-mpi_Verbose" );
}

static bool ConsumeIgnoredValue( int argc, char **argv, int &i, const char *pOption, bool required )
{
	if ( i + 1 >= argc || !argv[i + 1] || ( argv[i + 1][0] == '-' && required ) )
	{
		if ( required )
			Warning( "Error: expected a value after '%s'\n", pOption );
		return !required;
	}
	++i;
	return true;
}

// Preset values are measured on ep2_outland_09 (RTX 4070 SUPER, 920k luxels, per mode):
//   fast    32 it / 4 cand / 2 bounces:  ~9 s, p10 face ratio vs VRAD 0.71 (previews, iteration)
//   default 128 it / 8 cand / 4 bounces: ~14 s, p10 0.83
//   final   512 it / 16 cand / 6 bounces: ~34 s, p10 0.88, lowest residual noise before denoising
// Only knobs the user did not set explicitly are touched.
struct ReSTIRExplicitOptions
{
	bool iterations;
	bool candidates;
	bool maxBounces;
	bool denoiserQuality;
	ReSTIRExplicitOptions() : iterations( false ), candidates( false ), maxBounces( false ), denoiserQuality( false ) {}
};

static void ApplyPreset( ReSTIROptions &options, const ReSTIRExplicitOptions &explicitOptions )
{
	int iterations = options.iterations;
	int candidates = options.candidates;
	int maxBounces = options.maxBounces;
	ReSTIRDenoiserQuality quality = options.denoiserQuality;
	switch ( options.preset )
	{
	case RESTIR_PRESET_FAST:
		iterations = 32;
		candidates = 4;
		maxBounces = 2;
		quality = RESTIR_DENOISER_QUALITY_FAST;
		break;
	case RESTIR_PRESET_FINAL:
		iterations = 512;
		candidates = 16;
		maxBounces = 6;
		quality = RESTIR_DENOISER_QUALITY_HIGH;
		break;
	default:
		return;
	}
	if ( !explicitOptions.iterations )
		options.iterations = iterations;
	if ( !explicitOptions.candidates )
		options.candidates = candidates;
	if ( !explicitOptions.maxBounces )
		options.maxBounces = maxBounces;
	if ( !explicitOptions.denoiserQuality )
		options.denoiserQuality = quality;
}

// Ported command-line coverage from utils/vrad/vrad.cpp:2380-2795; unlike
// legacy VRAD, unknown option tokens are hard errors rather than map names.
static bool ParseRestirOptions( int argc, char **argv, ReSTIROptions &options )
{
	options = ReSTIROptions();
	s_ShadowMapDiagnosticsPath = "";
	s_BakeBothModes = false;
	ReSTIRExplicitOptions explicitOptions;
	int mapArg = -1;
	bool loggedIgnored = false;
	bool explicitMode = false;

	for ( int i = 1; i < argc; ++i )
	{
		const char *pArg = argv[i];
		if ( !pArg || !pArg[0] )
			continue;

		if ( pArg[0] != '-' )
		{
			if ( mapArg != -1 )
			{
				Warning( "Error: more than one BSP/map argument was supplied ('%s')\n", pArg );
				return false;
			}
			mapArg = i;
			continue;
		}

		if ( IsOption( pArg, "-ldr" ) || IsOption( pArg, "-hdr" ) )
		{
			options.hdr = IsOption( pArg, "-hdr" );
			explicitMode = true;
			continue;
		}
		if ( IsOption( pArg, "-both" ) )
		{
			// Mode orchestration belongs to the DLL, including converted-input rebakes.
			s_BakeBothModes = true;
			options.hdr = false;
			explicitMode = true;
			continue;
		}
		if ( IsOption( pArg, "-fast" ) || IsOption( pArg, "-final" ) )
		{
			ReSTIRPreset preset = IsOption( pArg, "-fast" ) ? RESTIR_PRESET_FAST : RESTIR_PRESET_FINAL;
			if ( options.preset != RESTIR_PRESET_DEFAULT && options.preset != preset )
			{
				Warning( "Error: -fast and -final are mutually exclusive\n" );
				return false;
			}
			options.preset = preset;
			continue;
		}
		if ( IsOption( pArg, "-restir_shadowmap_diagnostics" ) )
		{
			if ( i + 1 >= argc || !argv[i + 1] || !argv[i + 1][0] || argv[i + 1][0] == '-' )
			{
				Warning( "Error: expected a filepath after '-restir_shadowmap_diagnostics'\n" );
				return false;
			}
			s_ShadowMapDiagnosticsPath = argv[++i];
			continue;
		}
		if ( IsOption( pArg, "-restir_hlight_density" ) )
		{
			if ( i + 1 >= argc || !ParseIntValue( pArg, argv[++i], 1, 16382, options.highresDensity ) )
				return false;
			continue;
		}
		if ( IsOption( pArg, "-restir_shadowmaps" ) )
		{
			options.shadowMaps = true;
			continue;
		}
		if ( IsOption( pArg, "-StaticPropLighting" ) )
		{
			options.staticPropLighting = true;
			continue;
		}
		if ( IsOption( pArg, "-restir_notexturealbedo" ) )
		{
			options.textureAlbedo = false;
			continue;
		}
		if ( IsOption( pArg, "-restir_texturealbedo" ) )
		{
			options.textureAlbedo = true;
			continue;
		}
		if ( IsOption( pArg, "-TextureShadows" ) || IsOption( pArg, "-textureshadows" ) )
		{
			options.textureShadows = true;
			continue;
		}
		if ( IsOption( pArg, "-StaticPropPolys" ) )
		{
			Msg( "Ignoring -StaticPropPolys: VRAD ReSTIR always traces static props at polygon precision.\n" );
			continue;
		}
		if ( IsOption( pArg, "-lights" ) )
		{
			if ( i + 1 >= argc || !argv[i + 1] || !argv[i + 1][0] || argv[i + 1][0] == '-' )
			{
				Warning( "Error: expected a filepath after '-lights'\n" );
				return false;
			}
			options.lightsFile = argv[++i];
			continue;
		}
		if ( IsOption( pArg, "-vproject" ) || IsOption( pArg, "-game" ) || IsOption( pArg, "-insert_search_path" ) )
		{
			if ( !ConsumeIgnoredValue( argc, argv, i, pArg, true ) )
				return false;
			continue;
		}
		if ( IsOption( pArg, "-smooth" ) )
		{
			float degrees;
			if ( i + 1 >= argc || !ParseFloatValue( pArg, argv[++i], 0.0f, 180.0f, degrees ) )
				return false;
			options.smoothingThreshold = cosf( degrees * ( M_PI / 180.0f ) );
			continue;
		}

		if ( IsOption( pArg, "-restir_iterations" ) || IsOption( pArg, "-restir_candidates" ) ||
			IsOption( pArg, "-restir_spatial_radius" ) || IsOption( pArg, "-restir_maxbounces" ) ||
			IsOption( pArg, "-restir_seed" ) || IsOption( pArg, "-restir_gpu" ) )
		{
			if ( i + 1 >= argc )
			{
				Warning( "Error: expected a value after '%s'\n", pArg );
				return false;
			}
			int value = 0;
			int minValue = 0;
			int maxValue = 0;
			if ( IsOption( pArg, "-restir_iterations" ) ) { minValue = 1; maxValue = 65535; }
			else if ( IsOption( pArg, "-restir_candidates" ) ) { minValue = 1; maxValue = 4096; }
			else if ( IsOption( pArg, "-restir_spatial_radius" ) ) { minValue = 0; maxValue = 128; }
			else if ( IsOption( pArg, "-restir_maxbounces" ) ) { minValue = 0; maxValue = 128; }
			else if ( IsOption( pArg, "-restir_seed" ) ) { minValue = 0; maxValue = 0x7fffffff; }
			else { minValue = 0; maxValue = 255; }
			if ( !ParseIntValue( pArg, argv[++i], minValue, maxValue, value ) )
				return false;
			if ( IsOption( pArg, "-restir_iterations" ) ) { options.iterations = value; explicitOptions.iterations = true; }
			else if ( IsOption( pArg, "-restir_candidates" ) ) { options.candidates = value; explicitOptions.candidates = true; }
			else if ( IsOption( pArg, "-restir_spatial_radius" ) ) options.spatialRadius = value;
			else if ( IsOption( pArg, "-restir_maxbounces" ) ) { options.maxBounces = value; explicitOptions.maxBounces = true; }
			else if ( IsOption( pArg, "-restir_seed" ) ) options.seed = value;
			else options.gpuIndex = value;
			continue;
		}
		if ( IsOption( pArg, "-restir_force_compute_bvh" ) )
		{
			options.forceComputeBvh = true;
			continue;
		}
		if ( IsOption( pArg, "-restir_lightmapscale" ) )
		{
			if ( i + 1 >= argc )
			{
				Warning( "Error: expected a value in (0,1] after '-restir_lightmapscale'\n" );
				return false;
			}
			if ( !ParseFloatValue( pArg, argv[++i], 0.0625f, 1.0f, options.lightmapScale ) )
				return false;
			continue;
		}
		if ( IsOption( pArg, "-restir_emissivescale" ) )
		{
			if ( i + 1 >= argc )
			{
				Warning( "Error: expected a value after '%s'\n", pArg );
				return false;
			}
			if ( !ParseFloatValue( pArg, argv[++i], 0.0f, 1000.0f, options.emissiveScale ) )
				return false;
			continue;
		}
		if ( IsOption( pArg, "-restir_probe" ) )
		{
			// Diagnostic: evaluate LightPoints at a world point/normal after the bake and print it.
			if ( i + 6 >= argc )
			{
				Warning( "Error: expected 'x y z nx ny nz' after '-restir_probe'\n" );
				return false;
			}
			for ( int k = 0; k < 6; ++k )
			{
				float value = 0.0f;
				if ( !ParseFloatValue( pArg, argv[i + 1 + k], -1.0e9f, 1.0e9f, value ) )
					return false;
				options.probe[k] = value;
			}
			options.probeEnabled = true;
			i += 6;
			continue;
		}
		if ( IsOption( pArg, "-restir_denoiser" ) )
		{
			if ( i + 1 >= argc || !argv[i + 1] )
			{
				Warning( "Error: expected oidn or none after '-restir_denoiser'\n" );
				return false;
			}
			const char *pValue = argv[++i];
			if ( !Q_stricmp( pValue, "oidn" ) ) options.denoiser = RESTIR_DENOISER_OIDN;
			else if ( !Q_stricmp( pValue, "none" ) ) options.denoiser = RESTIR_DENOISER_NONE;
			else { Warning( "Error: invalid denoiser '%s'\n", pValue ); return false; }
			continue;
		}
		if ( IsOption( pArg, "-restir_denoiser_quality" ) )
		{
			if ( i + 1 >= argc || !argv[i + 1] ) { Warning( "Error: expected fast, balanced, or high after '-restir_denoiser_quality'\n" ); return false; }
			const char *pValue = argv[++i];
			if ( !Q_stricmp( pValue, "fast" ) ) options.denoiserQuality = RESTIR_DENOISER_QUALITY_FAST;
			else if ( !Q_stricmp( pValue, "balanced" ) ) options.denoiserQuality = RESTIR_DENOISER_QUALITY_BALANCED;
			else if ( !Q_stricmp( pValue, "high" ) ) options.denoiserQuality = RESTIR_DENOISER_QUALITY_HIGH;
			else { Warning( "Error: invalid denoiser quality '%s'\n", pValue ); return false; }
			explicitOptions.denoiserQuality = true;
			continue;
		}
		if ( IsOption( pArg, "-restir_denoiser_device" ) )
		{
			if ( i + 1 >= argc || !argv[i + 1] ) { Warning( "Error: expected default, cpu, or gpu after '-restir_denoiser_device'\n" ); return false; }
			const char *pValue = argv[++i];
			if ( !Q_stricmp( pValue, "default" ) ) options.denoiserDevice = RESTIR_DENOISER_DEVICE_DEFAULT;
			else if ( !Q_stricmp( pValue, "cpu" ) ) options.denoiserDevice = RESTIR_DENOISER_DEVICE_CPU;
			else if ( !Q_stricmp( pValue, "gpu" ) ) options.denoiserDevice = RESTIR_DENOISER_DEVICE_GPU;
			else { Warning( "Error: invalid denoiser device '%s'\n", pValue ); return false; }
			continue;
		}

		// Additional value-taking legacy switches present in vrad.cpp. They are
		// consumed so a following value is never mistaken for the map name.
		if ( IsOption( pArg, "-luxeldensity" ) || IsOption( pArg, "-maxchop" ) ||
			IsOption( pArg, "-scale" ) || IsOption( pArg, "-dlight" ) ||
			IsOption( pArg, "-sky" ) || IsOption( pArg, "-coring" ) )
		{
			if ( i + 1 >= argc )
			{
				Warning( "Error: expected a value after '%s'\n", pArg );
				return false;
			}
			float ignoredValue = 0.0f;
			const float minValue = IsOption( pArg, "-luxeldensity" ) || IsOption( pArg, "-maxchop" ) ? 0.000001f : -FLT_MAX;
			if ( !ParseFloatValue( pArg, argv[++i], minValue, FLT_MAX, ignoredValue ) )
				return false;
			if ( !loggedIgnored ) { Msg( "Ignoring legacy VRAD options not used by ReSTIR.\n" ); loggedIgnored = true; }
			continue;
		}
		if ( IsOption( pArg, "-ambient" ) )
		{
			if ( i + 3 >= argc )
			{
				Warning( "Error: expected three color values after '-ambient'\n" );
				return false;
			}
			for ( int component = 0; component < 3; ++component )
			{
				float ignoredValue = 0.0f;
				if ( !ParseFloatValue( pArg, argv[++i], -FLT_MAX, FLT_MAX, ignoredValue ) )
					return false;
			}
			if ( !loggedIgnored ) { Msg( "Ignoring legacy VRAD options not used by ReSTIR.\n" ); loggedIgnored = true; }
			continue;
		}

		// Legacy options whose value-taking behavior follows vrad.cpp:2380-2800.
		if ( IsOption( pArg, "-extrasky" ) || IsOption( pArg, "-threads" ) || IsOption( pArg, "-bounce" ) ||
			IsOption( pArg, "-chop" ) || IsOption( pArg, "-dispchop" ) || IsOption( pArg, "-disppatchradius" ) ||
			IsOption( pArg, "-softsun" ) || IsOption( pArg, "-maxdispsamplesize" ) || IsOption( pArg, "-StaticPropSampleScale" ) )
		{
			if ( i + 1 >= argc )
			{
				Warning( "Error: expected a value after '%s'\n", pArg );
				return false;
			}
			if ( IsOption( pArg, "-threads" ) || IsOption( pArg, "-bounce" ) )
			{
				int ignoredValue = 0;
				const int minValue = IsOption( pArg, "-threads" ) ? 1 : 0;
				if ( !ParseIntValue( pArg, argv[++i], minValue, 65535, ignoredValue ) )
					return false;
			}
			else
			{
				float ignoredValue = 0.0f;
				float minValue = 0.000001f;
				float maxValue = FLT_MAX;
				if ( IsOption( pArg, "-extrasky" ) ) minValue = 0.0f;
				else if ( IsOption( pArg, "-chop" ) || IsOption( pArg, "-dispchop" ) ) minValue = 1.0f;
				else if ( IsOption( pArg, "-disppatchradius" ) ) minValue = 10.0f;
				else if ( IsOption( pArg, "-softsun" ) ) { minValue = 0.0f; maxValue = 180.0f; }
				if ( !ParseFloatValue( pArg, argv[++i], minValue, maxValue, ignoredValue ) )
					return false;
			}
			if ( !loggedIgnored ) { Msg( "Ignoring legacy VRAD options not used by ReSTIR.\n" ); loggedIgnored = true; }
			continue;
		}
		if ( IsMPIOptionWithValue( pArg ) )
		{
			if ( !ConsumeIgnoredValue( argc, argv, i, pArg, true ) ) return false;
			if ( !loggedIgnored ) { Msg( "Ignoring legacy VRAD options not used by ReSTIR.\n" ); loggedIgnored = true; }
			continue;
		}
		if ( !Q_strnicmp( pArg, "-mpi", 4 ) || IsIgnoredOption( pArg ) )
		{
			if ( !loggedIgnored ) { Msg( "Ignoring legacy VRAD options not used by ReSTIR.\n" ); loggedIgnored = true; }
			continue;
		}

		Warning( "Error: unknown option '%s'\n", pArg );
		return false;
	}

	if ( mapArg == -1 )
	{
		Warning( "Error: no BSP/map file was supplied\n" );
		return false;
	}
	if ( !explicitMode )
		options.hdr = false;
	ApplyPreset( options, explicitOptions );
	if ( options.shadowMaps )
	{
		options.staticPropLighting = true;
		options.textureShadows = true;
		s_BakeBothModes = true;
		Msg( "Shadowmaps: implies -both\n" );
		Msg( "Shadowmaps: implies -StaticPropLighting=true\n" );
		Msg( "Shadowmaps: implies -TextureShadows=true\n" );
		if ( options.lightmapScale != 1.0f )
		{
			Warning( "Hlight: -restir_shadowmaps retains the native BSP grid; use -restir_hlight_density, not -restir_lightmapscale\n" );
			return false;
		}
		Msg( "Hlight: independent interval density=%d; native BSP grid unchanged\n", options.highresDensity );
	}

	char mapPath[MAX_PATH];
	Q_strncpy( mapPath, argv[mapArg], sizeof( mapPath ) );
	Q_DefaultExtension( mapPath, ".bsp", sizeof( mapPath ) );
	options.mapPath = mapPath;
	return true;
}

// Per-pass pre-denoise evidence: <path>.ldr.json / <path>.hdr.json; ordinary RGB is also the full source.
static bool WriteShadowMapDiagnostics( const ReSTIRScene &scene, const ReSTIRLightmapResult &result )
{
	if ( !s_ShadowMapDiagnosticsPath.Length() )
		return true;
	CUtlString diagnosticPath( s_ShadowMapDiagnosticsPath.String() );
	diagnosticPath += g_ReSTIROptions.hdr ? ".hdr.json" : ".ldr.json";
	FILE *file = fopen( diagnosticPath.String(), "wb" );
	if ( !file )
	{
		Warning( "Shadowmaps: cannot write diagnostics %s\n", diagnosticPath.String() );
		return false;
	}
	const int sunLightIndex = scene.shadowLights.Count() && scene.shadowLights[0].light.type == emit_skylight ? 0 : -1;
	fprintf( file, "{\"mode\":\"%s\",\"split\":%s,\"sunLightIndex\":%d,\"shadowSunAngularRadius\":%.9g,\"selectedLightCount\":%d",
		g_ReSTIROptions.hdr ? "hdr" : "ldr", result.sourceRadiance.Count() ? "true" : "false",
		sunLightIndex, scene.shadowSunAngularRadius, scene.shadowLights.Count() );
	fprintf(file,",\"hlightDensity\":%d,\"sampleCellCount\":%d,\"highGridCount\":%d,\"assetRGBScale\":%.9g",
		g_ReSTIROptions.shadowMaps ? g_ReSTIROptions.highresDensity : 1,scene.samples.Count(),scene.luxels.Count(),1.0/255.0);
	const CUtlVector<Vector> &source = result.sourceRadiance.Count() ? result.sourceRadiance : result.radiance;
	for ( int image = 0; image < 2; ++image )
	{
		const CUtlVector<Vector> &values = image == 0 ? source : result.radiance;
		fprintf( file, ",\"%s\":[", image == 0 ? "sourceRadiance" : "receiverRadiance" );
		for ( int i = 0; i < values.Count(); ++i )
			fprintf( file, "%s[%.9g,%.9g,%.9g]", i ? "," : "", values[i].x, values[i].y, values[i].z );
		fprintf( file, "]" );
	}
	fprintf( file, ",\"luxelValid\":[" );
	for ( int i = 0; i < result.luxelValid.Count(); ++i )
		fprintf( file, "%s%u", i ? "," : "", (unsigned int)result.luxelValid[i] );
	fprintf( file, "],\"faces\":[" );
	for ( int i = 0; i < scene.faces.Count(); ++i )
	{
		const ReSTIRGpuFace &face = scene.faces[i];
		const dface_t &native = g_pFaces[face.dface];
		fprintf( file, "%s{\"dface\":%d,\"firstOutput\":%d,\"firstLuxel\":%d,\"luxelW\":%d,\"luxelH\":%d,\"nativeW\":%d,\"nativeH\":%d,\"firstSample\":%d,\"numSamples\":%d,\"numChannels\":%d,\"numStyles\":%d,\"flags\":%d,\"styles\":[",
			i ? "," : "", face.dface, face.firstOutput, face.firstLuxel, face.luxelW, face.luxelH,
			native.m_LightmapTextureSizeInLuxels[0]+1,native.m_LightmapTextureSizeInLuxels[1]+1,
			face.firstSample,face.numSamples,face.numChannels, face.numStyles, face.flags );
		for ( int slot = 0; slot < face.numStyles; ++slot )
			fprintf( file, "%s%d", slot ? "," : "", face.styles[slot] );
		fprintf( file, "]}" );
	}
	fprintf( file, "],\"luxelPositions\":[" );
	for ( int i = 0; i < scene.luxels.Count(); ++i )
	{
		const float *position = scene.luxels[i].position;
		fprintf( file, "%s[%.9g,%.9g,%.9g]", i ? "," : "", position[0], position[1], position[2] );
	}
	fprintf(file,"],\"sunVisibility\":[");
	for (int i = 0; i < result.sunVisibility.Count(); ++i)
		fprintf(file,"%s%.9g",i ? "," : "",result.sunVisibility[i]);
	fprintf(file,"],\"sampleCells\":[");
	for (int i = 0; i < scene.samples.Count(); ++i)
	{
		const ReSTIRGpuSample &s = scene.samples[i];
		fprintf(file,"%s{\"face\":%d,\"s\":%d,\"t\":%d,\"position\":[%.9g,%.9g,%.9g],\"worldArea\":%.9g,\"coord\":[%.9g,%.9g],\"mins\":[%.9g,%.9g],\"maxs\":[%.9g,%.9g]}",
			i ? "," : "",s.face,s.s,s.t,s.position[0],s.position[1],s.position[2],s.position[3],
			s.lmCoord[0],s.lmCoord[1],s.lmCoord[2],s.lmCoord[3],s.lmMaxs[0],s.lmMaxs[1]);
	}
	fprintf( file, "]}\n" );
	const bool written = ferror( file ) == 0;
	const bool closed = fclose( file ) == 0;
	if ( !written || !closed )
		Warning( "Shadowmaps: cannot write diagnostics %s\n", diagnosticPath.String() );
	return written && closed;
}

// -restir_probe diagnostic: GPU direct/indirect at one point, plus the light table the GPU sees.
static void RunProbe( const ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	static const char *s_pTypeNames[] = { "surface", "point", "spot", "skylight", "quake", "skyambient" };
	Msg( "probe: %d GPU lights\n", scene.lights.Count() );
	for ( int i = 0; i < scene.lights.Count(); ++i )
	{
		const ReSTIRGpuLight &l = scene.lights[i];
		Msg( "  light %3d %-10s style %d intensity (%.2f %.2f %.2f) origin (%.0f %.0f %.0f) normal (%.3f %.3f %.3f) tris %d emissionTexture %d flags 0x%x attn (%.3g %.3g %.3g) radius %.0f\n",
			i, l.type >= 0 && l.type <= 5 ? s_pTypeNames[l.type] : "?", l.style, l.intensity[0], l.intensity[1], l.intensity[2],
			l.origin[0], l.origin[1], l.origin[2], l.normal[0], l.normal[1], l.normal[2], l.numTris, l.emissionTexture, (unsigned int)l.lightFlags,
			l.attenuation[0], l.attenuation[1], l.attenuation[2], l.origin[3] );
	}
	CUtlVector<ReSTIRGpuPointQuery> queries;
	ReSTIRGpuPointQuery q;
	memset( &q, 0, sizeof( q ) );
	for ( int k = 0; k < 3; ++k )
	{
		q.position[k] = g_ReSTIROptions.probe[k];
		q.normal[k] = g_ReSTIROptions.probe[3 + k];
	}
	queries.AddToTail( q );
	CUtlVector<ReSTIRGpuPointResult> results;
	if ( !device.LightPoints( queries, results ) )
	{
		Warning( "probe: LightPoints failed\n" );
		return;
	}
	for ( int s = 0; s < scene.sceneStyles.Count() && s < results.Count(); ++s )
	{
		const ReSTIRGpuPointResult &r = results[s];
		Msg( "probe: style %d direct (%.2f %.2f %.2f) indirect (%.2f %.2f %.2f)\n", scene.sceneStyles[s],
			r.direct[0], r.direct[1], r.direct[2], r.indirect[0], r.indirect[1], r.indirect[2] );
	}
}

// Port of the selected-mode face setup in utils/vrad/vrad.cpp:2223-2236.
static void SelectFaceArrayForMode( bool bHDR )
{
	if ( bHDR )
	{
		g_pFaces = dfaces_hdr;
		if ( numfaces_hdr == 0 )
		{
			numfaces_hdr = numfaces;
			memcpy( dfaces_hdr, dfaces, numfaces * sizeof( dfaces[0] ) );
		}
	}
	else
	{
		g_pFaces = dfaces;
	}
}

CVRadRestirDLL::CVRadRestirDLL()
	: m_pInterfaceDevice( NULL ), m_bFileSystemInitialized( false ), m_bBSPLoaded( false ),
	  m_bGpuInitialized( false ), m_bBakeComplete( false ), m_bInterrupted( false ), m_flProgress( 0.0f )
{
}

CVRadRestirDLL::~CVRadRestirDLL()
{
	Release();
}

// Lifecycle port based on utils/vrad/vrad.cpp:2133-2242.
bool CVRadRestirDLL::LoadSelectedBSP( const ReSTIROptions &options )
{
	if ( !m_bFileSystemInitialized )
	{
		CmdLib_InitFileSystem( options.mapPath.String() );
		m_bFileSystemInitialized = true;
	}

	const char *bspPath = options.transactionPath.Length() ? options.transactionPath.String() : options.mapPath.String();
	Msg( "Loading %s\n", bspPath );
	LoadBSPFile( bspPath );
	ParseEntities();
	SetHDRMode( options.hdr );
	VRadRestirDetailProps_SetHDRMode( options.hdr );
	SelectFaceArrayForMode( options.hdr );

	if ( g_pFullFileSystem )
	{
		g_pFullFileSystem->AddSearchPath( bspPath, "GAME", PATH_ADD_TO_HEAD );
		g_pFullFileSystem->AddSearchPath( bspPath, "MOD", PATH_ADD_TO_HEAD );
		char searchPaths[4096];
		g_pFullFileSystem->GetSearchPath( "GAME", true, searchPaths, sizeof( searchPaths ) );
		Msg( "GAME search paths: %s\n", searchPaths );
	}

	// VRAD never initializes the material system (VMTs are parsed with KeyValues, vradstaticprops.cpp:700);
	// doing so here would break the -both BSP reload with "Cannot set the shader API twice!".
	m_bBSPLoaded = true;
	return true;
}

void CVRadRestirDLL::UnloadSelectedBSP()
{
	if ( m_bBSPLoaded )
	{
		UnloadBSPFile();
		m_bBSPLoaded = false;
		g_pFaces = NULL;
	}
}

void CVRadRestirDLL::ClearState()
{
	m_bBakeComplete = false;
	m_bInterrupted = false;
	m_flProgress = 0.0f;
}

// Orchestration follows utils/vrad/vrad.cpp:2910-2953 and the module
// contract's scene/device/denoiser/output ordering.
int CVRadRestirDLL::main( int argc, char **argv )
{
	CommandLine()->CreateCmdLine( argc, argv );
	InstallAllocationFunctions();
	InstallSpewFunction();
	SetupDefaultToolsMinidumpHandler();
	MathLib_Init( 2.2f, 2.2f, 0.0f, 2.0f, false, false, false, false );
	ClearState();

	if ( !ParseRestirOptions( argc, argv, g_ReSTIROptions ) )
	{
		DeleteCmdLine( argc, argv );
		return 1;
	}
	if ( !LoadSelectedBSP( g_ReSTIROptions ) )
	{
		DeleteCmdLine( argc, argv );
		CmdLib_Cleanup();
		m_bFileSystemInitialized = false;
		return 1;
	}
	if ( !g_ReSTIROptions.shadowMaps &&
		g_GameLumps.GetGameLumpHandle( GAMELUMP_RESTIR_SHADOWMAPS ) != g_GameLumps.InvalidGameLump() )
	{
		s_BakeBothModes = true;
		g_ReSTIROptions.staticPropLighting = true;
		Msg( "Shadowmaps: converted BSP rebaked in both modes\n" );
		Msg( "Shadowmaps: converted BSP rebake implies -StaticPropLighting=true\n" );
	}
	if ( s_BakeBothModes )
	{
		// Neither completed mode is published if its paired bake fails. Keep
		// the original basename for macro/asset identity, and reload only this
		// owned sibling BSP between passes.
		char workPath[MAX_PATH * 2];
		V_snprintf(workPath,sizeof(workPath),"%s.restir-paired.%lu.bsp",
			g_ReSTIROptions.mapPath.String(),(unsigned long)GetCurrentProcessId());
		if (!CopyFileA(g_ReSTIROptions.mapPath.String(),workPath,TRUE))
		{
			Warning("Hlight: cannot create paired-bake transaction (Windows error %lu)\n",(unsigned long)GetLastError());
			UnloadSelectedBSP(); CmdLib_Cleanup(); m_bFileSystemInitialized = false;
			DeleteCmdLine(argc,argv); return 1;
		}
		g_ReSTIROptions.transactionPath = workPath;
		g_ReSTIROptions.hdr = false;
	}
	SetHDRMode( g_ReSTIROptions.hdr );
	VRadRestirDetailProps_SetHDRMode( g_ReSTIROptions.hdr );
	SelectFaceArrayForMode( g_ReSTIROptions.hdr );
	int exitCode = 0;
	for ( int pass = 0; pass < ( s_BakeBothModes ? 2 : 1 ); ++pass )
	{
		if ( pass )
		{
			g_ReSTIROptions.hdr = true;
			if ( !LoadSelectedBSP( g_ReSTIROptions ) )
			{
				exitCode = 1;
				break;
			}
		}
		Msg( "VRAD ReSTIR: baking %s mode\n", g_ReSTIROptions.hdr ? "HDR" : "LDR" );
		exitCode = BakeSelectedMode();
		if ( exitCode )
			break;
	}
	UnloadSelectedBSP();
	CmdLib_Cleanup();
	m_bFileSystemInitialized = false;
	if (g_ReSTIROptions.transactionPath.Length())
	{
		if (!exitCode && !MoveFileExA(g_ReSTIROptions.transactionPath.String(),
			g_ReSTIROptions.mapPath.String(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
		{
			Warning("Hlight: paired-bake atomic publication failed (Windows error %lu)\n",(unsigned long)GetLastError());
			exitCode = 1;
		}
		if (exitCode) DeleteFileA(g_ReSTIROptions.transactionPath.String());
		g_ReSTIROptions.transactionPath = "";
	}
	if ( exitCode )
		m_bBakeComplete = false;
	DeleteCmdLine( argc, argv );
	return exitCode;
}

int CVRadRestirDLL::BakeSelectedMode()
{
	m_bBakeComplete = false;
	m_flProgress = 0.0f;

	const double startTime = Plat_FloatTime();
	CReSTIRSceneBuilder sceneBuilder;
	ReSTIRScene scene;
	CReSTIRVulkanDevice device;
	CReSTIRDenoiser denoiser;
	ReSTIRLightmapResult result;
	bool staticPropsInitialized = false;
	bool success = false;
	const char *pFailedStage = NULL;

	// Every stage names itself on failure so a non-zero exit is never silent.
	#define RESTIR_STAGE( name, expr ) \
		if ( m_bInterrupted ) { pFailedStage = "interrupted"; goto cleanup; } \
		Msg( "VRAD ReSTIR: %s\n", name ); \
		if ( !( expr ) ) { pFailedStage = name; goto cleanup; }

	RESTIR_STAGE( "loading static props", g_ReSTIRStaticPropMgr.Init() );
	staticPropsInitialized = true;
	m_flProgress = 0.10f;
	RESTIR_STAGE( "rescaling lightmaps", ReSTIR_RescaleLightmaps( g_ReSTIROptions ) );
	RESTIR_STAGE( "building scene", sceneBuilder.Build( g_ReSTIROptions, scene ) );
	{
		int coverageCount = 0;
		int albedoCount = 0;
		int materialEmitterCount = 0;
		for ( int i = 0; i < scene.lights.Count(); ++i )
		{
			if ( scene.lights[i].lightFlags & RESTIR_LIGHT_MATERIAL )
				++materialEmitterCount;
		}
		for ( int i = 0; i < scene.textures.Count(); ++i )
		{
			if ( scene.textures[i].channels == 4 )
				++albedoCount;
			else
				++coverageCount;
		}
		Msg( "VRAD ReSTIR: %d triangles, %d materials (%d alpha, %d albedo textures), %d lights (%d exported, %d material emitters), %d emitter triangles, %d lit faces, %d samples, %d luxels\n",
			scene.triangles.Count(), scene.materials.Count(), coverageCount, albedoCount, scene.lights.Count(), scene.exportLights.Count(),
			materialEmitterCount, scene.emitterTriangles.Count(), scene.faces.Count(), scene.samples.Count(), scene.luxels.Count() );
		if ( g_ReSTIROptions.shadowMaps )
		{
			const int sunLightIndex = scene.shadowLights.Count() && scene.shadowLights[0].light.type == emit_skylight ? 0 : -1;
			Msg( "Shadowmaps: selected lights=%d sun light=%d\n", scene.shadowLights.Count(), sunLightIndex );
		}
	}
	m_flProgress = 0.25f;
	RESTIR_STAGE( "initializing Vulkan", device.Init( g_ReSTIROptions ) );
	RESTIR_STAGE( "uploading scene", device.UploadScene( scene ) );
	RESTIR_STAGE( "resolving light styles", ReSTIR_ResolveFaceStyles( scene, device ) );
	m_flProgress = 0.40f;
	RESTIR_STAGE( "baking lightmaps", device.BakeLightmaps( g_ReSTIROptions, result ) );
	RESTIR_STAGE( "writing shadowmap diagnostics", WriteShadowMapDiagnostics( scene, result ) );
	m_flProgress = 0.65f;

	if ( g_ReSTIROptions.denoiser == RESTIR_DENOISER_NONE )
	{
		denoiser.Init( g_ReSTIROptions, device.GetDeviceInfo() );
	}
	else
	{
		RESTIR_STAGE( "initializing denoiser", denoiser.Init( g_ReSTIROptions, device.GetDeviceInfo() ) );
		RESTIR_STAGE( "denoising", denoiser.Denoise( scene, result ) );
	}
	m_flProgress = 0.75f;
	RESTIR_STAGE( "uploading final lightmap", device.UploadFinalLightmap( result ) );
	if ( g_ReSTIROptions.probeEnabled )
	{
		RunProbe( scene, device );
	}

	{
		CReSTIRBSPOutput output;
		RESTIR_STAGE( "encoding lightmaps", output.EncodeLightmaps( g_ReSTIROptions, scene, result ) );
		RESTIR_STAGE( "computing leaf ambient lighting", ReSTIR_ComputeLeafAmbientLighting( g_ReSTIROptions, scene, device, result ) );
		RESTIR_STAGE( "computing static prop lighting", ReSTIR_ComputeStaticPropLighting( g_ReSTIROptions, scene, device ) );
		RESTIR_STAGE( "computing detail prop lighting", ReSTIR_ComputeDetailPropLighting( g_ReSTIROptions, scene, device ) );
		RESTIR_STAGE( "validating output", output.Validate( g_ReSTIROptions ) );
		RESTIR_STAGE( "writing BSP", output.Write( g_ReSTIROptions ) );
	}
	#undef RESTIR_STAGE
	m_flProgress = 1.0f;
	m_bBakeComplete = true;
	success = true;

cleanup:
	if ( staticPropsInitialized )
		g_ReSTIRStaticPropMgr.Shutdown();
	if ( success )
	{
		const ReSTIRDeviceInfo &info = device.GetDeviceInfo();
		const char *pBackend = info.backend == RESTIR_BACKEND_HARDWARE_RT ? "hardware-rt" : "compute-bvh";
		const char *pDenoiser = g_ReSTIROptions.denoiser == RESTIR_DENOISER_NONE ? "disabled" : denoiser.GetModeString();
		const char *pPreset = g_ReSTIROptions.preset == RESTIR_PRESET_FAST ? "fast" : ( g_ReSTIROptions.preset == RESTIR_PRESET_FINAL ? "final" : "default" );
		Msg( "VRAD ReSTIR: gpu=%s backend=%s luxels=%d ambient-samples=%d preset=%s iterations=%d candidates=%d bounces=%d denoiser=%s elapsed=%.3f s\n",
			info.deviceName.String(), pBackend, scene.luxels.Count(), g_pLeafAmbientLighting ? g_pLeafAmbientLighting->Count() : 0,
			pPreset, g_ReSTIROptions.iterations, g_ReSTIROptions.candidates, g_ReSTIROptions.maxBounces, pDenoiser, Plat_FloatTime() - startTime );
	}
	else
	{
		Warning( "VRAD ReSTIR: FAILED while %s; %s lighting mode was not written to %s.\n",
			pFailedStage ? pFailedStage : "starting", g_ReSTIROptions.hdr ? "HDR" : "LDR", g_ReSTIROptions.mapPath.String() );
	}
	denoiser.Shutdown();
	device.Shutdown();
	UnloadSelectedBSP();
	// Reopen the BSP's pak mount for the next mode without running process cleanup callbacks twice.
	CmdLib_TermFileSystem();
	m_bFileSystemInitialized = false;
	return success ? 0 : 1;
}

bool CVRadRestirDLL::Init( char const *pFilename )
{
	if ( !pFilename || !pFilename[0] )
		return false;
	ClearState();
	g_ReSTIROptions = ReSTIROptions();
	g_ReSTIROptions.mapPath = pFilename;
	char mapPath[MAX_PATH];
	Q_strncpy( mapPath, g_ReSTIROptions.mapPath.String(), sizeof( mapPath ) );
	Q_DefaultExtension( mapPath, ".bsp", sizeof( mapPath ) );
	g_ReSTIROptions.mapPath = mapPath;
	if ( !m_bFileSystemInitialized )
	{
		CmdLib_InitFileSystem( g_ReSTIROptions.mapPath.String() );
		m_bFileSystemInitialized = true;
	}
	if ( !LoadSelectedBSP( g_ReSTIROptions ) )
		return false;
	if ( m_pInterfaceDevice )
	{
		m_pInterfaceDevice->Shutdown();
		delete m_pInterfaceDevice;
		m_pInterfaceDevice = NULL;
		m_bGpuInitialized = false;
	}
	m_pInterfaceDevice = new CReSTIRVulkanDevice();
	if ( !m_pInterfaceDevice->Init( g_ReSTIROptions ) )
	{
		delete m_pInterfaceDevice;
		m_pInterfaceDevice = NULL;
		return false;
	}
	m_bGpuInitialized = true;
	return true;
}

void CVRadRestirDLL::Release()
{
	if ( m_pInterfaceDevice )
	{
		m_pInterfaceDevice->Shutdown();
		delete m_pInterfaceDevice;
		m_pInterfaceDevice = NULL;
		m_bGpuInitialized = false;
	}
	UnloadSelectedBSP();
	if ( m_bFileSystemInitialized )
	{
		CmdLib_Cleanup();
		m_bFileSystemInitialized = false;
	}
	ClearState();
}

void CVRadRestirDLL::GetBSPInfo( CBSPInfo *pInfo )
{
	if ( !pInfo )
		return;
	memset( pInfo, 0, sizeof( *pInfo ) );
	pInfo->dlightdata = pdlightdata ? pdlightdata->Base() : NULL;
	pInfo->lightdatasize = pdlightdata ? pdlightdata->Count() : 0;
	pInfo->dfaces = g_pFaces;
	pInfo->m_pFacesTouched = NULL;
	pInfo->numfaces = g_bHDR ? numfaces_hdr : numfaces;
	pInfo->dvertexes = dvertexes;
	pInfo->numvertexes = numvertexes;
	pInfo->dedges = dedges;
	pInfo->numedges = numedges;
	pInfo->dsurfedges = dsurfedges;
	pInfo->numsurfedges = numsurfedges;
	pInfo->texinfo = texinfo.Base();
	pInfo->numtexinfo = texinfo.Count();
	pInfo->g_dispinfo = g_dispinfo.Base();
	pInfo->g_numdispinfo = g_dispinfo.Count();
	pInfo->dtexdata = dtexdata;
	pInfo->numtexdata = numtexdata;
	pInfo->texDataStringData = g_TexDataStringData.Base();
	pInfo->nTexDataStringData = g_TexDataStringData.Count();
	pInfo->texDataStringTable = g_TexDataStringTable.Base();
	pInfo->nTexDataStringTable = g_TexDataStringTable.Count();
}

bool CVRadRestirDLL::DoIncrementalLight( char const *pVMFFile )
{
	(void)pVMFFile;
	Warning( "VRAD ReSTIR does not support incremental relighting; run a complete bake instead.\n" );
	return false;
}

bool CVRadRestirDLL::Serialize()
{
	return m_bBakeComplete;
}

float CVRadRestirDLL::GetPercentComplete()
{
	return m_flProgress;
}

void CVRadRestirDLL::Interrupt()
{
	m_bInterrupted = true;
}
