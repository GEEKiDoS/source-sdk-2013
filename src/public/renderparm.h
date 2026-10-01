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
	//       2 = append to main pass (VP slot 0), 3 = begin viewmodel append pass (VP slot 1).
	INT_RENDERPARM_DX12_MOTION_PASS,
	// Object identity for bone history: CBaseHandle::ToInt() of the renderable; 0 = static (no history).
	INT_RENDERPARM_DX12_MOTION_OBJECT,
	// Backend-written status (read by the client through IShaderAPI, never through a render context):
	// 0 = not evaluated, 1 = available, -1 = private shaders unavailable (sticky), -2 = unavailable for the current MSAA mode.
	INT_RENDERPARM_DX12_MOTION_STATUS,

	MAX_INT_RENDER_PARMS = 20
};

// for INT_RENDERPARM_BACK_BUFFER_INDEX
#define BACK_BUFFER_INDEX_DEFAULT	0
#define BACK_BUFFER_INDEX_HDR		1
#define DX12_MOTION_PASS_END            0
#define DX12_MOTION_PASS_BEGIN_MAIN     1
#define DX12_MOTION_PASS_APPEND_MAIN    2
#define DX12_MOTION_PASS_BEGIN_VIEWMODEL 3

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
