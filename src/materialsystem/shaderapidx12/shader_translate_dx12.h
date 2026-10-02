//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Renderer-facing interface of the private legacy shader translator (dx12_shaderconv).
//
//=============================================================================//

#ifndef SHADER_TRANSLATE_DX12_H
#define SHADER_TRANSLATE_DX12_H
#pragma once

#include "tier1/utlvector.h"
#include "tier1/utlstring.h"
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace shaderapidx12
{
using SignDxbcFnDX12 = HRESULT( APIENTRY * )( BYTE *, UINT32 );

// Numeric usage, conversion and component fields follow D3D declaration/DXBC values.
// No DDI/compiler headers escape the isolated compiler library.
struct ShaderVertexInputDX12
{
	uint32_t usage = 0, usageIndex = 0, registerIndex = 0;
	uint32_t conversion = 0, componentType = 3;
	bool transformedPosition = false;
};

struct ShaderLinkageDX12
{
	uint32_t usage = 0, usageIndex = 0, registerIndex = 0, writeMask = 0;
	bool centroid = false;
};

struct ShaderInlineConstantDX12
{
	uint32_t registerIndex = 0;
	uint32_t words[4] = {};
};

struct ShaderRasterStateDX12
{
	// Texture dimensions: 1 = 2D, 2 = cube, 3 = volume; null bindings remain 2D.
	uint8_t textureTypes[16] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	uint8_t texcoordWrap[16] = {};
	uint32_t texcoordMapping = 0x76543210;
	uint32_t projectedTexcoords = 0, clipPlaneMask = 0;
	uint32_t fillMode = 3, shadeMode = 2, primitiveType = 4, alphaFunction = 8;
	uint32_t fogTableMode = 0, comparisonPixelSamplers = 0, comparisonVertexSamplers = 0;
	bool alphaTest = false, fog = false, wFog = false;
	bool pointSize = false, pointSprite = false, transformedVertices = false;
};

struct ShaderTranslationRequestDX12
{
	const void *legacyBytes = nullptr;
	size_t byteCount = 0;
	bool pixel = false;
	const ShaderVertexInputDX12 *vertexInputs = nullptr;
	size_t vertexInputCount = 0;
	const ShaderLinkageDX12 *linkedOutputs = nullptr;
	size_t linkedOutputCount = 0;
	uint32_t centroidTexcoordMask = 0;
	ShaderRasterStateDX12 raster;
};

struct ShaderTranslationResultDX12
{
	// Complete, signed ISG1/OSG1/SHDR container, owned through PSO lifetime.
	CUtlVector<uint8_t> bytecode;
	CUtlVector<ShaderVertexInputDX12> vertexInputs;
	CUtlVector<ShaderLinkageDX12> inputLinkage, outputLinkage;
	// Register words for float/int/bool files respectively; bool uses words[0] only.
	CUtlVector<ShaderInlineConstantDX12> inlineConstants[3];
	// Float/int counts are float4/int4 registers; bool count is scalar 32-bit registers.
	uint32_t maxFloatConstants = 0, maxIntConstants = 0, maxBoolConstants = 0;
	// Legacy sampler declarations are independent of raster/linkage variants.
	uint32_t usedSamplerMask = 0;
	uint8_t outputRegistersMask = 0;

	// Member-wise exchange; variant switching runs per draw and must not build temporaries.
	void Swap( ShaderTranslationResultDX12 &other )
	{
		bytecode.Swap( other.bytecode );
		vertexInputs.Swap( other.vertexInputs );
		inputLinkage.Swap( other.inputLinkage );
		outputLinkage.Swap( other.outputLinkage );
		for ( int nBank = 0; nBank < ARRAYSIZE( inlineConstants ); ++nBank )
			inlineConstants[nBank].Swap( other.inlineConstants[nBank] );
		V_swap( maxFloatConstants, other.maxFloatConstants );
		V_swap( maxIntConstants, other.maxIntConstants );
		V_swap( maxBoolConstants, other.maxBoolConstants );
		V_swap( usedSamplerMask, other.usedSamplerMask );
		V_swap( outputRegistersMask, other.outputRegistersMask );
	}

	// Frees every container and restores the default-constructed state.
	void Purge()
	{
		bytecode.Purge();
		vertexInputs.Purge();
		inputLinkage.Purge();
		outputLinkage.Purge();
		for ( int nBank = 0; nBank < ARRAYSIZE( inlineConstants ); ++nBank )
			inlineConstants[nBank].Purge();
		maxFloatConstants = 0;
		maxIntConstants = 0;
		maxBoolConstants = 0;
		usedSamplerMask = 0;
		outputRegistersMask = 0;
	}
};

// Exact converter constant-buffer layouts, checked in the private implementation.
// Generated GS code uses this same VS extension layout at GS-visible b3.
struct ShaderVertexExtensionDX12
{
	float viewportScale[4] = {}, pointSize[4] = {}, clipPlanes[6][4] = {};
	float screenToClipOffset[4] = {}, screenToClipScale[4] = {};
};

struct ShaderPixelExtensionDX12
{
	float fogColor[3] = {}, alphaReference = 0;
	float fogStart = 0, fogEnd = 0, fogDistanceInverse = 0, fogDensity = 0;
};

struct ShaderBumpExtensionDX12
{
	float matrix[8][4] = {}, luminance[8][4] = {};
};

struct ShaderColorKeyExtensionDX12
{
	float colorKey[8][4] = {};
};

class CShaderTranslatorDX12
{
public:
	bool TranslateLegacy( const ShaderTranslationRequestDX12 &request, SignDxbcFnDX12 pfnSigner,
	    ShaderTranslationResultDX12 &result, CUtlString &error ) const;
	bool GenerateGeometry( const ShaderLinkageDX12 *pInputs, size_t nInputCount,
	    const ShaderRasterStateDX12 &raster, SignDxbcFnDX12 pfnSigner,
	    ShaderTranslationResultDX12 &result, CUtlString &error ) const;
};
} // namespace shaderapidx12

#endif // SHADER_TRANSLATE_DX12_H
