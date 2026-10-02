//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Episodic client render targets.
//
//===========================================================================//
#include "cbase.h"
#include "episodic_rendertargets.h"
#include "motionvectors_dx12.h"
#include "upscaler_dx12.h"

void CEpisodicRenderTargets::InitClientRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig )
{
	BaseClass::InitClientRenderTargets( pMaterialSystem, pHardwareConfig );
	MotionVectorsDX12_CreateRenderTarget( pMaterialSystem, pHardwareConfig );
	UpscalerDX12_Init( pHardwareConfig );
}

void CEpisodicRenderTargets::ShutdownClientRenderTargets()
{
	UpscalerDX12_Shutdown();
	MotionVectorsDX12_ShutdownRenderTarget();
	BaseClass::ShutdownClientRenderTargets();
}

static CEpisodicRenderTargets g_EpisodicRenderTargets;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CEpisodicRenderTargets, IClientRenderTargets,
	CLIENTRENDERTARGETS_INTERFACE_VERSION, g_EpisodicRenderTargets );
