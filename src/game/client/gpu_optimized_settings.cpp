//========= Copyright Valve Corporation, All rights reserved. ============//
#include "cbase.h"
#include "filesystem.h"
#include "gpu_optimized_settings.h"
#include "gpu_optimized_settings_model.h"
#include "shaderapi/ishaderapidx12.h"
#include "tier1/KeyValues.h"
#include "tier1/fmtstr.h"
#ifdef L4D2
#include "l4d2/ui/basemodui/terror_basemodui.h"
#endif
#include "tier0/memdbgon.h"

namespace GpuOptimizedSettings
{
static ConVar r_quality_tier_adapter( "r_quality_tier_adapter", "", FCVAR_ARCHIVE,
	"Adapter (vendor:device) last set up by optimized settings; clear it to apply again on the next level" );

static const char *const s_PresetNames[] = { "Low", "Medium", "High", "Ultra" };
static const char *const s_PresetTokens[] =
{
	"#GraphicsOptions_Low", "#GraphicsOptions_Medium", "#GraphicsOptions_High", "#GraphicsOptions_Ultra"
};
static_assert( ARRAYSIZE( s_PresetNames ) == PRESET_UNKNOWN && ARRAYSIZE( s_PresetTokens ) == PRESET_UNKNOWN,
	"Every preset needs a name and localization token" );

const char *PresetToken( Preset_t preset )
{
	return s_PresetTokens[preset];
}

// These choices target 1080p output. Only Low on the GTX 1050 (Time Spy 1739,
// 60 fps at 1600x900 internal with TAAU) was measured. The higher thresholds are
// conservative benchmark-ratio estimates, not measurements or an FPS promise.
static Preset_t SelectPreset( float timeSpyEquivalent )
{
	const float kMeasuredTimeSpy = 1739.0f;
	if ( timeSpyEquivalent >= 8.0f * kMeasuredTimeSpy )
		return PRESET_ULTRA;
	if ( timeSpyEquivalent >= 3.0f * kMeasuredTimeSpy )
		return PRESET_HIGH;
	if ( timeSpyEquivalent >= 1.5f * kMeasuredTimeSpy )
		return PRESET_MEDIUM;
	return PRESET_LOW;
}

struct Setting_t
{
	const char *m_pszCvar;
	const char *m_pszValues[PRESET_ULTRA];
};

// Ultra uses the registered (shipped high-tier) defaults for these cvars,
// not the highest menu choice. No unrelated settings are changed. The Low
// column is the original measured low-tier table; NULL leaves a cvar alone.
static const Setting_t s_Settings[] =
{
	//                                             Low       Medium    High
	{ "r_upscaler",                               { "6",      "6",      "6" } },
	// 0.8334 rather than 0.833: truncation must land on exactly 1600x900 at 1920x1080.
	{ "r_upscaler_scale",                         { "0.8334", "0.8334", "1" } },
	{ "r_ssr",                                    { "0",      "1",      "1" } },
	{ "r_volumetric",                             { "0",      "0",      "1" } },
	{ "r_ssr_quality",                            { "1",      "0",      "1" } },
	{ "r_ssr_resolution",                         { "0.5",    "0.5",    "0.5" } },
	{ "r_gtao_resolution",                         { "0.25",   "0.5",    "0.5" } },
	{ "r_gtao_slices",                             { "1",      "2",      "2" } },
	{ "r_gtao_steps",                              { "2",      "3",      "3" } },
	{ "r_gtao_denoise",                            { "1",      "1",      "1" } },
	{ "r_pbr_lean_lighting",                       { "1",      "1",      "0" } },
	{ "r_pbr_light_passes",                        { "4",      "4",      "1" } },
	{ "r_pbr_leaf_lighting",                       { "1",      "1",      "0" } },
	{ "r_csm_cascades",                            { "1",      "1",      "2" } },
	{ "r_shadowmap_viewmodel_self",                { "0",      "0",      "0" } },
	{ "r_shadowmap_filter",                        { "0",      "0",      "1" } },
	{ "r_shadowmap_filter_quality",                { "0",      "0",      "1" } },
	{ "r_shadowmap_resolution",                    { "0",      "1",      "1" } },
	{ "r_shadowmap_max_realtime_lights",           { "1",      "2",      "2" } },
	{ "mat_postfx_bloom_quality",                  { "0",      "1",      "2" } },
	{ "mat_motion_blur_quality",                   { "-1",     "0",      "1" } },
	{ "r_volumetric_quality",                      { NULL,     "0",      "0" } },
};

struct Selection_t
{
	Recommendation_t m_Recommendation;
	MaterialAdapterInfo_t m_Adapter;
	uint64 m_nDedicated;
	GpuModel::Result m_Result;
};

static IShaderAPIDX12 *GetRenderer()
{
	CreateInterfaceFn factory = Sys_GetFactory( "shaderapidx12" );
	IShaderAPIDX12 *renderer = factory ? static_cast<IShaderAPIDX12 *>( factory( SHADERAPIDX12_INTERFACE_VERSION, NULL ) ) : NULL;
	return renderer && renderer->AdapterPerformanceTier() != SHADERAPIDX12_ADAPTER_TIER_UNKNOWN ? renderer : NULL;
}

static unsigned int VendorID( const char *vendor )
{
	if ( !V_stricmp( vendor, "Nvidia" ) ) return 0x10de;
	if ( !V_stricmp( vendor, "AMD" ) ) return 0x1002;
	if ( !V_stricmp( vendor, "Intel" ) ) return 0x8086;
	if ( !V_stricmp( vendor, "Apple" ) ) return 0x106b;
	return 0;
}

static bool GetSelection( Selection_t &selection )
{
	IShaderAPIDX12 *renderer = GetRenderer();
	if ( !renderer || !materials )
		return false;

	// MaterialSystem exposes the selected adapter's DXGI description and IDs.
	// Dedicated bytes, not the dynamic OS budget, distinguish board variants.
	materials->GetDisplayAdapterInfo( materials->GetCurrentAdapter(), selection.m_Adapter );
	uint64 usage = 0, budget = 0;
	renderer->GetVideoMemory( usage, budget, selection.m_nDedicated );

	// The database is shipped content. Reuse the parsed result for this device
	// on menu reopen and command queries; rebuild if the adapter changes.
	static Selection_t cached = {};
	static bool cachedValid = false;
	if ( cachedValid && cached.m_nDedicated == selection.m_nDedicated &&
		cached.m_Adapter.m_VendorID == selection.m_Adapter.m_VendorID &&
		cached.m_Adapter.m_DeviceID == selection.m_Adapter.m_DeviceID &&
		!V_strcmp( cached.m_Adapter.m_pDriverName, selection.m_Adapter.m_pDriverName ) )
	{
		selection = cached;
		return true;
	}

	Recommendation_t &recommendation = selection.m_Recommendation;
	V_strncpy( recommendation.m_szAdapter, selection.m_Adapter.m_pDriverName, sizeof( recommendation.m_szAdapter ) );

	KeyValues::AutoDelete database( new KeyValues( "GPUDatabase" ) );
	CUtlVector<GpuModel::Row> rows;
	if ( database->LoadFromFile( filesystem, "scripts/gpu_database.txt", "MOD" ) )
	{
		KeyValues *gpus = database->FindKey( "GPUs" );
		for ( KeyValues *entry = gpus ? gpus->GetFirstTrueSubKey() : NULL; entry; entry = entry->GetNextTrueSubKey() )
		{
			GpuModel::Row row = { VendorID( entry->GetString( "vendor" ) ), entry->GetName(),
				entry->GetFloat( "timespy" ), entry->GetFloat( "gaming" ), entry->GetFloat( "tflops" ) };
			rows.AddToTail( row );
		}
	}
	selection.m_Result = GpuModel::Recommend( rows.Base(), rows.Count(), selection.m_Adapter.m_VendorID,
		recommendation.m_szAdapter, static_cast<unsigned int>( selection.m_nDedicated >> 20 ) );
	const GpuModel::Result &result = selection.m_Result;
	recommendation.m_Preset = SelectPreset( result.m_flTimeSpyEquivalent );
	switch ( result.m_Source )
	{
	case GpuModel::SOURCE_TIMESPY:
	case GpuModel::SOURCE_GAMING:
	case GpuModel::SOURCE_TFLOPS:
		recommendation.m_Source = SOURCE_DATABASE;
		V_strncpy( recommendation.m_szEntry, rows[result.m_nRow].m_pszName, sizeof( recommendation.m_szEntry ) );
		break;
	case GpuModel::SOURCE_ESTIMATE:
		recommendation.m_Source = SOURCE_ESTIMATE;
		break;
	case GpuModel::SOURCE_INTEGRATED:
		recommendation.m_Source = SOURCE_INTEGRATED;
		break;
	default:
		recommendation.m_Source = SOURCE_UNKNOWN;
		recommendation.m_Preset = PRESET_LOW;
		break;
	}
	cached = selection;
	cachedValid = true;
	return true;
}

bool GetRecommendation( Recommendation_t &out )
{
	Selection_t selection = {};
	if ( !GetSelection( selection ) )
		return false;
	out = selection.m_Recommendation;
	return true;
}

static void Print( const Selection_t &selection, bool applied )
{
	const Recommendation_t &recommendation = selection.m_Recommendation;
	const GpuModel::Result &result = selection.m_Result;
	const char *source;
	switch ( result.m_Source )
	{
	case GpuModel::SOURCE_TIMESPY: source = "Time Spy"; break;
	case GpuModel::SOURCE_GAMING: source = "gaming score"; break;
	case GpuModel::SOURCE_TFLOPS: source = "FP32 TFLOPS"; break;
	case GpuModel::SOURCE_ESTIMATE: source = "estimated Time Spy"; break;
	case GpuModel::SOURCE_INTEGRATED: source = "integrated graphics estimate"; break;
	default: source = "unidentified"; break;
	}
	Msg( "DX12 optimized settings: %s (%04x:%04x, %u MiB) -> %s -> %s%s\n",
		recommendation.m_szAdapter, selection.m_Adapter.m_VendorID, selection.m_Adapter.m_DeviceID,
		static_cast<unsigned int>( selection.m_nDedicated >> 20 ),
		recommendation.m_szEntry[0] ? recommendation.m_szEntry : source,
		s_PresetNames[recommendation.m_Preset], applied ? " (applied)" : "" );
	if ( recommendation.m_Source != SOURCE_UNKNOWN )
		Msg( "  %s: %.2f; mobile factor %.2f; Time Spy equivalent %.0f\n",
			source, result.m_flScore, result.m_flMobileFactor, result.m_flTimeSpyEquivalent );
}

static void Apply( const Selection_t &selection )
{
	const Preset_t preset = selection.m_Recommendation.m_Preset;
	for ( int i = 0; i < ARRAYSIZE( s_Settings ); ++i )
	{
		const Setting_t &setting = s_Settings[i];
		ConVar *var = g_pCVar->FindVar( setting.m_pszCvar );
		if ( !var )
			continue;
		if ( preset == PRESET_ULTRA )
			var->Revert();
		else if ( setting.m_pszValues[preset] )
			var->SetValue( setting.m_pszValues[preset] );
	}
	r_quality_tier_adapter.SetValue( CFmtStr( "%04x:%04x", selection.m_Adapter.m_VendorID, selection.m_Adapter.m_DeviceID ).Get() );
	Print( selection, true );
}

bool ApplyRecommended()
{
	Selection_t selection = {};
	if ( !GetSelection( selection ) )
		return false;
	Apply( selection );
	return true;
}

#ifdef L4D2
static bool s_bCheckedAdapter = false;
static bool s_bWarnUnidentified = false;
#endif

void ApplyAdapterQualityTier()
{
	if ( !GetRenderer() || !materials )
		return;
	MaterialAdapterInfo_t adapter = {};
	materials->GetDisplayAdapterInfo( materials->GetCurrentAdapter(), adapter );
#ifdef L4D2
	s_bCheckedAdapter = true;
#endif
	const CFmtStr adapterId( "%04x:%04x", adapter.m_VendorID, adapter.m_DeviceID );
	if ( !V_stricmp( r_quality_tier_adapter.GetString(), adapterId.Get() ) )
		return;

	Selection_t selection = {};
	if ( !GetSelection( selection ) )
		return;
	Apply( selection );
#ifdef L4D2
	s_bWarnUnidentified = selection.m_Recommendation.m_Source == SOURCE_UNKNOWN;
#endif
}

void OnMenuReady()
{
#ifdef L4D2
	// A menu with no background level still needs first-boot defaults. The
	// native main menu calls this once the engine has opened it after startup.
	if ( !s_bCheckedAdapter )
	{
		s_bCheckedAdapter = true;
		ApplyAdapterQualityTier();
	}
	// Defer the warning until loading finishes and the UI can accept input.
	// The archived adapter ID prevents it recurring after the first setup.
	if ( s_bWarnUnidentified && ShowGpuIdentificationWarning() )
		s_bWarnUnidentified = false;
#endif
}

static void CC_OptimizedSettings( const CCommand &args )
{
	const bool apply = args.ArgC() == 2 && !V_stricmp( args[1], "apply" );
	if ( args.ArgC() > 1 && !apply )
	{
		Msg( "Usage: r_optimized_settings [apply]\n" );
		return;
	}
	Selection_t selection = {};
	if ( !GetSelection( selection ) )
	{
		Msg( "DX12 optimized settings: no active DX12 adapter.\n" );
		return;
	}
	if ( apply )
	{
		Apply( selection );
		engine->ClientCmd_Unrestricted( "host_writeconfig\n" );
	}
	else
	{
		Print( selection, false );
	}
}
static ConCommand r_optimized_settings( "r_optimized_settings", CC_OptimizedSettings,
	"Show the detected DX12 GPU and recommended preset, or apply it with r_optimized_settings apply", 0 );
}
