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
// Called after the upscaler/framegen BeginFrame latches, before any motion draw of this RenderView.
// The blur gate must match the actual mono-main-view velocity-blur caller, without consulting Enabled().
// Automatic mode covers SDK/installed consumers plus active debug/overlay/screen-effect gates.
// Unsupported arbitrary custom map/VGUI material readers must use r_motionvectors 2.
void MotionVectorsDX12_BeginFrame( bool bVelocityBlurEligible );
// Available and needed by this RenderView's current-frame consumer latch; not a provider-selection gate.
bool MotionVectorsDX12_Enabled();
// Records the first main-view motion-vector pass for the current frame.
void MotionVectorsDX12_RecordMainPassBegin();
// Returns whether this RenderView submitted a main motion pass in the current frame. Backend dispatches
// still validate the ordered resolve themselves. Delta is zero after a history gap or when invalid.
bool MotionVectorsDX12_FrameValid( float *pDeltaSeconds );
ITexture *MotionVectorsDX12_RenderTarget();
int MotionVectorsDX12_ObjectKey( IClientRenderable *pRenderable );

#endif // MOTIONVECTORS_DX12_H
