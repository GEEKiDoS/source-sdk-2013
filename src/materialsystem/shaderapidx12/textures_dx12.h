//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Native D3D12 texture formats, render-target bindings and texture entry points.
//
//=============================================================================//

#ifndef TEXTURES_DX12_H
#define TEXTURES_DX12_H
#pragma once

#include "bitmap/imageformat.h"
#include "shaderapi/ishaderapi.h"
#include <d3d12.h>

namespace shaderapidx12
{
class CShaderAPIDX12;

struct RenderTargetBindingDX12
{
	static constexpr int kMaxColorTargets = 4;
	ID3D12Resource *colors[kMaxColorTargets]{};
	D3D12_CPU_DESCRIPTOR_HANDLE rtvs[kMaxColorTargets]{};
	DXGI_FORMAT colorFormats[kMaxColorTargets]{};
	int colorCount = 0;
	ID3D12Resource *color = nullptr;
	ID3D12Resource *depth = nullptr;
	D3D12_CPU_DESCRIPTOR_HANDLE rtv{};
	D3D12_CPU_DESCRIPTOR_HANDLE dsv{};
	DXGI_FORMAT colorFormat = DXGI_FORMAT_UNKNOWN;
	DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
	int sampleCount = 1;
	int sampleQuality = 0;
	int width = 0;
	int height = 0;
};

DXGI_FORMAT ImageFormatToDXGI12( ImageFormat format, int nFlags = 0, bool bDepth = false );
ImageFormat DXGI12ToImageFormat( DXGI_FORMAT format );

// Native texture entry points are implemented separately from the API state machine.
bool PrepareSampledTextureDX12( CShaderAPIDX12 &api, ShaderAPITextureHandle_t hTexture, bool bSRGB, ID3D12Resource **ppResource, D3D12_SHADER_RESOURCE_VIEW_DESC &srv, D3D12_SAMPLER_DESC &sampler, D3D12_CPU_DESCRIPTOR_HANDLE *pSource, bool bComparison );
bool PrepareRenderTargetsDX12( CShaderAPIDX12 &api, RenderTargetBindingDX12 &binding, bool bEncodeSRGB );
} // namespace shaderapidx12

#endif // TEXTURES_DX12_H
