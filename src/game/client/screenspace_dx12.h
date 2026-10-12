//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 screen-space effect controls and main-view scheduling.
//
//===========================================================================//
#ifndef SCREENSPACE_DX12_H
#define SCREENSPACE_DX12_H
#ifdef _WIN32
#pragma once
#endif

class Vector;
class IMaterialSystemHardwareConfig;
class IMatRenderContext;
class IShaderAPIDX12ScreenSpace;
class CViewSetup;
struct DX12VolumetricSettings;

void ScreenSpaceDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void ScreenSpaceDX12_Shutdown();
bool ScreenSpaceDX12_Available();
IShaderAPIDX12ScreenSpace *ScreenSpaceDX12_BeginView( IMatRenderContext *pRenderContext, const Vector &eye, const Vector &forward, bool &contacts, bool &ssr );
void ScreenSpaceDX12_BeginVolumetrics( const CViewSetup &view );
void ScreenSpaceDX12_DrawVolumetrics();
const DX12VolumetricSettings *ScreenSpaceDX12_GetVolumetricSettings();

#endif // SCREENSPACE_DX12_H
