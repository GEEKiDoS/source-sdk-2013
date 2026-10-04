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

ReSTIROptions g_ReSTIROptions;
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
			// The launcher replaces this token with -ldr/-hdr for each pass. Keep
			// direct DLL invocation deterministic by treating it as the LDR pass.
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
		if ( IsOption( pArg, "-StaticPropLighting" ) )
		{
			options.staticPropLighting = true;
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

	char mapPath[MAX_PATH];
	Q_strncpy( mapPath, argv[mapArg], sizeof( mapPath ) );
	Q_DefaultExtension( mapPath, ".bsp", sizeof( mapPath ) );
	options.mapPath = mapPath;
	return true;
}

// -restir_probe diagnostic: GPU direct/indirect at one point, plus the light table the GPU sees.
static void RunProbe( const ReSTIRScene &scene, CReSTIRVulkanDevice &device )
{
	static const char *s_pTypeNames[] = { "surface", "point", "spot", "skylight", "quake", "skyambient" };
	Msg( "probe: %d GPU lights\n", scene.lights.Count() );
	for ( int i = 0; i < scene.lights.Count(); ++i )
	{
		const ReSTIRGpuLight &l = scene.lights[i];
		Msg( "  light %3d %-10s style %d intensity (%.2f %.2f %.2f) origin (%.0f %.0f %.0f) normal (%.3f %.3f %.3f) tris %d attn (%.3g %.3g %.3g) radius %.0f\n",
			i, l.type >= 0 && l.type <= 5 ? s_pTypeNames[l.type] : "?", l.style, l.intensity[0], l.intensity[1], l.intensity[2],
			l.origin[0], l.origin[1], l.origin[2], l.normal[0], l.normal[1], l.normal[2], l.numTris,
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

	Msg( "Loading %s\n", options.mapPath.String() );
	LoadBSPFile( options.mapPath.String() );
	ParseEntities();
	SetHDRMode( options.hdr );
	VRadRestirDetailProps_SetHDRMode( options.hdr );
	SelectFaceArrayForMode( options.hdr );

	if ( g_pFullFileSystem )
	{
		g_pFullFileSystem->AddSearchPath( options.mapPath.String(), "GAME", PATH_ADD_TO_HEAD );
		g_pFullFileSystem->AddSearchPath( options.mapPath.String(), "MOD", PATH_ADD_TO_HEAD );
		char searchPaths[4096];
		g_pFullFileSystem->GetSearchPath( "GAME", true, searchPaths, sizeof( searchPaths ) );
		Msg( "GAME search paths: %s\n", searchPaths );
	}

	// VRAD never initializes the material system (VMTs are parsed with KeyValues, vradstaticprops.cpp:700);
	// doing so here would break the launcher's -both reload with "Cannot set the shader API twice!".
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
	Msg( "VRAD ReSTIR: %d triangles, %d materials, %d lights (%d exported), %d lit faces, %d samples, %d luxels\n",
		scene.triangles.Count(), scene.materials.Count(), scene.lights.Count(), scene.exportLights.Count(),
		scene.faces.Count(), scene.samples.Count(), scene.luxels.Count() );
	m_flProgress = 0.25f;
	RESTIR_STAGE( "initializing Vulkan", device.Init( g_ReSTIROptions ) );
	RESTIR_STAGE( "uploading scene", device.UploadScene( scene ) );
	m_flProgress = 0.40f;
	RESTIR_STAGE( "baking lightmaps", device.BakeLightmaps( g_ReSTIROptions, result ) );
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
		Warning( "VRAD ReSTIR: FAILED while %s; %s was not modified.\n", pFailedStage ? pFailedStage : "starting", g_ReSTIROptions.mapPath.String() );
	}
	denoiser.Shutdown();
	device.Shutdown();
	UnloadSelectedBSP();
	DeleteCmdLine( argc, argv );
	CmdLib_Cleanup();
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
