//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 PBR G-buffer debug view: the compute material expands the backend's world-normal / F0+roughness
//          targets into _rt_PBRGBufferDebug, an UnlitGeneric blit puts it over the opaque scene color.
//
//===========================================================================//
#include "cbase.h"
#include "pbr_debug_dx12.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/imaterialsystemhardwareconfig.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/itexture.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar mat_pbr_showgbuffer( "mat_pbr_showgbuffer", "0", FCVAR_CHEAT, "DX12 PBR G-buffer debug view: 0 off, 1 world normals, 2 F0, 3 roughness", true, 0, true, 3 );

namespace
{
static bool s_bDX12 = false;
static bool s_bMaterialsReady = false;
static CTextureReference s_DebugTarget;
static IMaterial *s_pCompute = nullptr;
static IMaterial *s_pBlit = nullptr;

static void ReleaseMaterial( IMaterial *&pMaterial )
{
	if ( pMaterial )
	{
		pMaterial->DecrementReferenceCount();
		pMaterial = nullptr;
	}
}
}

void PBRDebugDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig )
{
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	s_bDX12 = shaderDLL && !V_stricmp( shaderDLL, "stdshader_dx12" );
	s_bMaterialsReady = false;
}

void PBRDebugDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	s_bMaterialsReady = false;
	const char *shaderDLL = pHardwareConfig ? pHardwareConfig->GetShaderDLLName() : nullptr;
	if ( !pMaterialSystem || !shaderDLL || V_stricmp( shaderDLL, "stdshader_dx12" ) )
		return;
	s_DebugTarget.Init( pMaterialSystem->CreateNamedRenderTargetTextureEx2( "_rt_PBRGBufferDebug", 1, 1, RT_SIZE_FULL_FRAME_BUFFER,
		IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_NONE, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE, 0 ) );
	if ( !s_DebugTarget.IsValid() )
		return;

	// $output and $mode are set per dispatch, like the GTAO passes.
	s_pCompute = pMaterialSystem->CreateMaterial( "dx12/pbr_gbuffer_debug", new KeyValues( "DX12_PBRGBufferDebug" ) );

	// The compute pass writes raw channel values: read them back without the sRGB decode.
	KeyValues *pBlit = new KeyValues( "UnlitGeneric" );
	pBlit->SetString( "$basetexture", "_rt_PBRGBufferDebug" );
	pBlit->SetInt( "$ignorez", 1 );
	pBlit->SetInt( "$nofog", 1 );
	pBlit->SetInt( "$gammacolorread", 1 );
	s_pBlit = pMaterialSystem->CreateMaterial( "dx12/pbr_gbuffer_debug_blit", pBlit );

	s_bMaterialsReady = s_pCompute && !s_pCompute->IsErrorMaterial() && s_pBlit && !s_pBlit->IsErrorMaterial();
}

void PBRDebugDX12_Shutdown()
{
	ReleaseMaterial( s_pCompute );
	ReleaseMaterial( s_pBlit );
	s_DebugTarget.Shutdown();
	s_bMaterialsReady = false;
	s_bDX12 = false;
}

bool PBRDebugDX12_GBufferEnabled()
{
	return s_bDX12;
}

void PBRDebugDX12_Dispatch()
{
	const int nChannel = mat_pbr_showgbuffer.GetInt();
	if ( !s_bMaterialsReady || nChannel <= 0 )
		return;

	CMatRenderContextPtr pRenderContext( materials );
	int viewportX, viewportY, width, height;
	pRenderContext->GetViewport( viewportX, viewportY, width, height );
	if ( width <= 0 || height <= 0 )
		return;

	PIXEVENT( pRenderContext, "PBR G-buffer debug" );
	bool bFound;
	s_pCompute->FindVar( "$output", &bFound, false )->SetTextureValue( s_DebugTarget );
	s_pCompute->FindVar( "$mode", &bFound, false )->SetIntValue( nChannel - 1 ); // shader modes: 0 normals, 1 F0, 2 roughness

	// Compute materials dispatch from their dynamic state and skip the raster draw; the rectangle only drives the pass.
	pRenderContext->DrawScreenSpaceRectangle( s_pCompute, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1 );
	pRenderContext->DrawScreenSpaceRectangle( s_pBlit, 0, 0, width, height, 0, 0, width - 1, height - 1, s_DebugTarget->GetActualWidth(), s_DebugTarget->GetActualHeight() );
}
