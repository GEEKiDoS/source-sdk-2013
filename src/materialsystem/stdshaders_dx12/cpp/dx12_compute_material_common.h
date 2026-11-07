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

// World-space shading normal (R16G16_UNORM octahedral [0,1], see PBR_Output) and F0 + roughness (R8G8B8A8_UNORM)
// the PBR pixel shaders write during the opaque main-view pass. A pixel is PBR data only while stencil bit 0x80 is
// set (README "PBR G-buffer contract"). Resolved by the backend; null (reads 0) until the first G-buffer pass of the frame.
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

// Stencil plane of the scene depth (X24_TYPELESS_G8_UINT, plane 1). Texture2D<uint2> / Texture2DMS<uint2>, value in .y.
// Bit 0x80 is set on the nearest depth-writing PBR surface of the main-view G-buffer pass.
inline ShaderAPIDX12ComputeResource_t DX12MaterialSceneStencil()
{
	ShaderAPIDX12ComputeResource_t resource = {};
	resource.m_nKind = SHADERAPIDX12_COMPUTE_RESOURCE_SCENE_STENCIL;
	return resource;
}

#endif // DX12_COMPUTE_MATERIAL_COMMON_H
