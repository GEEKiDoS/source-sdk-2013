//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef POSTPROCESS_DX12_H
#define POSTPROCESS_DX12_H
#pragma once

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class IMatRenderContext;

void PostProcessDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
// Main-thread initialization retains all post materials and primes their variable tokens before queued rendering.
void PostProcessDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void PostProcessDX12_Shutdown();
bool PostProcessDX12_Active();
// First level init (config.cfg loaded): re-enables mat_postfx_dx12 once for a config saved before it was validated.
void PostProcessDX12_ValidateConfig();
void PostProcessDX12_SubmitFrameConfig();
void PostProcessDX12_Render( IMatRenderContext *pRenderContext, int x, int y, int w, int h, float flBloomScale, bool bColorCorrection );
int PostProcessDX12_Status();

#endif
