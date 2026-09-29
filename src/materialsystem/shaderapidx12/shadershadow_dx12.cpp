#include "shadershadow_dx12.h"
#include <algorithm>

namespace shaderapidx12
{
CShaderShadowDX12 *g_pShaderShadowDX12 = nullptr;
void CShaderShadowDX12::SetDefaultState()
{
    *this=CShaderShadowDX12();
    depthFunc_=SHADER_DEPTHFUNC_NEAREROREQUAL; depthWrites_=true; depthTest_=true;
    colorWrites_=true; alphaWrites_=false; alphaTest_=false; alphaFunc_=SHADER_ALPHAFUNC_GEQUAL; alphaRef_=0.7f;
    culling_=true; blending_=false; blendSrc_=SHADER_BLEND_ONE; blendDst_=SHADER_BLEND_ZERO;
    blendOp_=SHADER_BLEND_OP_ADD; separateAlpha_=false; blendAlphaSrc_=SHADER_BLEND_ONE; blendAlphaDst_=SHADER_BLEND_ZERO; blendAlphaOp_=SHADER_BLEND_OP_ADD;
    polyFront_=polyBack_=SHADER_POLYMODE_FILL; polyOffset_=SHADER_POLYOFFSET_DISABLE;
    drawFlags_=SHADER_DRAW_POSITION; materialSource_=SHADER_MATERIALSOURCE_MATERIAL;
    for(size_t i=0;i<overbright_.size();++i){overbright_[i]=1.f;texgen_[i]=false;textureAlpha_[i]=false;texgenParam_[i]=SHADER_TEXGENPARAM_OBJECT_LINEAR;for(int c=0;c<2;++c){texOps_[i][c]=SHADER_TEXOP_DISABLE;texArgs_[i][c][0]=SHADER_TEXARG_TEXTURE;texArgs_[i][c][1]=SHADER_TEXARG_PREVIOUSSTAGE;}}
}
void CShaderShadowDX12::DepthFunc(ShaderDepthFunc_t v){depthFunc_=v;}
void CShaderShadowDX12::EnableDepthWrites(bool v){depthWrites_=v;}
void CShaderShadowDX12::EnableDepthTest(bool v){depthTest_=v;}
void CShaderShadowDX12::EnablePolyOffset(PolygonOffsetMode_t v){polyOffset_=v;}
void CShaderShadowDX12::EnableStencil(bool v){stencil_=v;}
void CShaderShadowDX12::StencilFunc(ShaderStencilFunc_t v){stencilFunc_=v;}
void CShaderShadowDX12::StencilPassOp(ShaderStencilOp_t v){stencilPass_=v;}
void CShaderShadowDX12::StencilFailOp(ShaderStencilOp_t v){stencilFail_=v;}
void CShaderShadowDX12::StencilDepthFailOp(ShaderStencilOp_t v){stencilDepthFail_=v;}
void CShaderShadowDX12::StencilReference(int value){stencilReference_=static_cast<uint8_t>(std::clamp(value,0,255));}
void CShaderShadowDX12::StencilMask(int mask){stencilMask_=static_cast<uint8_t>(std::clamp(mask,0,255));}
void CShaderShadowDX12::StencilWriteMask(int mask){stencilWriteMask_=static_cast<uint8_t>(std::clamp(mask,0,255));}
void CShaderShadowDX12::EnableColorWrites(bool v){colorWrites_=v;}
void CShaderShadowDX12::EnableAlphaWrites(bool v){alphaWrites_=v;}
void CShaderShadowDX12::EnableBlending(bool v){blending_=v;}
void CShaderShadowDX12::BlendFunc(ShaderBlendFactor_t s,ShaderBlendFactor_t d){blendSrc_=s;blendDst_=d;}
void CShaderShadowDX12::EnableAlphaTest(bool v){alphaTest_=v;}
void CShaderShadowDX12::AlphaFunc(ShaderAlphaFunc_t f,float r){alphaFunc_=f;alphaRef_=std::clamp(r,0.0f,1.0f);}
void CShaderShadowDX12::PolyMode(ShaderPolyModeFace_t face,ShaderPolyMode_t mode){if(mode<SHADER_POLYMODE_POINT||mode>SHADER_POLYMODE_FILL)return;if(face==SHADER_POLYMODEFACE_FRONT||face==SHADER_POLYMODEFACE_FRONT_AND_BACK)polyFront_=mode;if(face==SHADER_POLYMODEFACE_BACK||face==SHADER_POLYMODEFACE_FRONT_AND_BACK)polyBack_=mode;}
void CShaderShadowDX12::EnableCulling(bool v){culling_=v;}
void CShaderShadowDX12::EnableConstantColor(bool v){constantColor_=v;}
void CShaderShadowDX12::VertexShaderVertexFormat(unsigned int f,int n,int *dims,int u){vertexFlags_=f&~VERTEX_BONE_INDEX;texCoordCount_=std::clamp(n,0,8);userDataSize_=std::clamp(u,0,4);for(int i=0;i<texCoordCount_;++i)texCoordDimensions_[i]=dims?std::clamp(dims[i],1,4):2;}
void CShaderShadowDX12::SetVertexShader(const char *name,int staticIndex){vertexShader_=name?name:"";staticVertexIndex_=staticIndex;}
void CShaderShadowDX12::SetPixelShader(const char *name,int staticIndex){pixelShader_=name?name:"";staticPixelIndex_=staticIndex;}
void CShaderShadowDX12::EnableLighting(bool v){lighting_=v;}
void CShaderShadowDX12::EnableSpecular(bool v){specular_=v;}
void CShaderShadowDX12::EnableSRGBWrite(bool v){srgbWrite_=v;}
void CShaderShadowDX12::EnableSRGBRead(Sampler_t s,bool v){if(s>=0&&s<(int)srgbRead_.size())srgbRead_[s]=v;}
void CShaderShadowDX12::EnableVertexBlend(bool v){vertexBlend_=v;}
void CShaderShadowDX12::OverbrightValue(TextureStage_t stage,float value){if(stage>=0&&stage<(int)overbright_.size())overbright_[stage]=std::max(0.f,value);}
void CShaderShadowDX12::EnableTexture(Sampler_t s,bool v){if(s>=0&&s<(int)textures_.size())textures_[s]=v;}
void CShaderShadowDX12::EnableTexGen(TextureStage_t stage,bool v){if(stage>=0&&stage<(int)texgen_.size())texgen_[stage]=v;}
void CShaderShadowDX12::TexGen(TextureStage_t stage,ShaderTexGenParam_t p){if(stage>=0&&stage<(int)texgenParam_.size())texgenParam_[stage]=p;}
void CShaderShadowDX12::EnableCustomPixelPipe(bool v){customPipe_=v;}
void CShaderShadowDX12::CustomTextureStages(int n){customStages_=std::clamp(n,0,16);}
void CShaderShadowDX12::CustomTextureOperation(TextureStage_t stage,ShaderTexChannel_t channel,ShaderTexOp_t op,ShaderTexArg_t arg1,ShaderTexArg_t arg2){if(stage<0||stage>=16||channel<0||channel>1)return;texOps_[stage][channel]=op;texArgs_[stage][channel][0]=arg1;texArgs_[stage][channel][1]=arg2;}
void CShaderShadowDX12::DrawFlags(unsigned int v){drawFlags_=v;}
void CShaderShadowDX12::EnableAlphaPipe(bool v){alphaPipe_=v;}
void CShaderShadowDX12::EnableConstantAlpha(bool v){constantAlpha_=v;}
void CShaderShadowDX12::EnableVertexAlpha(bool v){vertexAlpha_=v;}
void CShaderShadowDX12::EnableTextureAlpha(TextureStage_t s,bool v){if(s>=0&&s<(int)textureAlpha_.size())textureAlpha_[s]=v;}
void CShaderShadowDX12::EnableBlendingSeparateAlpha(bool v){separateAlpha_=v;}
void CShaderShadowDX12::BlendFuncSeparateAlpha(ShaderBlendFactor_t s,ShaderBlendFactor_t d){blendAlphaSrc_=s;blendAlphaDst_=d;}
void CShaderShadowDX12::FogMode(ShaderFogMode_t v){fogMode_=v;}
void CShaderShadowDX12::SetDiffuseMaterialSource(ShaderMaterialSource_t v){materialSource_=v;}
void CShaderShadowDX12::SetMorphFormat(MorphFormat_t v){morphFormat_=v;}
void CShaderShadowDX12::DisableFogGammaCorrection(bool v){fogGammaDisabled_=v;}
void CShaderShadowDX12::EnableAlphaToCoverage(bool v){alphaToCoverage_=v;}
void CShaderShadowDX12::SetShadowDepthFiltering(Sampler_t stage){if(stage>=0&&stage<16)comparisonSamplerMask_|=static_cast<uint16_t>(1u<<stage);}
void CShaderShadowDX12::BlendOp(ShaderBlendOp_t v){blendOp_=v;}
void CShaderShadowDX12::BlendOpSeparateAlpha(ShaderBlendOp_t v){blendAlphaOp_=v;}
} // namespace shaderapidx12
