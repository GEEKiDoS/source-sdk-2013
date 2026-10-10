//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2MP client render targets.
//
//===========================================================================//
#include "cbase.h"
#include "hl2mp_rendertargets.h"
#include "motionvectors_dx12.h"
#include "upscaler_dx12.h"
#include "gtao_dx12.h"
#include "pbr_debug_dx12.h"
#include "postprocess_dx12.h"
#include "framegen_dx12.h"

void CHL2MPRenderTargets::InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );
	MotionVectorsDX12_CreateRenderTarget( pMaterialSystem, pHardwareConfig );
	UpscalerDX12_Init( pHardwareConfig );
	FrameGenDX12_Init( pHardwareConfig );
	GTAODX12_Init( pHardwareConfig );
	GTAODX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
	PBRDebugDX12_Init( pHardwareConfig );
	PBRDebugDX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
	PostProcessDX12_Init( pHardwareConfig );
	PostProcessDX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
}

void CHL2MPRenderTargets::ShutdownClientRenderTargets()
{
	PostProcessDX12_Shutdown();
	PBRDebugDX12_Shutdown();
	GTAODX12_Shutdown();
	FrameGenDX12_Shutdown();
	UpscalerDX12_Shutdown();
	MotionVectorsDX12_ShutdownRenderTarget();
	BaseClass::ShutdownClientRenderTargets();
}

static CHL2MPRenderTargets g_HL2MPRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CHL2MPRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_HL2MPRenderTargets );
