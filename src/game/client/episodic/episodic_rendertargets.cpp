//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Episodic client render targets.
//
//===========================================================================//
#include "cbase.h"
#include "episodic_rendertargets.h"
#include "motionvectors_dx12.h"
#include "upscaler_dx12.h"
#include "gtao_dx12.h"
#include "postprocess_dx12.h"
#include "framegen_dx12.h"

void CEpisodicRenderTargets::InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );
	MotionVectorsDX12_CreateRenderTarget( pMaterialSystem, pHardwareConfig );
	UpscalerDX12_Init( pHardwareConfig );
	FrameGenDX12_Init( pHardwareConfig );
	GTAODX12_Init( pHardwareConfig );
	GTAODX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
	PostProcessDX12_Init( pHardwareConfig );
	PostProcessDX12_CreateRenderTargets( pMaterialSystem, pHardwareConfig );
}

void CEpisodicRenderTargets::ShutdownClientRenderTargets()
{
	PostProcessDX12_Shutdown();
	GTAODX12_Shutdown();
	FrameGenDX12_Shutdown();
	UpscalerDX12_Shutdown();
	MotionVectorsDX12_ShutdownRenderTarget();
	BaseClass::ShutdownClientRenderTargets();
}

static CEpisodicRenderTargets g_EpisodicRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CEpisodicRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_EpisodicRenderTargets );
