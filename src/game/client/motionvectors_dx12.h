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
// Records the first main-view motion-vector pass for the current frame.
void MotionVectorsDX12_RecordMainPassBegin();
// Returns whether the current frame produced motion vectors and, when available,
// reports the wall-clock interval spanned by the previous and current passes.
bool MotionVectorsDX12_FrameValid( float *pDeltaSeconds );
ITexture *MotionVectorsDX12_RenderTarget();
int MotionVectorsDX12_ObjectKey( IClientRenderable *pRenderable );

#endif // MOTIONVECTORS_DX12_H
