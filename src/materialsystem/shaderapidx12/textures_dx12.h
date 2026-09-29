#pragma once

#include <d3d12.h>
#include <array>
#include <wrl/client.h>
#include "bitmap/imageformat.h"
#include "shaderapi/ishaderapi.h"

namespace shaderapidx12
{
class CShaderAPIDX12;

struct RenderTargetBindingDX12
{
    static constexpr int kMaxColorTargets = 4;
    std::array<ID3D12Resource *,kMaxColorTargets> colors{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,kMaxColorTargets> rtvs{};
    std::array<DXGI_FORMAT,kMaxColorTargets> colorFormats{};
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

DXGI_FORMAT ImageFormatToDXGI12(ImageFormat format, int flags = 0, bool depth = false);
ImageFormat DXGI12ToImageFormat(DXGI_FORMAT format);

// Native texture entry points are implemented separately from the API state machine.
bool PrepareSampledTextureDX12(CShaderAPIDX12 &, ShaderAPITextureHandle_t, bool, ID3D12Resource **, D3D12_SHADER_RESOURCE_VIEW_DESC &, D3D12_SAMPLER_DESC &, D3D12_CPU_DESCRIPTOR_HANDLE *, bool);
bool PrepareRenderTargetsDX12(CShaderAPIDX12 &, RenderTargetBindingDX12 &, bool);
}
