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

void GTAODX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void GTAODX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void GTAODX12_Shutdown();
bool GTAODX12_Enabled();
// Computes this frame's AO term (depth only; once the opaque depth is final, before the SSR marker).
void GTAODX12_Compute( IMatRenderContext *pRenderContext );
// Darkens the opaque scene by the computed term (after SSR, before translucency).
void GTAODX12_Apply( IMatRenderContext *pRenderContext );
int GTAODX12_Status();

#endif // GTAO_DX12_H
