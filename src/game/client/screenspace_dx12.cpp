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

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

extern ConVar building_cubemaps;

static ConVar r_ssr( "r_ssr", "0", FCVAR_ARCHIVE, "DX12: screen-space reflections on reflective opaque PBR materials", true, 0, true, 1 );
static ConVar r_ssr_quality( "r_ssr_quality", "1", FCVAR_ARCHIVE, "SSR quality: 0 low, 1 balanced, 2 high", true, 0, true, 2 );
static ConVar r_ssr_max_distance( "r_ssr_max_distance", "512", FCVAR_ARCHIVE, "SSR ray distance in Source units", true, 16.f, true, 2048.f );
static ConVar r_ssr_thickness( "r_ssr_thickness", "2", FCVAR_ARCHIVE, "SSR depth thickness in Source units", true, 0.1f, true, 16.f );
static ConVar r_contactshadows( "r_contactshadows", "0", FCVAR_ARCHIVE, "DX12: screen-space contact shadows for shadowmapped direct lights", true, 0, true, 1 );
static ConVar r_contactshadows_quality( "r_contactshadows_quality", "1", FCVAR_ARCHIVE, "Contact-shadow quality: 0 low, 1 balanced, 2 high", true, 0, true, 2 );
static ConVar r_contactshadows_length( "r_contactshadows_length", "16", FCVAR_ARCHIVE, "Contact-shadow ray distance in Source units", true, 1.f, true, 64.f );
static ConVar r_contactshadows_thickness( "r_contactshadows_thickness", "1", FCVAR_ARCHIVE, "Contact-shadow depth thickness in Source units", true, 0.1f, true, 8.f );

static ConVar r_volumetric( "r_volumetric", "0", FCVAR_ARCHIVE, "DX12: additive volumetric lighting from opted-in shadowmapped lights", true, 0, true, 1 );
static ConVar r_volumetric_quality( "r_volumetric_quality", "1", FCVAR_ARCHIVE, "Volumetric quality: 0 low, 1 balanced, 2 high", true, 0, true, 2 );
static ConVar r_volumetric_density( "r_volumetric_density", "0.00025", FCVAR_ARCHIVE, "Volumetric scattering coefficient in inverse Source units", true, 0.f, true, 0.01f );
static ConVar r_volumetric_strength( "r_volumetric_strength", "1", FCVAR_ARCHIVE, "Volumetric radiance multiplier", true, 0.f, true, 8.f );
static ConVar r_volumetric_anisotropy( "r_volumetric_anisotropy", "0.2", FCVAR_ARCHIVE, "Volumetric Henyey-Greenstein phase anisotropy", true, -0.9f, true, 0.9f );
static ConVar r_volumetric_max_distance( "r_volumetric_max_distance", "2048", FCVAR_ARCHIVE, "Volumetric ray distance in Source units", true, 64.f, true, 8192.f );
static ConVar r_volumetric_sun_override( "r_volumetric_sun_override", "0", FCVAR_ARCHIVE, "Override the sun's map volumetric opt-in and medium settings", true, 0, true, 1 );
static ConVar r_volumetric_sun_enable( "r_volumetric_sun_enable", "1", FCVAR_ARCHIVE, "Enable sun volumetrics when sun override is on; valid shadow coverage is still required", true, 0, true, 1 );
static ConVar r_volumetric_sun_density( "r_volumetric_sun_density", "0.00025", FCVAR_ARCHIVE, "Sun-only volumetric scattering coefficient with sun override on", true, 0.f, true, 0.01f );
static ConVar r_volumetric_sun_strength( "r_volumetric_sun_strength", "1", FCVAR_ARCHIVE, "Sun-only volumetric radiance multiplier with sun override on", true, 0.f, true, 8.f );
static ConVar r_volumetric_sun_anisotropy( "r_volumetric_sun_anisotropy", "0.2", FCVAR_ARCHIVE, "Sun-only volumetric phase anisotropy with sun override on", true, -0.9f, true, 0.9f );
static ConVar r_volumetric_sun_max_distance( "r_volumetric_sun_max_distance", "2048", FCVAR_ARCHIVE, "Sun-only volumetric ray distance with sun override on, clipped to valid shadow coverage", true, 64.f, true, 8192.f );

namespace
{
IShaderAPIDX12ScreenSpace *s_pScreenSpace = nullptr;
bool s_bVolumetricScope = false;
DX12VolumetricSettings s_VolumetricSettings = {};
const uint32 kContactSteps[3] = { 8, 12, 24 };
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

void ScreenSpaceDX12_Shutdown()
{
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

IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_BeginView( IMatRenderContext *pRenderContext, const Vector &eye, const Vector &forward, bool &contacts, bool &ssr )
{
	contacts = false;
	ssr = false;
	if ( !s_pScreenSpace || !pRenderContext )
		return nullptr;

	contacts = r_contactshadows.GetBool();
	ssr = r_ssr.GetBool();
	if ( !contacts && !ssr && !s_bVolumetricScope )
		return nullptr;

	int x, y, width, height;
	pRenderContext->GetViewport( x, y, width, height );
	DX12ScreenSpaceViewDesc view = {};
	view.constants.eyeLength[0] = eye.x;
	view.constants.eyeLength[1] = eye.y;
	view.constants.eyeLength[2] = eye.z;
	view.constants.eyeLength[3] = r_contactshadows_length.GetFloat();
	view.constants.forwardThickness[0] = forward.x;
	view.constants.forwardThickness[1] = forward.y;
	view.constants.forwardThickness[2] = forward.z;
	view.constants.forwardThickness[3] = r_contactshadows_thickness.GetFloat();
	view.constants.viewport[0] = static_cast<float>( x );
	view.constants.viewport[1] = static_cast<float>( y );
	view.constants.viewport[2] = static_cast<float>( width );
	view.constants.viewport[3] = static_cast<float>( height );
	view.constants.control[0] = contacts ? 1u : 0u;
	view.constants.control[2] = kContactSteps[r_contactshadows_quality.GetInt()];
	view.constants.control[3] = ssr ? 1u : 0u;
	view.constants.depth[1] = 0.25f;
	view.constants.depth[2] = 0.5f;
	view.constants.depth[3] = 8.f;
	view.ssr.quality = static_cast<uint32>( r_ssr_quality.GetInt() );
	view.ssr.maxDistance = r_ssr_max_distance.GetFloat();
	view.ssr.thickness = r_ssr_thickness.GetFloat();
	// Matrices, sample count and depth clear are filled on ordered backend replay.
	// Submit zero-sized views too: a rejected latest main bracket must invalidate a volume capture.
	s_pScreenSpace->BeginView( view );
	return s_pScreenSpace;
}

void ScreenSpaceDX12_BeginVolumetrics( const CViewSetup &view )
{
	if ( !s_pScreenSpace || !r_volumetric.GetBool() || CurrentViewID() != VIEW_MAIN ||
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
	// The dither pattern is static unless a temporal upscaler accumulates it (GTAO convention: gtao_dx12.cpp).
	const bool bTemporal = UpscalerDX12_Enabled() && UpscalerDX12_Status() > 0;
	settings.control[3] = bTemporal ? static_cast<uint32>( gpGlobals->framecount & 63 ) : 0u;
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
