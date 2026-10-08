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

	// DX12 motion-vector pass (shaderapidx12 only; other backends store and ignore).
	// PASS: 0 = end pass (resolve into render target 0), 1 = begin main pass (clear, camera reprojection, VP slot 0),
	//       2 = append to main pass (VP slot 0), 3 = begin viewmodel append pass (VP slot 1),
	//       4 = advance empty viewmodel VP history only (no draws/resolve; END is a no-op on success).
	INT_RENDERPARM_DX12_MOTION_PASS,
	// Object identity for bone history: CBaseHandle::ToInt() of the renderable; 0 = static (no history).
	INT_RENDERPARM_DX12_MOTION_OBJECT,
	// Backend-written status (read by the client through IShaderAPI, never through a render context):
	// 0 = not evaluated, 1 = available, -1 = private shaders unavailable (sticky), -2 = unavailable for the current MSAA mode.
	INT_RENDERPARM_DX12_MOTION_STATUS,

	// DX12 native-AA upscaler; other backends store and ignore these values.
	INT_RENDERPARM_DX12_UPSCALE_MODE,     // 0 off, 1 auto, 2 DLSS/DLAA, 3 FSR native AA, 4 XeSS AA.
	INT_RENDERPARM_DX12_UPSCALE_DISPATCH, // bit 0 run, bit 1 reset.
	INT_RENDERPARM_DX12_UPSCALE_STATUS,   // backend-owned: 0 off/unevaluated, 1|(kind<<8) active (DLSS 1, FSR 2, XeSS 3),
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

	MAX_INT_RENDER_PARMS = 23
};

// for INT_RENDERPARM_BACK_BUFFER_INDEX
#define BACK_BUFFER_INDEX_DEFAULT	0
#define BACK_BUFFER_INDEX_HDR		1
#define DX12_MOTION_PASS_END            0
#define DX12_MOTION_PASS_BEGIN_MAIN     1
#define DX12_MOTION_PASS_APPEND_MAIN    2
#define DX12_MOTION_PASS_BEGIN_VIEWMODEL 3
#define DX12_MOTION_PASS_HISTORY_VIEWMODEL 4
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
