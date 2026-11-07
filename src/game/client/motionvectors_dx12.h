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
class IMatRenderContext;

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
// True only for a current, CPU-certified stationary resolve whose conservative velocity bound
// guarantees that the available tile path would copy every centre pixel. No GPU readback.
bool MotionVectorsDX12_BlurIsStatic( float flBlurScale, float flMaxBlurLength, int nRegionWidth, int nRegionHeight );
// Backend replay epoch on the recording owner, -1 otherwise (or on another backend).
int MotionVectorsDX12_FrameEpoch();
ITexture *MotionVectorsDX12_RenderTarget();
int MotionVectorsDX12_ObjectKey( IClientRenderable *pRenderable );
// Dilated 16x16 maxima of the velocity region [0, nRegionWidth) x [0, nRegionHeight) (the main 3D view's render size;
// smaller than the target under a temporal render scale). Counts stay 0 when the targets are unavailable, and the
// blur then runs its full gather.
bool MotionVectorsDX12_BuildBlurTiles( IMatRenderContext *pRenderContext, float flMaxBlurLength, int nRegionWidth, int nRegionHeight, int &nTileCountX, int &nTileCountY );

#endif // MOTIONVECTORS_DX12_H
