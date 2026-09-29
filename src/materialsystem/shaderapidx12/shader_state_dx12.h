#pragma once
#include "shaderapi/ishaderapi.h"
#include "shadershadow_dx12.h"
#include <array>
namespace shaderapidx12
{
struct EffectiveStateDX12
{
    ShaderRasterState_t raster{};
    bool depthEnable=true,depthWrite=true,blendEnable=false,alphaTest=false,scissor=false,alphaToCoverage=false,colorWrite=true;
    ShaderBlendFactor_t srcBlend=SHADER_BLEND_ONE,dstBlend=SHADER_BLEND_ZERO;
    ShaderBlendOp_t blendOp=SHADER_BLEND_OP_ADD;
    float alphaRef=0.0f;
    uint32 stencilReadMask=0xff,stencilWriteMask=0xff;int stencilRef=0;
};
class CShaderStateDX12
{
public:
    void Reset();
    void ApplyShadow(const CShaderShadowDX12 &shadow);
    void OverrideDepth(bool enabled,bool write);
    void OverrideColorWrite(bool enabled,bool write);
    const EffectiveStateDX12 &Effective()const{return state_;}
private:EffectiveStateDX12 state_{};
};
} // namespace shaderapidx12
