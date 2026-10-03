//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: DX12 frame generation (DLSS-G / FSR frame generation / XeSS-FG) and NVIDIA Reflex client side. Settings,
//          status and latency markers use the renderer's IShaderAPIDX12; the eligible view, frame id and dispatch
//          go through the render context so they stay ordered with the draw stream.
//
//===========================================================================//
#ifndef FRAMEGEN_DX12_H
#define FRAMEGEN_DX12_H
#ifdef _WIN32
#pragma once
#endif

#include "cdll_int.h"

class IMaterialSystemHardwareConfig;
class IMatRenderContext;
class CViewSetup;

// After UpscalerDX12_Init: DX12 detection and the IShaderAPIDX12 lookup (r_framegen / r_reflex stay off without it).
void FrameGenDX12_Init( IMaterialSystemHardwareConfig *pHardwareConfig );
void FrameGenDX12_Shutdown();
// DX12, r_framegen != 0 and a valid _rt_MotionVectors target; says nothing about provider readiness.
bool FrameGenDX12_Enabled();
// Called once per normal RenderView before any 3D draw: marks the eligible main view for the renderer, forwards
// fps_max when it changes, and latches the frame for the dispatch.
void FrameGenDX12_BeginFrame( IMatRenderContext *pRenderContext, bool bMainViewEligible );
// Called after the last post-processing pass of the latched main view, before the HUD: camera floats, frame id
// and the dispatch.
void FrameGenDX12_Dispatch( IMatRenderContext *pRenderContext, const CViewSetup &view, bool bReset );
// Latency markers and status polling from CHLClient::FrameStageNotify (direct IShaderAPIDX12 calls, time-critical).
void FrameGenDX12_FrameStage( ClientFrameStage_t stage );
// IShaderAPIDX12::FrameGenerationStatus(); 0 when not on DX12 or the renderer lacks the interface.
int FrameGenDX12_Status();
// Smoothed frames shown per rendered frame: 1 without frame generation, ~2 (up to 4) with it.
float FrameGenDX12_ShownPerFrame();
// "DLSS-G" / "FSR FG" / "XeSS-FG" while a provider is active, NULL otherwise.
const char *FrameGenDX12_ActiveName();

#endif // FRAMEGEN_DX12_H
