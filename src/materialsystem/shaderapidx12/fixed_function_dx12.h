//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Generated SM5 replacement shaders for legacy fixed-function snapshots.
//
//=============================================================================//

#ifndef FIXED_FUNCTION_DX12_H
#define FIXED_FUNCTION_DX12_H
#pragma once

#include "materialsystem/shaderapidx12/shaderdevice_dx12.h"
#include "materialsystem/shaderapidx12/vertex_layout_dx12.h"
#include "shaderapi/ishadershadow.h"
#include "tier1/utlvector.h"

namespace shaderapidx12
{
struct FixedFunctionStateDX12
{
	VertexFormat_t format = 0;
	unsigned drawFlags = SHADER_DRAW_POSITION;
	int texCoordCount = 0;
	bool textureEnabled[16] = {}, texgen[16] = {}, textureAlpha[16] = {};
	uint8_t textureTypes[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	float overbright[16] = {};
	ShaderTexGenParam_t texgenParam[16] = {};
	ShaderTexOp_t colorOp[16] = {}, alphaOp[16] = {};
	ShaderTexArg_t colorArg1[16] = {}, colorArg2[16] = {}, alphaArg1[16] = {}, alphaArg2[16] = {};
	bool customPipe = false, lighting = false, specular = false, vertexBlend = false, constantColor = false, alphaPipe = false, constantAlpha = false, vertexAlpha = false;
	bool alphaTest = false, flatShade = false;
	ShaderAlphaFunc_t alphaFunction = SHADER_ALPHAFUNC_GEQUAL;
	ShaderMaterialSource_t materialSource = SHADER_MATERIALSOURCE_MATERIAL;
	ShaderFogMode_t fogMode = SHADER_FOGMODE_DISABLED;
};

// Generates real signed SM5 bytecode for a legacy fixed-function snapshot.
// The caller owns the returned shader record and must retain it until all PSOs retire.
ShaderRecordDX12 *CreateFixedFunctionShaderDX12( CShaderDeviceDX12 *pDevice, const FixedFunctionStateDX12 &state, bool bPixel,
    const CUtlVector<ShaderLinkageDX12> *pLinkedInputs = nullptr );
// Compiles null-terminated HLSL source into a caller-owned native shader record; null on failure.
ShaderRecordDX12 *CompileNativeShaderRecordDX12( CShaderDeviceDX12 *pDevice, const char *pszSource, bool bPixel, const char *pszProfile );
} // namespace shaderapidx12

#endif // FIXED_FUNCTION_DX12_H
