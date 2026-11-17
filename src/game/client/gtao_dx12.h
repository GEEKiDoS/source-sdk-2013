//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 GTAO client trigger.
//
//===========================================================================//
#ifndef GTAO_DX12_H
#define GTAO_DX12_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class IMatRenderContext;
class CViewSetup;

void GTAODX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void GTAODX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void GTAODX12_Shutdown();
bool GTAODX12_Enabled();
// Computes this frame's AO term (depth only; once the opaque depth is final, before the SSR marker). pViewModel is the
// viewmodels' view when their depth is in the scene depth (the viewmodel depth prepass): their pixels then get their own
// term, which the viewmodel draws apply themselves (pbr_forward_screenspace.hlsli); otherwise they read as far.
void GTAODX12_Compute( IMatRenderContext *pRenderContext, const CViewSetup *pViewModel );
// Darkens the opaque scene by the computed term (after SSR, before translucency).
void GTAODX12_Apply( IMatRenderContext *pRenderContext );
int GTAODX12_Status();

#endif // GTAO_DX12_H
