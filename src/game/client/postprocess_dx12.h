//========= Copyright Valve Corporation, All rights reserved. ============//
#ifndef POSTPROCESS_DX12_H
#define POSTPROCESS_DX12_H
#pragma once

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class IMatRenderContext;

void PostProcessDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void PostProcessDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void PostProcessDX12_Shutdown();
bool PostProcessDX12_Active();
void PostProcessDX12_SubmitFrameConfig();
void PostProcessDX12_Render( IMatRenderContext *pRenderContext, int x, int y, int w, int h, float flBloomScale, bool bColorCorrection );
int PostProcessDX12_Status();

#endif
