//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 native-AA upscaler client trigger (DLAA / FSR native AA / XeSS AA).
//
//===========================================================================//
#ifndef UPSCALER_DX12_H
#define UPSCALER_DX12_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialSystemHardwareConfig;
class IMatRenderContext;
class CViewSetup;

void UpscalerDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void UpscalerDX12_Shutdown();
// DX12, r_upscaler != 0 and a valid _rt_MotionVectors target; says nothing about provider readiness.
bool UpscalerDX12_Enabled();
// Called once per normal RenderView before any 3D draw: submits the selected mode for the eligible main view and
// mode 0 otherwise, so no later view inherits an armed provider.
void UpscalerDX12_BeginFrame( IMatRenderContext *pRenderContext, bool bMainTemporalViewEligible, const CViewSetup &view );
// The existing BeginFrame eligibility latch, before Dispatch consumes it (includes the chained NR layers).
bool UpscalerDX12_RequiresMotionVectors();
// Called right after the viewmodels of the latched main view: submits camera parameters and the dispatch.
void UpscalerDX12_Dispatch( IMatRenderContext *pRenderContext, const CViewSetup &view, bool bReset );
// Backend status (INT_RENDERPARM_DX12_UPSCALE_STATUS); 0 when not on DX12.
int UpscalerDX12_Status();
// Turns mat_antialias off (temporal AA and frame generation both need a single-sample scene); shared with framegen_dx12.cpp.
void UpscalerDX12_ReconcileMSAA();

#endif // UPSCALER_DX12_H
