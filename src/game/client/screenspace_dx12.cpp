//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 screen-space effect controls and main-view scheduling.
//          Render resources and ordered GPU work belong to shaderapidx12.
//
//===========================================================================//
#include "cbase.h"
#include "screenspace_dx12.h"
#include "shadowmaps_dx12.h"
#include "upscaler_dx12.h"
#include "viewrender.h"
#include "view_shared.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "shaderapi/ishaderapidx12screenspace.h"
#include "tier1/interface.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterial.h"
#include "tier1/keyvalues.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar building_cubemaps;

static ConVar r_ssr( "r_ssr", "1", FCVAR_ARCHIVE, "DX12: screen-space reflections on reflective opaque PBR materials", true, 0, true, 1 );
static ConVar r_ssr_quality( "r_ssr_quality", "1", FCVAR_ARCHIVE, "SSR quality: 0 low, 1 balanced, 2 high", true, 0, true, 2 );
static ConVar r_ssr_resolution( "r_ssr_resolution", "1", FCVAR_ARCHIVE, "SSR trace resolution per axis relative to the quality profile: 1 unchanged, 0.5 half, 0.25 quarter; ray quality stays unchanged", true, 0.25f, true, 1.f );
static ConVar r_ssr_max_distance( "r_ssr_max_distance", "1024", FCVAR_ARCHIVE, "SSR ray distance in Source units", true, 16.f, true, 2048.f );
static ConVar r_ssr_thickness( "r_ssr_thickness", "2", FCVAR_ARCHIVE, "SSR depth thickness in Source units", true, 0.1f, true, 16.f );
static ConVar r_ssr_forward_steps( "r_ssr_forward_steps", "16", FCVAR_ARCHIVE, "SSR of translucents (car glass, windows) and viewmodels, traced in their own shaders: march steps, 0 off", true, 0, true, 32 );
static ConVar r_contactshadows( "r_contactshadows", "0", FCVAR_ARCHIVE, "DX12: Bend screen-space contact shadows for the shadowmapped sun", true, 0, true, 1 );
static ConVar r_contactshadows_debug( "r_contactshadows_debug", "0", FCVAR_CHEAT,
	"Show sun contact visibility on supported receivers: white is unoccluded. Does not enable contact shadows.", true, 0, true, 1 );
static ConVar r_contactshadows_quality( "r_contactshadows_quality", "1", FCVAR_ARCHIVE, "Sun contact-shadow samples: 0 = 16, 1 = 32, 2 = 60", true, 0, true, 2 );
static ConVar r_contactshadows_surface_thickness( "r_contactshadows_surface_thickness", "0.005", FCVAR_ARCHIVE, "Assumed surface thickness as a fraction of the remaining device-depth range", true, 0.0001f, true, 0.1f );
static ConVar r_contactshadows_edge_threshold( "r_contactshadows_edge_threshold", "0.02", FCVAR_ARCHIVE, "Relative device-depth threshold for rejecting interpolation across edges", true, 0.f, true, 1.f );
static ConVar r_contactshadows_contrast( "r_contactshadows_contrast", "4", FCVAR_ARCHIVE, "Bend sun contact-shadow contrast", true, 1.f, true, 8.f );
static ConVar r_depth_prepass( "r_depth_prepass", "1", FCVAR_ARCHIVE, "DX12: replay normal main-view opaque geometry into depth before shading (same LOD and alpha coverage)", true, 0, true, 1 );

static ConVar r_flashlight_bounce( "r_flashlight_bounce", "1", FCVAR_ARCHIVE, "Screen-space flashlight bounce lighting", true, 0, true, 1 );
static ConVar r_flashlight_bounce_scale( "r_flashlight_bounce_scale", "0.12", FCVAR_ARCHIVE, "Reflected flashlight radiance multiplier", true, 0.f, true, 4.f );
static ConVar r_flashlight_bounce_debug( "r_flashlight_bounce_debug", "0", FCVAR_CHEAT, "Show measured flashlight colour at the weighted centroid", true, 0, true, 1 );

static ConVar r_volumetric( "r_volumetric", "1", FCVAR_ARCHIVE, "DX12: additive volumetric lighting from opted-in shadowmapped lights", true, 0, true, 1 );
static ConVar r_volumetric_quality( "r_volumetric_quality", "0", FCVAR_ARCHIVE, "Volumetric quality: 0 low, 1 balanced, 2 high", true, 0, true, 2 );
static ConVar r_volumetric_density( "r_volumetric_density", "0.00025", FCVAR_ARCHIVE, "Volumetric scattering coefficient in inverse Source units", true, 0.f, true, 0.01f );
static ConVar r_volumetric_strength( "r_volumetric_strength", "1", FCVAR_ARCHIVE, "Volumetric radiance multiplier", true, 0.f, true, 8.f );
static ConVar r_volumetric_anisotropy( "r_volumetric_anisotropy", "0.2", FCVAR_ARCHIVE, "Volumetric Henyey-Greenstein phase anisotropy", true, -0.9f, true, 0.9f );
static ConVar r_volumetric_max_distance( "r_volumetric_max_distance", "2048", FCVAR_ARCHIVE, "Volumetric ray distance in Source units", true, 64.f, true, 8192.f );
static ConVar r_volumetric_sun_override( "r_volumetric_sun_override", "0", FCVAR_NONE, "Override the sun's map volumetric opt-in and medium settings", true, 0, true, 1 );
static ConVar r_volumetric_sun_enable( "r_volumetric_sun_enable", "1", FCVAR_ARCHIVE, "Enable sun volumetrics when sun override is on; valid shadow coverage is still required", true, 0, true, 1 );
static ConVar r_volumetric_sun_density( "r_volumetric_sun_density", "0.00025", FCVAR_ARCHIVE, "Sun-only volumetric scattering coefficient with sun override on", true, 0.f, true, 0.01f );
static ConVar r_volumetric_sun_strength( "r_volumetric_sun_strength", "1", FCVAR_ARCHIVE, "Sun-only volumetric radiance multiplier with sun override on", true, 0.f, true, 8.f );
static ConVar r_volumetric_sun_anisotropy( "r_volumetric_sun_anisotropy", "0.2", FCVAR_ARCHIVE, "Sun-only volumetric phase anisotropy with sun override on", true, -0.9f, true, 0.9f );
static ConVar r_volumetric_sun_max_distance( "r_volumetric_sun_max_distance", "2048", FCVAR_ARCHIVE, "Sun-only volumetric ray distance with sun override on, clipped to valid shadow coverage", true, 64.f, true, 8192.f );

namespace
{
IShaderAPIDX12ScreenSpace *s_pScreenSpace = nullptr;
CTextureReference s_SkyColor, s_SkyDepth;
CMaterialReference s_SkyComposite;
ConVar r_sky_halfres( "r_sky_halfres", "0", FCVAR_ARCHIVE, "Shade deferred main-view 3D sky at half internal resolution" );
bool s_bVolumetricScope = false;
DX12VolumetricSettings s_VolumetricSettings = {};
const uint32 kContactSteps[3] = { 16, 32, 60 };
const uint32 kVolumetricSteps[3] = { 8, 16, 32 };
}

void ScreenSpaceDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_pScreenSpace = nullptr;
	s_bVolumetricScope = false;
	s_VolumetricSettings = DX12VolumetricSettings{};
	if ( !shaderDLL || V_stricmp( shaderDLL, "stdshader_dx12" ) )
		return;

	CreateInterfaceFn factory = Sys_GetFactory( "shaderapidx12" );
	s_pScreenSpace = factory ? static_cast<IShaderAPIDX12ScreenSpace *>( factory( SHADERAPIDX12_SCREENSPACE_INTERFACE_VERSION, nullptr ) ) : nullptr;
}

void ScreenSpaceDX12_CreateSkyTargets( IMaterialSystem *pMaterials )
{
	if ( !s_pScreenSpace )
		return;
	int w, h;
	pMaterials->GetBackBufferDimensions( w, h );
	w = MAX( 1, ( w + 1 ) / 2 );
	h = MAX( 1, ( h + 1 ) / 2 );
	s_SkyColor.InitRenderTarget( w, h, RT_SIZE_LITERAL, IMAGE_FORMAT_RGBA16161616F, MATERIAL_RT_DEPTH_NONE, false, "_rt_HalfSkyColor" );
	s_SkyDepth.InitRenderTarget( w, h, RT_SIZE_LITERAL, pMaterials->GetShadowDepthTextureFormat(), MATERIAL_RT_DEPTH_NONE, false, "_rt_HalfSkyDepth" );
	KeyValues *kv = new KeyValues( "DX12_PostFXRcas" );
	kv->SetString( "$basetexture", "_rt_HalfSkyColor" );
	kv->SetFloat( "$sharpness", -1.f );
	IMaterial *material = pMaterials->CreateMaterial( "dx12/half_sky_composite", kv );
	s_SkyComposite.Init( material );
	if ( material )
		material->DecrementReferenceCount();
}

bool ScreenSpaceDX12_HalfSky( int w, int h, ITexture *&color, ITexture *&depth, IMaterial *&composite )
{
	static ConVarRef wireframe( "mat_wireframe" ), fullbright( "mat_fullbright" ), postDebug( "mat_postfx_debug" );
	static ConVarRef normals( "mat_pbr_showgbuffer" ), histogram( "mat_show_histogram" ), dump( "mat_dump_rts" );
	if ( !r_sky_halfres.GetBool() || wireframe.GetInt() || fullbright.GetInt() || postDebug.GetInt() ||
		normals.GetInt() || histogram.GetBool() || dump.GetBool() ||
		ScreenSpaceDX12_ContactDebug() || !s_SkyColor.IsValid() || !s_SkyDepth.IsValid() || !s_SkyComposite.IsValid() ||
		s_SkyColor->IsError() || s_SkyDepth->IsError() || s_SkyComposite->IsErrorMaterial() ||
		w > s_SkyColor->GetActualWidth() || h > s_SkyColor->GetActualHeight() ||
		w > s_SkyDepth->GetActualWidth() || h > s_SkyDepth->GetActualHeight() )
		return false;
	color = s_SkyColor;
	depth = s_SkyDepth;
	composite = s_SkyComposite;
	return true;
}

void ScreenSpaceDX12_Shutdown()
{
	s_SkyComposite.Shutdown();
	s_SkyColor.Shutdown();
	s_SkyDepth.Shutdown();
	if ( s_bVolumetricScope )
		ShadowMapsDX12_EndVolumetrics();
	s_bVolumetricScope = false;
	s_VolumetricSettings = DX12VolumetricSettings{};
	s_pScreenSpace = nullptr;
}

bool ScreenSpaceDX12_Available()
{
	return s_pScreenSpace != nullptr;
}

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_GetAPI()
{
	return s_pScreenSpace;
}

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_DepthPrepass()
{
	return r_depth_prepass.GetBool() ? s_pScreenSpace : nullptr;
}

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_ViewModelContacts()
{
	static ConVarRef compact( "r_pbr_lean_lighting", true );
	return r_contactshadows.GetBool() && !( compact.IsValid() && compact.GetBool() ) ? s_pScreenSpace : nullptr;
}

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_ViewModelBounce()
{
	return r_flashlight_bounce.GetBool() && r_flashlight_bounce_scale.GetFloat() > 0.f ? s_pScreenSpace : nullptr;
}

bool ScreenSpaceDX12_ContactDebug()
{
	return s_pScreenSpace && r_contactshadows_debug.GetBool();
}

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_BeginView( IMatRenderContext *pRenderContext, const Vector &eye, const Vector &forward, bool &contacts, bool &ssr )
{
	contacts = false;
	ssr = false;
	if ( !s_pScreenSpace || !pRenderContext )
		return nullptr;

	contacts = r_contactshadows.GetBool();
	const bool debugContacts = ScreenSpaceDX12_ContactDebug();
	ssr = !debugContacts && r_ssr.GetBool();
	const bool bounce = r_flashlight_bounce.GetBool() && !debugContacts;
	if ( !contacts && !debugContacts && !ssr && !s_bVolumetricScope && !bounce )
		return nullptr;

	int x, y, width, height;
	pRenderContext->GetViewport( x, y, width, height );
	DX12ScreenSpaceViewDesc view = {};
	view.constants.eyeLength[0] = eye.x;
	view.constants.eyeLength[1] = eye.y;
	view.constants.eyeLength[2] = eye.z;
	// The legacy eye/forward .w lanes are reserved for the backend's forward SSR settings.
	view.constants.forwardThickness[0] = forward.x;
	view.constants.forwardThickness[1] = forward.y;
	view.constants.forwardThickness[2] = forward.z;
	view.constants.contactParams[0] = r_contactshadows_surface_thickness.GetFloat();
	view.constants.contactParams[1] = r_contactshadows_edge_threshold.GetFloat();
	view.constants.contactParams[2] = r_contactshadows_contrast.GetFloat();
	view.constants.viewport[0] = static_cast<float>( x );
	view.constants.viewport[1] = static_cast<float>( y );
	view.constants.viewport[2] = static_cast<float>( width );
	view.constants.viewport[3] = static_cast<float>( height );
	view.constants.control[3] = ssr ? 1u : 0u;
	view.constants.depth[1] = 0.25f;
	view.constants.depth[2] = 0.5f;
	view.constants.depth[3] = 8.f;
	view.constants.contactControl[0] = contacts ? 1u : 0u;
	view.constants.contactControl[2] = kContactSteps[r_contactshadows_quality.GetInt()];
	view.constants.contactControl[3] = debugContacts ? 1u : 0u;
	view.bounceScale = bounce ? r_flashlight_bounce_scale.GetFloat() : 0.f;
	view.bounceDebug = bounce && r_flashlight_bounce_debug.GetBool();
	view.ssr.quality = static_cast<uint32>( r_ssr_quality.GetInt() );
	view.ssr.maxDistance = r_ssr_max_distance.GetFloat();
	view.ssr.thickness = r_ssr_thickness.GetFloat();
	view.ssr.resolutionScale = r_ssr_resolution.GetFloat();
	view.ssr.forwardSteps = static_cast<uint32>( r_ssr_forward_steps.GetInt() );
	// Matrices, sample count and depth clear are filled on ordered backend replay.
	// Submit zero-sized views too: a rejected latest main bracket must invalidate a volume capture.
	s_pScreenSpace->BeginView( view );
	return s_pScreenSpace;
}

void ScreenSpaceDX12_BeginVolumetrics( const CViewSetup &view )
{
	if ( !s_pScreenSpace || ScreenSpaceDX12_ContactDebug() || !r_volumetric.GetBool() || CurrentViewID() != VIEW_MAIN ||
		view.m_eStereoEye != STEREO_EYE_MONO || building_cubemaps.GetBool() || g_pIntroData )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	// NULL denotes the native scene target. The backend also checks the actual scene color/depth pair.
	if ( pRenderContext->GetRenderTarget() != nullptr )
		return;
	pRenderContext.SafeRelease();

	DX12VolumetricSettings settings = {};
	settings.medium[0] = r_volumetric_density.GetFloat();
	settings.medium[1] = r_volumetric_strength.GetFloat();
	settings.medium[2] = r_volumetric_anisotropy.GetFloat();
	settings.medium[3] = r_volumetric_max_distance.GetFloat();
	const bool bSunOverride = r_volumetric_sun_override.GetBool();
	if ( bSunOverride )
	{
		settings.sunMedium[0] = r_volumetric_sun_density.GetFloat();
		settings.sunMedium[1] = r_volumetric_sun_strength.GetFloat();
		settings.sunMedium[2] = r_volumetric_sun_anisotropy.GetFloat();
		settings.sunMedium[3] = r_volumetric_sun_max_distance.GetFloat();
	}
	else
	{
		for ( int i = 0; i < ARRAYSIZE( settings.medium ); ++i )
			settings.sunMedium[i] = settings.medium[i];
	}
	const bool bSunEnabled = bSunOverride ? r_volumetric_sun_enable.GetBool() : ShadowMapsDX12_VolumetricSunEnabled();
	settings.control[0] = kVolumetricSteps[r_volumetric_quality.GetInt()];
	settings.control[1] = bSunEnabled && settings.sunMedium[0] > 0.f && settings.sunMedium[1] > 0.f ? 1u : 0u;
	settings.control[2] = 1u;
	// All volume paths use the same rotating 64-frame IGN sequence. It is
	// deliberately independent of the selected AA provider so quarter-res
	// shaft integration is not replaced by a provider-specific pattern.
	settings.control[3] = static_cast<uint32>( gpGlobals->framecount & 63u );
	s_VolumetricSettings = settings;
	s_bVolumetricScope = true;
	ShadowMapsDX12_BeginVolumetrics();
	s_pScreenSpace->BeginVolumetrics( settings );
}

void ScreenSpaceDX12_DrawVolumetrics()
{
	if ( !s_bVolumetricScope )
		return;

	s_pScreenSpace->DrawVolumetrics();
	ShadowMapsDX12_EndVolumetrics();
	s_bVolumetricScope = false;
	s_VolumetricSettings = DX12VolumetricSettings{};
}

const DX12VolumetricSettings *ScreenSpaceDX12_GetVolumetricSettings()
{
	return s_bVolumetricScope ? &s_VolumetricSettings : nullptr;
}
