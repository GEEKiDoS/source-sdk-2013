//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Helpers for stdshader_dx12 materials that dispatch compute through IShaderAPIDX12Compute.
//
//===========================================================================//
#ifndef DX12_COMPUTE_MATERIAL_COMMON_H
#define DX12_COMPUTE_MATERIAL_COMMON_H
#pragma once

#include "shaderapi/ishaderapidx12.h"
#include "tier1/interface.h"

// stdshader_dx12 only runs on shaderapidx12, which always exports the compute interface.
inline IShaderAPIDX12Compute *DX12ComputeAPI()
{
	static IShaderAPIDX12Compute *s_pCompute = static_cast<IShaderAPIDX12Compute *>( Sys_GetFactory( "shaderapidx12" )( SHADERAPIDX12_COMPUTE_INTERFACE_VERSION, nullptr ) );
	return s_pCompute;
}

inline ShaderAPIDX12ComputeResource_t DX12MaterialTexture( ShaderAPITextureHandle_t hTexture, int nMip = 0, int nMipCount = 0 )
{
	ShaderAPIDX12ComputeResource_t resource = {};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_TEXTURE;
	resource.m_hTexture = hTexture;
	resource.m_nMip = nMip;
	resource.m_nMipCount = nMipCount;
	return resource;
}

inline ShaderAPIDX12ComputeResource_t DX12MaterialSceneDepth()
{
	ShaderAPIDX12ComputeResource_t resource = {};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_DEPTH;
	return resource;
}

#endif // DX12_COMPUTE_MATERIAL_COMMON_H
