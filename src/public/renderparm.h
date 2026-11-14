//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: defines constants to use for the materialsystem and shaderapi
// SetxxxRenderingParameter functions
//
// $NoKeywords: $
//
//===========================================================================//

#ifndef RENDERPARM_H
#define RENDERPARM_H

#ifndef _WIN32
#pragma once
#endif


enum RenderParamVector_t
{
	VECTOR_RENDERPARM_HMDWARP_LEFT_CENTRE = 0,
	VECTOR_RENDERPARM_HMDWARP_LEFT_COEFF012,
	VECTOR_RENDERPARM_HMDWARP_LEFT_COEFF34_RED_OFFSET,
	VECTOR_RENDERPARM_HMDWARP_RIGHT_CENTRE,
	VECTOR_RENDERPARM_HMDWARP_RIGHT_COEFF012,
	VECTOR_RENDERPARM_HMDWARP_RIGHT_COEFF34_BLUE_OFFSET,
	VECTOR_RENDERPARM_HMDWARP_GROW_OUTIN,
	VECTOR_RENDERPARM_HMDWARP_GROW_ABOVEBELOW,
	VECTOR_RENDERPARM_HMDWARP_ASPECT,
	INT_RENDERPARM_DISTORTION_TYPE,
	VECTOR_RENDERPARM_WIND_DIRECTION,
	VECTOR_RENDERPARM_L4D2_WATER_FOG_RANGE = 11, // start, end, unused
	// Published every frame by the DX12 post stack (postprocess_dx12.cpp) for materials whose colours are authored
	// for the display rather than the scene: x = the tone curve's peak relative to paper white (0 while the scene
	// is presented without that curve), y = the exposure bias in EV applied before it.
	VECTOR_RENDERPARM_DX12_DISPLAY_TRANSFORM = 12,

	MAX_VECTOR_RENDER_PARMS = 20
};


#define MAX_FLOAT_RENDER_PARMS 20


// DX12 native-AA upscaler camera (main CViewSetup, submitted before the dispatch); <= 0 derives from the projection.
enum RenderParamFloat_t
{
	FLOAT_RENDERPARM_DX12_UPSCALE_FOV_Y = 0,	// vertical field of view, radians
	FLOAT_RENDERPARM_DX12_UPSCALE_NEAR,
	FLOAT_RENDERPARM_DX12_UPSCALE_FAR,
	// DLSS-NR model tuning (read when each layer's feature is created; a change rebuilds the chain).
	FLOAT_RENDERPARM_DX12_NR_INTENSITY,
	FLOAT_RENDERPARM_DX12_NR_LOCAL_STRUCTURE,
	FLOAT_RENDERPARM_DX12_NR_LOCAL_TONE,
	FLOAT_RENDERPARM_DX12_NR_SKIN_STRUCTURE,	// -1 follows local structure
	// DLSS-NR composition of each layer's model answer back onto that layer's input (read per dispatch).
	FLOAT_RENDERPARM_DX12_NR_WHITE_POINT,
	FLOAT_RENDERPARM_DX12_NR_TRANSFER_STRENGTH,
	FLOAT_RENDERPARM_DX12_NR_COLOUR_STRENGTH,
	FLOAT_RENDERPARM_DX12_NR_MAX_RATIO,
	FLOAT_RENDERPARM_DX12_NR_COMPARE_SPLIT,
	FLOAT_RENDERPARM_DX12_NR_COMPARE_ZOOM,
	// DX12 temporal upscaler input: the main 3D view's render size in pixels, at the top-left of the scene targets
	// (submitted before every dispatch). <= 0 or >= the scene size is native-resolution AA.
	FLOAT_RENDERPARM_DX12_UPSCALE_RENDER_WIDTH,
	FLOAT_RENDERPARM_DX12_UPSCALE_RENDER_HEIGHT,
	// Material sampler mip bias requested for the current temporal-AA view. The shader API applies it only to
	// eligible mipmapped material textures, and only after a provider has been selected.
	FLOAT_RENDERPARM_DX12_UPSCALE_LOD_BIAS,
	// Backend-written (shaderapidx12 only, read through IShaderAPI after the frame's main motion pass resolved): conservative
	// upper bound of the motion target's velocity length in UV units (1 = full target extent) while
	// INT_RENDERPARM_DX12_MOTION_BLUR_STATIC is 1; it covers the camera reprojection's float residual and the RGBA16F
	// quantisation. Meaningless (0) otherwise.
	FLOAT_RENDERPARM_DX12_MOTION_STATIC_BOUND,
	// Frame-stable shader clock published by the L4D2 client at FRAME_RENDER_START (tree sway, water, engine post).
	// It follows the DX12 slots so neither backend reinterprets the other's values.
	FLOAT_RENDERPARM_L4D2_CURRENT_TIME,
};

enum RenderParamInt_t
{
	INT_RENDERPARM_ENABLE_FIXED_LIGHTING = 0,
	INT_RENDERPARM_MORPH_ACCUMULATOR_X_OFFSET,
	INT_RENDERPARM_MORPH_ACCUMULATOR_Y_OFFSET,
	INT_RENDERPARM_MORPH_ACCUMULATOR_SUBRECT_WIDTH,
	INT_RENDERPARM_MORPH_ACCUMULATOR_SUBRECT_HEIGHT,
	INT_RENDERPARM_MORPH_ACCUMULATOR_4TUPLE_COUNT,

	INT_RENDERPARM_MORPH_WEIGHT_X_OFFSET,
	INT_RENDERPARM_MORPH_WEIGHT_Y_OFFSET,
	INT_RENDERPARM_MORPH_WEIGHT_SUBRECT_WIDTH,
	INT_RENDERPARM_MORPH_WEIGHT_SUBRECT_HEIGHT,

	INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA,

	INT_RENDERPARM_BACK_BUFFER_INDEX,
	INT_RENDERPARM_L4D2_WRITE_WATER_FOG_TO_DESTALPHA = 12,

	// DX12 motion-vector pass (shaderapidx12 only; other backends store and ignore).
	// PASS: 0 = end pass (resolve into render target 0), 1 = begin main pass (clear, camera reprojection, VP slot 0),
	//       2 = append to main pass (VP slot 0), 3 = begin viewmodel append pass (VP slot 1),
	//       4 = advance empty viewmodel VP history only (no draws/resolve; END is a no-op on success).
	INT_RENDERPARM_DX12_MOTION_PASS,
	// Object identity for bone history: CBaseHandle::ToInt() of the renderable; 0 = static (no history).
	INT_RENDERPARM_DX12_MOTION_OBJECT,
	// DX12 PBR G-buffer pass (shaderapidx12 only; other backends store and ignore): 1 = begin (PBR draws that write
	// three SV_Targets append the normal / F0+roughness targets; no clear — validity is stencil bit 0x80), 0 = end
	// (targets become readable).
	INT_RENDERPARM_DX12_PBR_GBUFFER_PASS,
	// Backend-written status (read by the client through IShaderAPI, never through a render context):
	// 0 = not evaluated, 1 = available, -1 = private shaders unavailable (sticky), -2 = unavailable for the current MSAA mode.
	INT_RENDERPARM_DX12_MOTION_STATUS,

	// DX12 temporal upscaler (native AA or a reduced render size, see FLOAT_RENDERPARM_DX12_UPSCALE_RENDER_*); other backends store and ignore these values.
	INT_RENDERPARM_DX12_UPSCALE_MODE,     // 0 off, 1 auto, 2 DLSS/DLAA, 3 FSR, 4 XeSS, 5 native SMAA T2x.
	INT_RENDERPARM_DX12_UPSCALE_DISPATCH, // bit 0 run, bit 1 reset.
	INT_RENDERPARM_DX12_UPSCALE_STATUS,   // backend-owned: 0 off/unevaluated, 1|(kind<<8) active (DLSS 1, FSR 2, XeSS 3, SMAA T2x 4),
	                                      // -1 no provider, -2 feature creation failed, -3 MSAA, -4 wrong target/owner,
	                                      // -5 no motion resolve this frame, -6 provider replay error.
	// DLSS-NR layers chained after the temporal AA dispatch (layer N+1 is fed layer N's output).
	INT_RENDERPARM_DX12_NR_CONFIG,        // DX12_NR_CONFIG_* bit fields; 0 layers disables NR.
	INT_RENDERPARM_DX12_NR_STATUS,        // backend-owned: 0 off, N > 0 layers active, -1 unavailable, -2 creation failed,
	                                      // -3 no temporal AA dispatch this frame, -6 replay error.

	// DX12 frame generation, the per-frame part that must stay ordered with the draw stream (shaderapidx12 only; other
	// backends store and ignore). Settings, status and latency markers go through IShaderAPIDX12 (shaderapi/ishaderapidx12.h).
	INT_RENDERPARM_DX12_FRAMEGEN_VIEW,      // 1: this RenderView is the eligible main view (sent before its first 3D draw);
	                                        // a frame without it presents in pass-through, the provider stays selected.
	INT_RENDERPARM_DX12_FRAMEGEN_DISPATCH,  // bit 0 run, bit 1 reset; sent once per main view after the last post-processing pass, before the HUD.
	INT_RENDERPARM_DX12_FRAMEGEN_FRAME,     // client frame id (28 bits, the one the latency markers use) for the frame being rendered; queued before DISPATCH.
	// Backend-written (shaderapidx12 only): 1 while this frame's resolved motion target is a stationary candidate, i.e. the
	// main pass's effective previous and current view-projection are bit-identical and every motion draw's previous
	// transforms (view-projection and model rows) were bit-identical to its current ones (history misses and static
	// objects reuse the current transforms). 0 for stale, failed, suppressed or moving frames.
	INT_RENDERPARM_DX12_MOTION_BLUR_STATIC,
	// Backend-written (shaderapidx12 only): the backend's current frame counter (31 bits) as seen by the recording owner,
	// -1 when queried off the recording owner (queued client frames may run ahead of the frame being recorded).
	INT_RENDERPARM_DX12_FRAME_EPOCH,

	MAX_INT_RENDER_PARMS = 27
};

// for INT_RENDERPARM_BACK_BUFFER_INDEX
#define BACK_BUFFER_INDEX_DEFAULT	0
#define BACK_BUFFER_INDEX_HDR		1
#define DX12_MOTION_PASS_END            0
#define DX12_MOTION_PASS_BEGIN_MAIN     1
#define DX12_MOTION_PASS_APPEND_MAIN    2
#define DX12_MOTION_PASS_BEGIN_VIEWMODEL 3
#define DX12_MOTION_PASS_HISTORY_VIEWMODEL 4
#define DX12_GBUFFER_PASS_END           0
#define DX12_GBUFFER_PASS_BEGIN         1
#define DX12_UPSCALE_DISPATCH_RUN   1
#define DX12_UPSCALE_DISPATCH_RESET 2
#define DX12_FRAMEGEN_DISPATCH_RUN   1
#define DX12_FRAMEGEN_DISPATCH_RESET 2
// INT_RENDERPARM_DX12_NR_CONFIG bit fields.
#define DX12_NR_MAX_LAYERS                8
#define DX12_NR_CONFIG_LAYERS_SHIFT       0	// 4 bits, 0..DX12_NR_MAX_LAYERS
#define DX12_NR_CONFIG_PRESET_SHIFT       4	// 4 bits, 0 default, 1..3 model presets
#define DX12_NR_CONFIG_STYLE_SHIFT        8	// 2 bits, 0 standard, 1 natural, 2 cinematic
#define DX12_NR_CONFIG_REVERSIBLE_SHIFT   10	// 3 bits, 0 soft knee, 1 Neutwo composed, 2 Neutwo replace, 3 hybrid composed, 4 hybrid replace
#define DX12_NR_CONFIG_DEBUG_VIEW_SHIFT   13	// 2 bits, 0 off, 1 proxy, 2 model answer, 3 amplified edit
#define DX12_NR_CONFIG_COMPARE_SHIFT      15	// 2 bits, 0 off, 1 side by side, 2 wipe
#define DX12_NR_CONFIG_AUTO_MASK          (1 << 17)
#define DX12_NR_CONFIG_UI_CORRECTION      (1 << 18)
#define DX12_NR_CONFIG_APPLY_MODEL        (1 << 19)
#define DX12_NR_CONFIG_COMPARE_SWAP       (1 << 20)

enum RenderParamTexture_t
{
	TEXTURE_RENDERPARM_AMBIENT_OCCLUSION = 0,

	MAX_TEXTURE_RENDER_PARMS = 2
};

// ENABLE_FIXED_LIGHTING modes:
#define ENABLE_FIXED_LIGHTING_NONE 0
#define ENABLE_FIXED_LIGHTING_BASICLIGHT 1
#define ENABLE_FIXED_LIGHTING_OUTPUTMRTS_FOR_DEFERRED_LIGHTING 2
#define ENABLE_FIXED_LIGHTING_OUTPUTNORMAL_AND_DEPTH 3

#endif // RENDERPARM_H
