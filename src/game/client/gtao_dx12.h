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
void GTAODX12_Dispatch( IMatRenderContext *pRenderContext );
int GTAODX12_Status();

#endif // GTAO_DX12_H
