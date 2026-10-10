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

// World-space shading normals plus the depth they were drawn at (R32G32_UINT, see PBR_Output) and F0 + roughness (R8G8B8A8_UNORM)
// the PBR pixel shaders write during the opaque scene pass. A pixel is PBR data only while the scene depth equals the stored depth
// and specular alpha is nonzero (README "PBR G-buffer contract"). Resolved by the backend; null (reads 0) until the first
// G-buffer pass of the frame and while the PBR override is off.
inline ShaderAPIDX12ComputeResource_t DX12MaterialPbrNormals()
{
	ShaderAPIDX12ComputeResource_t resource = {};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_PBR_NORMALS;
	return resource;
}

inline ShaderAPIDX12ComputeResource_t DX12MaterialPbrSpecular()
{
	ShaderAPIDX12ComputeResource_t resource = {};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_PBR_SPECULAR;
	return resource;
}

#endif // DX12_COMPUTE_MATERIAL_COMMON_H
