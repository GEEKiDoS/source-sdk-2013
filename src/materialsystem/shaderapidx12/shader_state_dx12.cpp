#include "shader_state_dx12.h"
namespace shaderapidx12
{
void CShaderStateDX12::Reset(){state_=EffectiveStateDX12{};}
void CShaderStateDX12::OverrideDepth(bool enabled,bool write){state_.depthEnable=enabled;state_.depthWrite=write;}
void CShaderStateDX12::ApplyShadow(const CShaderShadowDX12 &shadow){state_.depthEnable=shadow.DepthTest();state_.depthWrite=shadow.DepthWrites();state_.blendEnable=shadow.Blending();state_.alphaTest=shadow.AlphaTest();}
void CShaderStateDX12::OverrideColorWrite(bool enabled,bool write){if(enabled)state_.colorWrite=write;}
} // namespace shaderapidx12
