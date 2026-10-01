//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 motion-vector render-target and client pass helpers.
//
//===========================================================================//
#ifndef MOTIONVECTORS_DX12_H
#define MOTIONVECTORS_DX12_H
#ifdef _WIN32
#pragma once
#endif

class IMaterialSystem;
class IMaterialSystemHardwareConfig;
class ITexture;
class IClientRenderable;

void MotionVectorsDX12_CreateRenderTarget( IMaterialSystem *pMaterialSystem, IMaterialSystemHardwareConfig *pHardwareConfig );
void MotionVectorsDX12_ShutdownRenderTarget();
bool MotionVectorsDX12_Enabled();
ITexture *MotionVectorsDX12_RenderTarget();
int MotionVectorsDX12_ObjectKey( IClientRenderable *pRenderable );

#endif // MOTIONVECTORS_DX12_H
