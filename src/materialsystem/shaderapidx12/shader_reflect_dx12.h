//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Shader bytecode reflection for both native formats: SM5 DXBC (D3DReflect)
//          and SM6 DXIL containers (dxcompiler's IDxcUtils::CreateReflection).
//          Shared by shaderapidx12 and ShaderCompiler3.
//
//=============================================================================//
#ifndef SHADER_REFLECT_DX12_H
#define SHADER_REFLECT_DX12_H
#pragma once

#include <d3d12shader.h>
#include <cstddef>
#include <cstdint>

namespace shaderapidx12
{

enum class ShaderBytecodeKindDX12
{
	Invalid,
	Dxbc,	// container with an SHDR/SHEX program
	Dxil,	// container with a DXIL program
};

// Program kind from the SM4+ version token (DXBC) or the DXIL program header: 0 pixel, 1 vertex, 5 compute.
// Returns Invalid for anything that is not a well-formed container with exactly one of the two program parts.
ShaderBytecodeKindDX12 ClassifyShaderBytecodeDX12( const void *pBytes, size_t nBytes, uint32_t *pProgramKind = nullptr );

// ID3D12ShaderReflection for DXBC or DXIL. DXIL needs dxcompiler.dll next to the calling module;
// without it the call fails with ERROR_MOD_NOT_FOUND.
HRESULT ReflectShaderDX12( const void *pBytes, size_t nBytes, ID3D12ShaderReflection **ppReflection );

// True when dxcompiler.dll beside the calling module loaded and created its IDxcUtils.
bool DxilReflectionAvailableDX12();

}

#endif // SHADER_REFLECT_DX12_H
