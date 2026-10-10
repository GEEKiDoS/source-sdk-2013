//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 PBR G-buffer debug view (mat_pbr_showgbuffer).
//
//===========================================================================//
#ifndef PBR_DEBUG_DX12_H
#define PBR_DEBUG_DX12_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialSystem;
class IMaterialSystemHardwareConfig;

void PBRDebugDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void PBRDebugDX12_CreateRenderTargets( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void PBRDebugDX12_Shutdown();

// True on the DX12 renderer: the opaque scene pass of the main view fills the PBR G-buffer.
bool PBRDebugDX12_GBufferEnabled();

// Draws the channel selected by mat_pbr_showgbuffer (1 world normals, 2 F0, 3 roughness) over the opaque scene color.
// Call after the G-buffer pass ended; does nothing while the cvar is 0.
void PBRDebugDX12_Dispatch();

#endif // PBR_DEBUG_DX12_H
